#!/usr/bin/env python3
import csv
import sys
from pathlib import Path


def unescape(text: str) -> str:
    output = []
    escaped = False
    for char in text:
        if escaped:
            if char == "t":
                output.append("\t")
            elif char == "n":
                output.append("\n")
            elif char == "\\":
                output.append("\\")
            else:
                output.append(char)
            escaped = False
        elif char == "\\":
            escaped = True
        else:
            output.append(char)
    if escaped:
        output.append("\\")
    return "".join(output)


def parse_commit_log(path: Path) -> dict:
    if not path.exists():
        raise SystemExit(f"missing commit log: {path}")

    lines = path.read_text().splitlines()
    if not lines or lines[0] != "DAS_COMMIT_LOG_V1":
        raise SystemExit(f"commit log missing DAS_COMMIT_LOG_V1 header: {path}")
    if "[entries]" not in lines:
        raise SystemExit(f"commit log missing [entries] section: {path}")

    split_index = lines.index("[entries]")
    metadata_lines = lines[1:split_index]
    entry_lines = lines[split_index + 1 :]

    metadata = {}
    for line in metadata_lines:
        if "=" not in line:
            raise SystemExit(f"invalid metadata line in {path}: {line}")
        key, value = line.split("=", 1)
        metadata[key] = unescape(value)

    metadata["entries"] = []
    for line in entry_lines:
        if not line:
            continue
        parts = line.split("\t")
        if len(parts) != 3:
            raise SystemExit(f"invalid commit entry line in {path}: {line}")
        metadata["entries"].append([unescape(parts[0]), parts[1], unescape(parts[2])])
    return metadata


def main() -> None:
    if len(sys.argv) != 3:
        print("usage: verify_commit_logs.py <results.csv> <commit_log_dir>", file=sys.stderr)
        raise SystemExit(1)

    results_path = Path(sys.argv[1])
    commit_log_dir = Path(sys.argv[2])
    if not results_path.exists():
        raise SystemExit(f"missing results csv: {results_path}")
    if not commit_log_dir.exists():
        raise SystemExit(f"missing commit log directory: {commit_log_dir}")

    with results_path.open("r", newline="") as handle:
        reader = csv.DictReader(handle)
        for row in reader:
            task_id = row["task_id"]
            committed = int(row["committed"])
            log_path = commit_log_dir / f"{task_id}.commit.log"
            log = parse_commit_log(log_path)
            status = log.get("status")
            entry_count = int(log.get("entry_count", "0"))
            if committed == 1 and status != "COMMITTED":
                raise SystemExit(f"{task_id} expected COMMITTED log status")
            if committed == 0 and status != "ABORTED":
                raise SystemExit(f"{task_id} expected ABORTED log status")
            if entry_count != len(log["entries"]):
                raise SystemExit(f"{task_id} entry_count mismatch")
            if committed == 1 and entry_count < 1:
                raise SystemExit(f"{task_id} committed txn must have at least one commit entry")
            if committed == 0 and entry_count != 0:
                raise SystemExit(f"{task_id} aborted txn must not have commit entries")

    print("commit_log_verification=ok")


if __name__ == "__main__":
    main()
