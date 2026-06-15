#!/usr/bin/env python3
import csv
import statistics
import sys
from pathlib import Path


def main() -> None:
    if len(sys.argv) != 2:
        print("usage: parse_results.py <synthetic_results.csv>", file=sys.stderr)
        raise SystemExit(1)

    csv_path = Path(sys.argv[1])
    if not csv_path.exists():
        print(f"missing results file: {csv_path}", file=sys.stderr)
        raise SystemExit(1)

    rows = []
    with csv_path.open("r", newline="") as handle:
        reader = csv.DictReader(handle)
        for row in reader:
            rows.append(row)

    if not rows:
        print("task_count,committed_task_count,aborted_task_count,avg_commit_latency_us")
        print("0,0,0,0.0")
        return

    committed = sum(int(row["committed"]) for row in rows)
    aborted = len(rows) - committed
    latencies = [float(row["commit_latency_us"]) for row in rows]
    avg_latency = statistics.mean(latencies)
    total_latency = sum(latencies)
    conflicts = sum(int(row["conflict_abort_count"]) for row in rows)
    validation_failures = sum(int(row["validation_fail_count"]) for row in rows)
    planned_losers = sum(int(row["planned_loser_count"]) for row in rows)
    throughput = 0.0 if total_latency == 0.0 else (len(rows) * 1_000_000.0 / total_latency)

    print(
        "task_count,committed_task_count,aborted_task_count,conflict_abort_count,"
        "validation_fail_count,planned_loser_count,avg_commit_latency_us,throughput_txn_per_s"
    )
    print(
        f"{len(rows)},{committed},{aborted},{conflicts},{validation_failures},"
        f"{planned_losers},{avg_latency:.2f},{throughput:.2f}"
    )


if __name__ == "__main__":
    main()
