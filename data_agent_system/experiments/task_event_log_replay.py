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


def replay_task_logs(task_log_dir: Path) -> list:
    rows = []
    for path in sorted(task_log_dir.glob("*.task.log")):
      log = parse_task_log(path)
      events = log["events"]
      final_event = events[-1] if events else {"event_type": "", "branch_id": ""}
      rows.append(
          {
              "task_id": log.get("task_id", ""),
              "txn_id": log.get("txn_id", ""),
              "task_phase": log.get("task_phase", ""),
              "commit_attempts": int(log.get("commit_attempts", "0")),
              "committed": int(log.get("committed", "0")),
              "event_count": len(events),
              "final_event": final_event["event_type"],
              "final_branch_id": final_event["branch_id"],
              "read_event_count": sum(1 for event in events if event["event_type"] == "READ"),
              "write_event_count": sum(1 for event in events if event["event_type"] == "WRITE"),
              "source_log": path.name,
          }
      )
    return rows


def write_state_csv(rows: list, output_path: Path) -> None:
    with output_path.open("w", newline="") as handle:
        writer = csv.writer(handle)
        writer.writerow(
            [
                "task_id",
                "txn_id",
                "task_phase",
                "commit_attempts",
                "committed",
                "event_count",
                "final_event",
                "final_branch_id",
                "read_event_count",
                "write_event_count",
                "source_log",
            ]
        )
        for row in rows:
            writer.writerow(
                [
                    row["task_id"],
                    row["txn_id"],
                    row["task_phase"],
                    row["commit_attempts"],
                    row["committed"],
                    row["event_count"],
                    row["final_event"],
                    row["final_branch_id"],
                    row["read_event_count"],
                    row["write_event_count"],
                    row["source_log"],
                ]
            )


def main() -> None:
    if len(sys.argv) not in (2, 3):
        print("usage: task_event_log_replay.py <task_log_dir> [output.csv]", file=sys.stderr)
        raise SystemExit(1)

    task_log_dir = Path(sys.argv[1])
    if not task_log_dir.exists():
        raise SystemExit(f"missing task log directory: {task_log_dir}")

    rows = replay_task_logs(task_log_dir)
    if len(sys.argv) == 2:
        writer = csv.writer(sys.stdout)
        writer.writerow(
            [
                "task_id",
                "txn_id",
                "task_phase",
                "commit_attempts",
                "committed",
                "event_count",
                "final_event",
                "final_branch_id",
                "read_event_count",
                "write_event_count",
                "source_log",
            ]
        )
        for row in rows:
            writer.writerow(
                [
                    row["task_id"],
                    row["txn_id"],
                    row["task_phase"],
                    row["commit_attempts"],
                    row["committed"],
                    row["event_count"],
                    row["final_event"],
                    row["final_branch_id"],
                    row["read_event_count"],
                    row["write_event_count"],
                    row["source_log"],
                ]
            )
        return

    write_state_csv(rows, Path(sys.argv[2]))


if __name__ == "__main__":
    main()
