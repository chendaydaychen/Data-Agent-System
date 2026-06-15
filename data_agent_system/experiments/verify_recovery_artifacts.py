#!/usr/bin/env python3
import csv
import sys
from pathlib import Path
from typing import Dict, Tuple


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


def parse_store(path: Path) -> Dict[str, Tuple[int, str]]:
    lines = path.read_text().splitlines()
    if not lines or lines[0] != "DAS_KV_V1":
        raise SystemExit("recovery store snapshot missing DAS_KV_V1 header")
    result: Dict[str, Tuple[int, str]] = {}
    for raw_line in lines[1:]:
        if not raw_line:
            continue
        parts = raw_line.split("\t")
        if len(parts) != 3:
            raise SystemExit("recovery store snapshot line must have 3 fields")
        result[unescape(parts[0])] = (int(parts[1]), unescape(parts[2]))
    return result


def parse_expected(path: Path) -> Dict[str, Tuple[int, str]]:
    expected: Dict[str, Tuple[int, str]] = {}
    with path.open("r", newline="") as handle:
        reader = csv.DictReader(handle)
        for row in reader:
            expected[row["key"]] = (int(row["version"]), row["value"])
    return expected


def verify_journal(path: Path, store_path: Path, temp_path: Path) -> None:
    lines = path.read_text().splitlines()
    if len(lines) < 4:
        raise SystemExit("journal file too short")
    if lines[0] != "DAS_KV_JOURNAL_V1":
        raise SystemExit("journal missing DAS_KV_JOURNAL_V1 header")
    values = {}
    for line in lines[1:]:
        if "=" not in line:
            raise SystemExit("journal line missing '=' separator")
        key, value = line.split("=", 1)
        values[key] = unescape(value)
    if values.get("mode") != "replace_with_temp":
        raise SystemExit("journal mode must be replace_with_temp")
    if values.get("target") != str(store_path):
        raise SystemExit("journal target does not match store path")
    if values.get("temp") != str(temp_path):
        raise SystemExit("journal temp path does not match temp path")


def main() -> None:
    if len(sys.argv) != 5:
        print(
            "usage: verify_recovery_artifacts.py <store.tsv> <temp.tsv> <journal> <expected.csv>",
            file=sys.stderr,
        )
        raise SystemExit(1)

    store_path = Path(sys.argv[1])
    temp_path = Path(sys.argv[2])
    journal_path = Path(sys.argv[3])
    expected_path = Path(sys.argv[4])

    if not temp_path.exists():
        raise SystemExit(f"missing temp snapshot: {temp_path}")
    if not journal_path.exists():
        raise SystemExit(f"missing journal file: {journal_path}")

    verify_journal(journal_path, store_path, temp_path)
    temp_entries = parse_store(temp_path)
    expected_entries = parse_expected(expected_path)
    if temp_entries != expected_entries:
        raise SystemExit("temp snapshot does not match expected recovered state")

    print("recovery_artifact_verification=ok")


if __name__ == "__main__":
    main()
