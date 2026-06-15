#!/usr/bin/env python3
import csv
import sys
from pathlib import Path


def load_csv(path: Path) -> list:
    with path.open("r", newline="") as handle:
        return list(csv.DictReader(handle))


def main() -> None:
    if len(sys.argv) != 3:
        print("usage: verify_replayed_state.py <replayed.csv> <expected.csv>", file=sys.stderr)
        raise SystemExit(1)

    replayed = load_csv(Path(sys.argv[1]))
    expected = load_csv(Path(sys.argv[2]))
    if replayed != expected:
        raise SystemExit("replayed state does not match expected state")
    print("replayed_state_verification=ok")


if __name__ == "__main__":
    main()
