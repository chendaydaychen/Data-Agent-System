#!/usr/bin/env python3
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


def parse_store_config(path: Path) -> dict:
    lines = path.read_text().splitlines()
    if not lines or lines[0] != "DAS_STORE_CONFIG_V1":
      raise SystemExit("store config missing DAS_STORE_CONFIG_V1 header")

    parsed = {}
    for line in lines[1:]:
        if "=" not in line:
            raise SystemExit(f"invalid store config line: {line}")
        key, value = line.split("=", 1)
        parsed[key] = unescape(value)
    return parsed


def main() -> None:
    if len(sys.argv) != 2:
        print("usage: verify_store_config.py <store_config.txt>", file=sys.stderr)
        raise SystemExit(1)

    path = Path(sys.argv[1])
    if not path.exists():
        raise SystemExit(f"missing store config artifact: {path}")

    parsed = parse_store_config(path)
    required = {
        "kind",
        "path",
        "namespace_prefix",
        "host",
        "port",
        "database_index",
        "column_family",
    }
    missing = required.difference(parsed.keys())
    if missing:
        raise SystemExit(f"store config missing fields: {sorted(missing)}")

    print("store_config_verification=ok")


if __name__ == "__main__":
    main()
