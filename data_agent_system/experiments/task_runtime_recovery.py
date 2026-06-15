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


def recover_runtime(task_log_dir: Path) -> list:
    rows = []
    for path in sorted(task_log_dir.glob("*.task.log")):
        log = parse_task_log(path)
        events = log["events"]
        final_event = events[-1] if events else {"event_type": "", "branch_id": "", "detail": ""}
        committed = log.get("committed", "0") == "1"
        task_phase = log.get("task_phase", "")

        def latest_branch() -> str:
            for event in reversed(events):
                if event["branch_id"]:
                    return event["branch_id"]
            return ""

        def latest_savepoint(branch_id: str) -> str:
            for event in reversed(events):
                if event["branch_id"] == branch_id and event["event_type"] == "SAVEPOINT":
                    return event["detail"]
            return ""

        branch_id = latest_branch()
        savepoint_id = latest_savepoint(branch_id)
        action = "RETRY_FROM_SCRATCH"
        command = "SUBMIT_FRESH_TASK"
        summary = "restart task from a fresh submission"

        if committed or task_phase == "2" or final_event["event_type"] == "COMMIT_TASK":
            action = "SKIP_COMMITTED"
            command = "NOOP"
            branch_id = final_event["branch_id"]
            savepoint_id = ""
            summary = "task already committed; no recovery work required"
        elif final_event["event_type"] == "ABORT_TASK" or task_phase == "3":
            action = "RETRY_FROM_SCRATCH"
            command = "SUBMIT_FRESH_TASK"
            branch_id = final_event["branch_id"]
            savepoint_id = ""
            summary = "restart task from a fresh submission"
        elif final_event["event_type"] == "COMMIT_ATTEMPT":
            action = "REVALIDATE_COMMIT"
            command = "REVALIDATE_COMMIT"
            branch_id = ""
            savepoint_id = ""
            summary = "re-run validation and conditional commit"
        elif savepoint_id:
            action = "RESUME_FROM_SAVEPOINT"
            command = "RESUME_FROM_SAVEPOINT"
            summary = "resume execution from the latest recorded savepoint"
        else:
            action = "RESUME_BRANCH"
            command = "RESUME_BRANCH"
            summary = "resume execution from the latest branch state"

        rows.append(
            {
                "task_id": log.get("task_id", ""),
                "recovery_action": action,
                "recovery_command": command,
                "target_branch_id": branch_id,
                "target_savepoint_id": savepoint_id,
                "plan_branch_count": sum(1 for event in events if event["event_type"] == "CREATE_BRANCH"),
                "event_count": len(events),
                "commit_attempts": int(log.get("commit_attempts", "0")),
                "task_phase": task_phase,
                "summary": summary,
            }
        )
    return rows


def write_csv(rows: list, output_path: Path) -> None:
    with output_path.open("w", newline="") as handle:
        writer = csv.writer(handle)
        writer.writerow(
            [
                "task_id",
                "recovery_action",
                "recovery_command",
                "target_branch_id",
                "target_savepoint_id",
                "plan_branch_count",
                "event_count",
                "commit_attempts",
                "task_phase",
                "summary",
            ]
        )
        for row in rows:
            writer.writerow(
                [
                    row["task_id"],
                    row["recovery_action"],
                    row["recovery_command"],
                    row["target_branch_id"],
                    row["target_savepoint_id"],
                    row["plan_branch_count"],
                    row["event_count"],
                    row["commit_attempts"],
                    row["task_phase"],
                    row["summary"],
                ]
            )


def main() -> None:
    if len(sys.argv) not in (2, 3):
        print("usage: task_runtime_recovery.py <task_log_dir> [output.csv]", file=sys.stderr)
        raise SystemExit(1)

    task_log_dir = Path(sys.argv[1])
    if not task_log_dir.exists():
        raise SystemExit(f"missing task log directory: {task_log_dir}")

    rows = recover_runtime(task_log_dir)
    if len(sys.argv) == 2:
        writer = csv.writer(sys.stdout)
        writer.writerow(
            [
                "task_id",
                "recovery_action",
                "recovery_command",
                "target_branch_id",
                "target_savepoint_id",
                "plan_branch_count",
                "event_count",
                "commit_attempts",
                "task_phase",
                "summary",
            ]
        )
        for row in rows:
            writer.writerow(
                [
                    row["task_id"],
                    row["recovery_action"],
                    row["recovery_command"],
                    row["target_branch_id"],
                    row["target_savepoint_id"],
                    row["plan_branch_count"],
                    row["event_count"],
                    row["commit_attempts"],
                    row["task_phase"],
                    row["summary"],
                ]
            )
        return

    write_csv(rows, Path(sys.argv[2]))


if __name__ == "__main__":
    main()
