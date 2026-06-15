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
    lines = path.read_text().splitlines()
    if not lines or lines[0] != "DAS_COMMIT_LOG_V1":
        raise SystemExit(f"commit log missing DAS_COMMIT_LOG_V1 header: {path}")
    if "[entries]" not in lines:
        raise SystemExit(f"commit log missing [entries] section: {path}")

    split_index = lines.index("[entries]")
    metadata = {}
    for line in lines[1:split_index]:
        if "=" not in line:
            raise SystemExit(f"invalid metadata line in {path}: {line}")
        key, value = line.split("=", 1)
        metadata[key] = unescape(value)

    entries = []
    for line in lines[split_index + 1 :]:
        if not line:
            continue
        parts = line.split("\t")
        if len(parts) != 3:
            raise SystemExit(f"invalid commit entry line in {path}: {line}")
        entries.append([unescape(parts[0]), parts[1], unescape(parts[2])])
    metadata["entries"] = entries
    return metadata


def replay_commit_logs(commit_log_dir: Path) -> dict:
    state = {}
    for path in sorted(commit_log_dir.glob("*.commit.log")):
        log = parse_commit_log(path)
        if log.get("status") != "COMMITTED":
            continue
        for key, expected_version, value in log["entries"]:
            state[key] = {
                "value": value,
                "expected_version": int(expected_version),
                "source_log": path.name,
            }
    return state


def write_state_csv(state: dict, output_path: Path) -> None:
    with output_path.open("w", newline="") as handle:
        writer = csv.writer(handle)
        writer.writerow(["key", "value", "expected_version", "source_log"])
        for key in sorted(state.keys()):
            entry = state[key]
            writer.writerow([key, entry["value"], entry["expected_version"], entry["source_log"]])


def main() -> None:
    if len(sys.argv) not in (2, 3):
        print("usage: commit_log_replay.py <commit_log_dir> [output.csv]", file=sys.stderr)
        raise SystemExit(1)

    commit_log_dir = Path(sys.argv[1])
    if not commit_log_dir.exists():
        raise SystemExit(f"missing commit log directory: {commit_log_dir}")

    state = replay_commit_logs(commit_log_dir)
    if len(sys.argv) == 2:
        writer = csv.writer(sys.stdout)
        writer.writerow(["key", "value", "expected_version", "source_log"])
        for key in sorted(state.keys()):
            entry = state[key]
            writer.writerow([key, entry["value"], entry["expected_version"], entry["source_log"]])
        return

    write_state_csv(state, Path(sys.argv[2]))


if __name__ == "__main__":
    main()
