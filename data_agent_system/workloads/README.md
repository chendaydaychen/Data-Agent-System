# workloads

Workload adapters for exercising the runtime under different access patterns.

- `synthetic/`: minimal synthetic tasks and experiment drivers.
- `kv_style/`: KV-oriented workloads that interact directly with object keys and values.
  Includes a scripted task adapter for driving multi-step branch programs over
  the runtime.
- `agent_task/`: higher-level task-style workloads with candidate branches.
