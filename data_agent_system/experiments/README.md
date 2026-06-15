# experiments

Shell and Python helpers for running synthetic experiments, parsing outputs, and
verifying storage, commit-log, and recovery artifacts.

## End-to-End Closed Loop

`run_e2e_closed_loop.sh` runs the smallest reproducible closed-loop experiment:

1. build `agent_retry_loop_demo` and `synthetic_experiment`;
2. run a single retry-oriented Agent task that aborts once, recovers from the
   task event log, and commits on retry;
3. run a small synthetic workload that emits CSV metrics, commit logs, task
   event logs, a file-backed KV snapshot, and store config;
4. verify the generated artifacts and derive task/runtime recovery plans.

The default output path is timestamped under `output/e2e/`. To target the
remote node3 toolchain, leave the default auto-detection in place; if
`/opt/gcc-11.4/bin/g++` exists the script uses the `gcc11-release` CMake preset.
For local WSL development without that toolchain, it falls back to
`build-local-wsl`.
