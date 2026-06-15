#!/usr/bin/env python3
import csv
import sys
from pathlib import Path
from typing import Dict, Tuple


def verify_csv(csv_path: Path) -> None:
    required_columns = {
        "task_id",
        "committed",
        "branch_count",
        "planned_loser_count",
        "winner_commit_count",
        "real_abort_count",
        "conflict_abort_count",
        "validation_fail_count",
        "retry_count",
        "commit_latency_us",
    }

    with csv_path.open("r", newline="") as handle:
        reader = csv.DictReader(handle)
        missing = required_columns.difference(reader.fieldnames or [])
        if missing:
            raise SystemExit(f"csv missing required columns: {sorted(missing)}")

        row_count = 0
        for row in reader:
            row_count += 1
            branch_count = int(row["branch_count"])
            planned_loser_count = int(row["planned_loser_count"])
            committed = int(row["committed"])
            winner_commit_count = int(row["winner_commit_count"])
            real_abort_count = int(row["real_abort_count"])

            if branch_count < 1:
                raise SystemExit("branch_count must be >= 1")
            if planned_loser_count != branch_count - 1:
                raise SystemExit("planned_loser_count must equal branch_count - 1")
            if committed not in (0, 1):
                raise SystemExit("committed must be 0 or 1")
            if winner_commit_count not in (0, 1):
                raise SystemExit("winner_commit_count must be 0 or 1")
            if real_abort_count not in (0, 1):
                raise SystemExit("real_abort_count must be 0 or 1")
            if committed == 1 and winner_commit_count != 1:
                raise SystemExit("committed row must have winner_commit_count=1")
            if committed == 0 and real_abort_count != 1:
                raise SystemExit("aborted row must have real_abort_count=1")

        if row_count == 0:
            raise SystemExit("csv must contain at least one data row")


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


def parse_store(store_path: Path) -> Dict[str, Tuple[int, str]]:
    if not store_path.exists():
        raise SystemExit(f"missing store file: {store_path}")

    lines = store_path.read_text().splitlines()
    if not lines:
        raise SystemExit("store file must not be empty")
    if lines[0] != "DAS_KV_V1":
        raise SystemExit("store file missing DAS_KV_V1 header")

    seen_keys = set()
    parsed: Dict[str, Tuple[int, str]] = {}
    for line_no, raw_line in enumerate(lines[1:], start=2):
        if not raw_line:
            continue
        parts = raw_line.split("\t")
        if len(parts) != 3:
            raise SystemExit(f"store line {line_no} must have exactly 3 tab-separated fields")
        key = unescape(parts[0])
        version = int(parts[1])
        value = unescape(parts[2])
        if not key:
            raise SystemExit(f"store line {line_no} has empty key")
        if version < 1:
            raise SystemExit(f"store line {line_no} has invalid version")
        if key in seen_keys:
            raise SystemExit(f"duplicate key in store file: {key}")
        seen_keys.add(key)
        parsed[key] = (version, value)

    if not seen_keys:
        raise SystemExit("store file must contain at least one key")
    return parsed


def verify_store(store_path: Path) -> None:
    _ = parse_store(store_path)


def verify_snapshot(store_path: Path, snapshot_path: Path) -> None:
    if not snapshot_path.exists():
        raise SystemExit(f"missing snapshot file: {snapshot_path}")

    store_entries = parse_store(store_path)
    expected_entries: Dict[str, Tuple[int, str]] = {}
    with snapshot_path.open("r", newline="") as handle:
        reader = csv.DictReader(handle)
        required_columns = {"key", "version", "value"}
        missing = required_columns.difference(reader.fieldnames or [])
        if missing:
            raise SystemExit(f"snapshot missing required columns: {sorted(missing)}")
        for row in reader:
            expected_entries[row["key"]] = (int(row["version"]), row["value"])

    if store_entries != expected_entries:
        raise SystemExit("store contents do not match expected snapshot")


def main() -> None:
    if len(sys.argv) not in (2, 3, 4):
        print("usage: verify_artifacts.py <results.csv> [store.tsv] [expected_snapshot.csv]", file=sys.stderr)
        raise SystemExit(1)

    csv_path = Path(sys.argv[1])
    if not csv_path.exists():
        raise SystemExit(f"missing csv file: {csv_path}")
    verify_csv(csv_path)

    if len(sys.argv) >= 3:
        verify_store(Path(sys.argv[2]))
    if len(sys.argv) == 4:
        verify_snapshot(Path(sys.argv[2]), Path(sys.argv[3]))

    print("artifact_verification=ok")


if __name__ == "__main__":
    main()
