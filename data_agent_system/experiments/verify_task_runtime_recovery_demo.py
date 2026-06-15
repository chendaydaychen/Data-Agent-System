#!/usr/bin/env python3
import sys
from pathlib import Path


def main() -> None:
    if len(sys.argv) != 3:
        print(
            "usage: verify_task_runtime_recovery_demo.py <actual.txt> <expected.txt>",
            file=sys.stderr,
        )
        raise SystemExit(1)

    actual = Path(sys.argv[1]).read_text().splitlines()
    expected = Path(sys.argv[2]).read_text().splitlines()
    if actual != expected:
        raise SystemExit("task runtime recovery demo output does not match expected output")
    print("task_runtime_recovery_demo_verification=ok")


if __name__ == "__main__":
    main()
