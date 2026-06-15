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


def parse_task_log(path: Path) -> dict:
    if not path.exists():
        raise SystemExit(f"missing task event log: {path}")

    lines = path.read_text().splitlines()
    if not lines or lines[0] != "DAS_TASK_EVENT_LOG_V1":
        raise SystemExit(f"task event log missing DAS_TASK_EVENT_LOG_V1 header: {path}")
    if "[events]" not in lines:
        raise SystemExit(f"task event log missing [events] section: {path}")

    split_index = lines.index("[events]")
    metadata = {}
    for line in lines[1:split_index]:
        if "=" not in line:
            raise SystemExit(f"invalid metadata line in {path}: {line}")
        key, value = line.split("=", 1)
        metadata[key] = unescape(value)

    events = []
    for line in lines[split_index + 1 :]:
        if not line:
            continue
        parts = line.split("\t")
        if len(parts) != 6:
            raise SystemExit(f"invalid task event line in {path}: {line}")
        events.append(
            {
                "sequence_number": int(parts[0]),
                "event_type": parts[1],
                "branch_id": unescape(parts[2]),
                "object_id": unescape(parts[3]),
                "numeric_value": int(parts[4]),
                "detail": unescape(parts[5]),
            }
        )
    metadata["events"] = events
    return metadata


def main() -> None:
    if len(sys.argv) != 3:
        print("usage: verify_task_event_logs.py <results.csv> <task_log_dir>", file=sys.stderr)
        raise SystemExit(1)

    results_path = Path(sys.argv[1])
    task_log_dir = Path(sys.argv[2])
    if not results_path.exists():
        raise SystemExit(f"missing results csv: {results_path}")
    if not task_log_dir.exists():
        raise SystemExit(f"missing task log directory: {task_log_dir}")

    with results_path.open("r", newline="") as handle:
        reader = csv.DictReader(handle)
        for row in reader:
            task_id = row["task_id"]
            committed = int(row["committed"])
            task_log = parse_task_log(task_log_dir / f"{task_id}.task.log")

            if task_log.get("task_id") != task_id:
                raise SystemExit(f"{task_id} task_id mismatch in task event log")
            if int(task_log.get("commit_attempts", "0")) < 1:
                raise SystemExit(f"{task_id} task event log missing commit attempt")

            events = task_log["events"]
            if int(task_log.get("event_count", "0")) != len(events):
                raise SystemExit(f"{task_id} event_count mismatch")
            if not events:
                raise SystemExit(f"{task_id} task event log must contain events")
            if events[0]["event_type"] != "SUBMIT_TASK":
                raise SystemExit(f"{task_id} first event must be SUBMIT_TASK")
            if events[-1]["event_type"] not in ("COMMIT_TASK", "ABORT_TASK"):
                raise SystemExit(f"{task_id} final event must be COMMIT_TASK or ABORT_TASK")
            for index, event in enumerate(events):
                if event["sequence_number"] != index:
                    raise SystemExit(f"{task_id} event sequence mismatch at index {index}")

            if committed == 1:
                if task_log.get("committed") != "1":
                    raise SystemExit(f"{task_id} expected committed=1 in task event log")
                if events[-1]["event_type"] != "COMMIT_TASK":
                    raise SystemExit(f"{task_id} expected final COMMIT_TASK event")
            else:
                if task_log.get("committed") != "0":
                    raise SystemExit(f"{task_id} expected committed=0 in task event log")
                if events[-1]["event_type"] != "ABORT_TASK":
                    raise SystemExit(f"{task_id} expected final ABORT_TASK event")

    print("task_event_log_verification=ok")


if __name__ == "__main__":
    main()
