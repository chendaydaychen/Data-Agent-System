# data_agent_system

Core implementation modules for the agent-driven data management system.

## Modules

- `runtime/`: task submission, execution plan, and runtime orchestration.
- `agent_txn/`: agent-side transaction context, validation, commit, rollback, and recovery.
- `branch/`: candidate branch lifecycle and winner/loser management.
- `intent/`: semantic write intents and commit policy dispatch.
- `cache/`: read sets, write buffers, object cache entries, and savepoints.
- `storage/`: versioned KV abstractions and backend adapters.
- `workloads/`: workload adapters and synthetic drivers.
- `experiments/`: experiment runners, parsers, and verification scripts.
