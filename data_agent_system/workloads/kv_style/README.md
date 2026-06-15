# kv_style

Reserved for workloads that model direct versioned-KV access patterns instead of
full candidate-branch task execution.

Current contents:

- `scripted_kv_task.h`: a lightweight scripted workload adapter that executes
  branch-local read/write/savepoint/rollback steps through `TaskRuntime`.

Typical future contents:

- single-key and multi-key read/write flows
- conditional-write conflict cases
- backend-focused microbenchmarks
