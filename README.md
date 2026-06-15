# Data-Agent-System

Agent-driven data management system built on top of a replaceable versioned KV
storage backend.

## Directory Layout

```text
data_agent_system/
├── runtime/      # Task submission and execution-plan orchestration
├── agent_txn/    # Agent-side transaction lifecycle, validation, commit, rollback
├── branch/       # Candidate branch state and winner/loser management
├── intent/       # Semantic write intents and commit policy dispatch
├── cache/        # Read set, write buffer, object cache, savepoints
├── storage/      # Versioned KV abstraction and storage adapters
├── workloads/    # Synthetic, KV-style, and agent-task workloads
└── experiments/  # Experiment runners and result parsing scripts
```

## Current Status

The repository now includes a minimal end-to-end implementation aligned with
`docs/data_agent_system_kv_design.md`:

- `storage/` provides the `VersionedKVStore` abstraction and an in-memory
  `MemoryKVStore` with version-checked writes, plus a file-backed
  `FileKVStore` that persists versioned KV state to a local TSV-like store.
  Backend construction is now routed through `storage/store_factory.h`.
- `cache/`, `intent/`, `branch/`, and `agent_txn/` provide the first-pass data
  structures and managers for agent-side read tracking, buffered writes,
  semantic intents, winner selection, validation, commit, and rollback.
- `examples/minimal_flow_demo.cc` wires a small winner-commit flow across three
  candidate branches.
- `examples/task_runtime_workflow.cc` exercises the higher-level task runtime,
  savepoint rollback, successful winner commit, and validation-triggered abort.
- `workloads/agent_task/minimal_candidate_task.h` provides a reusable workload
  helper that drives the runtime using the agent-task abstraction.
- `workloads/kv_style/scripted_kv_task.h` provides a scripted KV-style task
  adapter for expressing multi-step branch programs with reads, writes,
  savepoints, and savepoint rollback over the unified runtime.
- `runtime/task_context.h` and `runtime/execution_plan.h` now carry richer
  task-level metadata, object references, and branch-plan descriptions so the
  runtime boundary reflects agent task semantics instead of only passing branch
  id lists.
- `runtime/task_session.h` keeps `TaskContext`, `ExecutionPlan`, and the
  agent-side transaction context together across submit, branch execution, and
  commit/abort, so task-level lifecycle state persists inside the runtime
  boundary.
- `runtime/task_event_log_io.h` exports durable `DAS_TASK_EVENT_LOG_V1`
  artifacts that capture task submission, branch-level operations, and
  commit/abort outcomes from the Agent side.
- `runtime/task_event_log_replay.h` reconstructs task-level lifecycle summaries
  from those event logs, providing a minimal replay-oriented recovery view for
  Agent-side task execution.
- `runtime/task_recovery.h` and `runtime/task_recovery_manager.h` classify
  replayed task logs into concrete recovery actions such as skipping committed
  tasks, retrying aborted tasks, or resuming from the latest savepoint.
- `runtime/task_runtime.h` can now load those task logs back into runtime-facing
  recovery plans and recovered task-session skeletons, so recovery is no longer
  limited to offline CSV analysis.
- `runtime/task_recovery_execution.h` turns recovered task sessions into
  concrete runtime next-step commands such as `NOOP`, `SUBMIT_FRESH_TASK`,
  `RESUME_BRANCH`, `RESUME_FROM_SAVEPOINT`, and `REVALIDATE_COMMIT`.
- `runtime/task_continuation_registry.h` plus the registration hooks on
  `runtime/task_runtime.h` resolve recovered sessions back to workload-specific
  continuation handlers via persisted `task.workload_name`, so post-recovery
  execution can stay workload-driven instead of being hard-coded inside demos.
- task-event logs now persist the task's input/output object references plus
  the full `TaskContext.metadata` map, so recovery handlers can rely on a
  stable task-owned input contract instead of only reconstructing state from
  branch events.
- `workloads/kv_style/scripted_kv_task.h` now also exposes a minimal helper for
  continuing recovered KV-style sessions after recovery, so resumed tasks can
  apply follow-up work and re-enter the normal commit path.
- `runtime/task_continuation.h` provides a reusable callback boundary for
  post-recovery continuation, so recovered sessions can be resumed by workload-
  specific logic without hard-coding follow-up steps into the runtime.
- `workloads/synthetic/synthetic_recovery.h` shows the intended binding model:
  workloads register their own recovery continuation handler with the runtime,
  and recovered sessions are resumed through that workload-owned logic.
- `workloads/agent_task/minimal_candidate_recovery.h` and the registration
  helper now do the same for the minimal agent-task workload, while
  `workloads/kv_style/scripted_kv_task.h` exposes a runtime registration helper
  for scripted KV continuation steps.
- `workloads/register_builtin_recovery_handlers.h` provides a small built-in
  registration layer for the workloads that currently support recovery
  continuation out of the box, including `synthetic`, `agent_task`, and the
  metadata-driven `kv_style` recovery path.
- `examples/workload_recovery_registry_demo.cc` exercises runtime-level
  recovery registration for both `agent_task` and `kv_style` workloads.
- task-event logs now persist `workload_name` and `planner_id`, so recovery can
  reconstruct the workload boundary instead of degrading every recovered task
  into a generic runtime placeholder.
- `workloads/synthetic/synthetic_driver.h` provides a small experiment driver
  that emits per-task CSV metrics for commit and abort paths.
- `examples/kv_style_workflow.cc` exercises repeated writes to the same object,
  savepoint rollback on buffered KV updates, semantic `APPEND` / `DELTA` /
  `CAS` handling, and winner commit through the agent-side transaction layer.
- `examples/multi_object_atomicity_demo.cc` makes the multi-object path
  explicit: one winner commits coordinated updates across several KV objects,
  while a conflicting run shows that none of those writes become visible when
  validation fails before the batch conditional commit.
- `examples/semantic_concurrency_demo.cc` makes the current semantic commit
  strategy explicit: `APPEND`, `DELTA`, and `CAS` are rebound against the
  latest store value at commit time, so append/delta can merge with concurrent
  target-object updates while CAS only commits if its condition still holds.
- `examples/agent_retry_loop_demo.cc` shows the agent-side retry path end to
  end: a validation failure produces a retry-from-scratch recovery decision,
  the runtime submits a fresh task instance, increments `retry_count`, and the
  workload re-enters the normal winner-commit path through the built-in
  continuation registry.
- `storage/non_batch_memory_kv_store.h` and
  `examples/non_batch_fallback_demo.cc` provide a first-pass fallback path for
  stores that do not support atomic batch conditional writes, including a demo
  that shows sequential conditional commit and compensation rollback of earlier
  writes when a later fallback write conflicts.
- `CMakeLists.txt` defines a small build target for the demo.

## Notes

- `rocksdb_adapter.h` and `redis_adapter.h` are interface stubs only. They are
  placeholders for future backend integration.
- `run_synthetic.sh` and `run_contention.sh` accept an optional backend
  argument: `memory` or `file`.
- `storage/store_factory.h` is the current backend selection boundary; `rocksdb`
  and `redis` kinds now resolve to compatibility adapters backed by the local
  file and memory implementations until real external integrations are added.
- `StoreConfig` already carries backend-specific config fields such as `path`
  `namespace_prefix`, `host`, `port`, `database_index`, and `column_family`;
  the current `RedisAdapter` uses database-index plus namespace prefixing to
  simulate logical keyspaces on top of the shared in-memory backend.
- store configuration can now be exported as a durable `DAS_STORE_CONFIG_V1`
  artifact, so experiment outputs record the backend parameters used to produce
  them.
- The current `RocksDbAdapter` compatibility layer uses a directory-style root
  with `CURRENT.tsv` and `MANIFEST.txt`, and records `column_family`, to mimic
  backend-specific layout instead of writing directly to a single flat file
  path.
- `smoke_verify_artifacts.sh` validates sample CSV output and sample file-backed
  KV persistence artifacts, including reload snapshot expectations, without
  relying on the local C++ toolchain.
- `FileKVStore` now uses a minimal journal-plus-temp-snapshot protocol so a
  restart can finish or clean up an interrupted snapshot replacement.
- synthetic workflows can also emit per-task commit-log artifacts, giving the
  Agent-side commit path a durable record outside process memory.
- sample commit logs can now be replayed into reconstructed object state, which
  provides a minimal recovery-oriented use of the Agent-side commit artifacts.
- commit-log artifacts now use an explicit `DAS_COMMIT_LOG_V1` header plus text
  escaping, so values and validation reasons are no longer implicitly limited
  to single-line plain text.
- `agent_txn/commit_log_replay.h` mirrors that replay path in C++ so future
  runtime-side recovery logic does not depend on Python-only tooling.
- `agent_txn/commit_log_recovery.h` adds a minimal C++ recovery-apply helper
  that can replay committed entries back into a `VersionedKVStore` with
  idempotent same-value handling.
- `agent_txn/recovery_manager.h` lifts that recovery path to commit-log
  directories so the Agent-side manager can drive store reconstruction from a
  durable log set.
- The current implementation is intentionally minimal. It establishes the
  transaction-upward architecture, but it does not yet include persistence,
  recovery logs, rich workload drivers, or production-grade conflict policies.
- The non-batch fallback path is intentionally first-pass only: it currently
  requires pre-existing target keys and uses in-band compensation writes rather
  than a durable two-phase protocol.

## Build

The default `/usr/local/bin/g++` on this machine is incomplete and does not
find the C++ standard library headers. Use one of the bundled toolchains under
`/opt` instead.

Recommended:

```bash
cmake --preset gcc11-release
cmake --build --preset build-gcc11-release
```

Alternative:

```bash
cmake --preset gcc15-release
cmake --build --preset build-gcc15-release
```

The presets also bake the matching `/opt/gcc-*/lib64` path into the build
RPATH, so the produced demo binaries can run without exporting
`LD_LIBRARY_PATH`.

Validated locally on this machine:

```bash
cmake -S /home/cht/Data-Agent-System \
  -B /home/cht/Data-Agent-System/build-gcc11 \
  -DCMAKE_CXX_COMPILER=/opt/gcc-11.4/bin/g++
cmake --build /home/cht/Data-Agent-System/build-gcc11 -j2
```
