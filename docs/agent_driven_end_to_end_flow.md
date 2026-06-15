# Data Agent System 端到端流程与设计说明

本文档从端到端视角说明当前仓库中的系统实现，重点回答三个问题：

1. Agent 侧和底层存储侧的系统边界是什么？
2. 一个任务从提交到提交成功、失败回滚、崩溃恢复，完整流程是什么？
3. 现在已经实现并验证了哪些关键环节？

## 1. 系统定位

这个系统的核心设计选择是：

```text
事务语义放在 Agent 侧
底层数据库只提供可替换的版本化 KV 能力
```

也就是说：

- 底层存储不理解 task、branch、winner、loser、semantic intent、savepoint、retry policy
- Agent 侧负责任务级事务、候选分支、读写集、冲突验证、提交决策、回滚与恢复
- 底层只暴露版本化 KV 接口

因此，我们把底层数据库抽象成：

```text
Versioned KV backend = get + getVersion + put + putIfVersion + 可选 batch conditional write
```

当前这套边界可以落在：

- 内存 KV
- 文件型 KV
- RocksDB/Redis 兼容层
- 后续任何支持版本校验写的 KV 系统

## 2. 总体架构

当前系统的主要调用链是：

```text
Workload / Task
  -> TaskRuntime
  -> AgentTxnManager
  -> BranchManager
  -> ValidationManager + CommitManager + RollbackManager
  -> Storage Adapter
  -> VersionedKVStore
```

关键代码位置：

- 运行时入口：
`[runtime/task_runtime.h](../data_agent_system/runtime/task_runtime.h)`
- Agent 事务生命周期：
`[agent_txn/agent_txn_manager.h](../data_agent_system/agent_txn/agent_txn_manager.h)`
- 分支管理：
`[branch/branch_manager.h](../data_agent_system/branch/branch_manager.h)`
- 语义写意图策略：
`[intent/policy_dispatcher.h](../data_agent_system/intent/policy_dispatcher.h)`
- 存储边界：
`[storage/versioned_kv_store.h](../data_agent_system/storage/versioned_kv_store.h)`

## 3. 底层存储边界

底层存储接口刻意保持很小。

当前
`[storage/versioned_kv_store.h](../data_agent_system/storage/versioned_kv_store.h)`
中的核心接口包括：

- `Get(key)`
- `GetVersion(key)`
- `Put(key, value)`
- `PutIfVersion(key, expected_version, value)`
- `DeleteIfVersion(key, expected_version)`
- `BatchPutIfVersion(checks, writes)`

这组接口已经足够支持 Agent 侧实现：

- 读集验证
- 写写冲突检测
- 支持 batch 的原子提交
- 不支持 batch 时的 fallback commit
- 新创建 key 的回滚删除

底层数据库并不负责事务，只负责“基于版本的 key-value 读写约束”。

## 4. Agent 侧核心对象

### 4.1 TaskContext

`TaskContext` 表示一个用户可见任务，包含：

- task id
- objective
- workload name
- planner id
- 输入/输出对象引用
- 恢复和 continuation 所需 metadata

对应代码：
`[runtime/task_context.h](../data_agent_system/runtime/task_context.h)`

### 4.2 AgentTxnContext

`AgentTxnContext` 是 Agent 侧持有的任务级事务上下文，包含：

- `txn_id`
- `task_id`
- 事务状态
- branch 列表
- winner branch id
- commit log
- validation result
- metrics
- fallback commit 配置

对应代码：
`[agent_txn/agent_txn_context.h](../data_agent_system/agent_txn/agent_txn_context.h)`

### 4.3 BranchContext

每个候选方案对应一个 `BranchContext`，内部保存：

- branch 状态
- read set
- write buffer
- intent log
- candidate result
- savepoints

对应代码：
`[branch/branch_context.h](../data_agent_system/branch/branch_context.h)`

### 4.4 ReadSet 与 WriteBuffer

`ReadSet` 记录：

- object id
- observed version

`WriteBuffer` / `ObjectCacheEntry` 记录：

- object id 和 object type
- base value / base version
- 当前分支缓冲值
- dirty 标记
- 最新 intent type
- undo 信息

对应代码：

- `[cache/read_set.h](../data_agent_system/cache/read_set.h)`
- `[cache/object_cache.h](../data_agent_system/cache/object_cache.h)`
- `[cache/write_buffer.h](../data_agent_system/cache/write_buffer.h)`

### 4.5 IntentLog

IntentLog 记录的不是“最后写成什么值”，而是“这次写的逻辑语义是什么”。

当前 intent 类型包括：

- `kRead`
- `kOverwrite`
- `kAppend`
- `kDelta`
- `kCas`

对应代码：

- `[intent/intent.h](../data_agent_system/intent/intent.h)`
- `[intent/intent_type.h](../data_agent_system/intent/intent_type.h)`
- `[intent/intent_log.h](../data_agent_system/intent/intent_log.h)`

## 5. 端到端执行流程

正常成功路径可以概括为：

```text
submit task
  -> create agent transaction
  -> create candidate branches
  -> execute branch-local reads / buffered writes
  -> stage branch results
  -> select winner
  -> validate winner
  -> resolve write intents
  -> commit winner to versioned KV
  -> discard losers
  -> persist task / commit / fallback artifacts
```

下面按阶段展开说明。

### 5.1 任务提交

运行时在提交任务时会做这些事：

- 校验 `TaskContext`
- 校验 `ExecutionPlan`
- 创建 `AgentTxnContext`
- 为每个候选分支创建 `BranchContext`
- 记录初始 task event

入口代码：
`[runtime/task_runtime.h](../data_agent_system/runtime/task_runtime.h)`

验证示例：

- `[examples/minimal_flow_demo.cc](../examples/minimal_flow_demo.cc)`
- `[examples/task_runtime_workflow.cc](../examples/task_runtime_workflow.cc)`

### 5.2 分支执行

每个 branch 都运行在 Agent 侧缓冲状态上，而不是直接写底层存储。

读：

- 如果对象已经在 branch 的 write buffer 中，就读缓冲值
- 否则从底层存储读取，并把版本记录到 read set

写：

- 不直接落到底层 KV
- 先更新 branch 的 write buffer
- 再把逻辑 intent 追加到 intent log

savepoint：

- 记录当前 write buffer 和 intent log 的位置
- 支持 branch 内局部回滚，不必直接 abort 整个 task

对应实现：

- `[branch/branch_manager.h](../data_agent_system/branch/branch_manager.h)`
- `[runtime/task_runtime.h](../data_agent_system/runtime/task_runtime.h)`

验证示例：

- `[examples/task_runtime_workflow.cc](../examples/task_runtime_workflow.cc)`
- `[examples/kv_style_workflow.cc](../examples/kv_style_workflow.cc)`

### 5.3 winner 选择

所有 branch 执行完后：

- 每个 branch 会带着 score 和 summary 进入 staged 状态
- Agent 侧从中选出一个 winner
- 其余 branch 标成 loser

这个阶段完全由 Agent 侧完成，底层存储对 winner/loser 一无所知。

对应实现：
`[branch/branch_manager.h](../data_agent_system/branch/branch_manager.h)`

### 5.4 winner 验证

winner 在进入提交前会做验证。

当前验证逻辑是分层的：

- 严格语义的写，例如 `OVERWRITE`，要求先前读到的版本没有变化
- 语义可重绑定的写，例如 `APPEND`、`DELTA`、`CAS`，可以绕过严格读集校验，在 commit 时再结合最新值做语义判断

对应实现：

- `[agent_txn/validation_manager.h](../data_agent_system/agent_txn/validation_manager.h)`
- `[intent/policy_dispatcher.h](../data_agent_system/intent/policy_dispatcher.h)`

### 5.5 intent 解析与语义并发控制

系统在提交时不会直接把 write buffer 生硬刷到底层，而是先根据最新 intent 解释“这次写真正的语义”。

当前策略分为几类：

- `OVERWRITE`
  - `strict`
  - 只有预期版本仍匹配时才能提交
- `APPEND`
  - `commutative_rebase`
  - 将 branch 追加内容拼接到最新 store 值上
- `DELTA`
  - `commutative_rebase`
  - 在最新 store 值上重新应用数值增量
- `CAS`
  - `conditional_rebase`
  - 只有条件在最新 store 值上仍成立时才提交

这部分逻辑集中在：
`[intent/policy_dispatcher.h](../data_agent_system/intent/policy_dispatcher.h)`

这一步是系统区别于“纯 OCC over KV”的关键：Agent 侧不是只看版本冲突，而是在做语义感知并发控制。

### 5.6 提交路径 A：支持 batch 条件写的后端

如果底层后端支持 `BatchPutIfVersion`：

- Agent 侧从 winner 的读写信息中整理版本检查条件
- 从 resolved intent 中生成最终写入值
- 一次性执行 `BatchPutIfVersion`

如果成功：

- winner 提交成功
- loser 被丢弃
- transaction 标记为 committed

如果失败：

- 任务 abort
- 不会出现部分可见写入

对应实现：
`[agent_txn/commit_manager.h](../data_agent_system/agent_txn/commit_manager.h)`

验证示例：
`[examples/multi_object_atomicity_demo.cc](../examples/multi_object_atomicity_demo.cc)`

### 5.7 提交路径 B：不支持 batch 的 fallback commit

如果底层后端不支持 `BatchPutIfVersion`：

- runtime 会为事务分配 fallback artifact 路径
- Agent 侧先写 durable fallback artifact
- 然后逐条用 `PutIfVersion` 顺序提交
- 之后恢复逻辑可以决定：
  - 前滚剩余写入，走到 `COMMITTED`
  - 或回滚已经写入的部分，走到 `ROLLED_BACK`

fallback artifact 记录：

- transaction id
- task id
- fallback phase
- 已应用写入数量
- 每个 key 的 expected version
- target value
- previous value / previous version
- key 之前是否存在

对应实现：

- `[agent_txn/fallback_commit_log.h](../data_agent_system/agent_txn/fallback_commit_log.h)`
- `[agent_txn/fallback_commit_recovery.h](../data_agent_system/agent_txn/fallback_commit_recovery.h)`
- `[runtime/task_runtime.h](../data_agent_system/runtime/task_runtime.h)`

验证示例：

- `[examples/non_batch_fallback_demo.cc](../examples/non_batch_fallback_demo.cc)`
- `[examples/fallback_crash_safe_demo.cc](../examples/fallback_crash_safe_demo.cc)`

## 6. 回滚与恢复

当前系统在三个层次支持回滚/恢复。

### 6.1 savepoint 层局部回滚

在 branch 内部：

- 创建 savepoint
- 继续 speculative writes
- 根据 savepoint 回滚 write buffer 和 intent log

这一步完全发生在 Agent 侧缓冲区内，不涉及底层存储。

### 6.2 task abort

如果验证失败或 commit 失败：

- transaction 标记 aborted
- winner branch 标记 aborted
- loser branches 标记 discarded
- 对支持 batch 的 backend，不会留下部分可见写

### 6.3 fallback 崩溃恢复

如果 non-batch fallback commit 中途崩溃：

- `TaskRuntime` 启动时可以自动扫描 fallback artifact 目录
- 遇到非终态 artifact，会自动恢复
- 恢复逻辑会根据当前 key 状态和 fallback phase 决定：
  - 前滚完成剩余写，进入 `COMMITTED`
  - 或回滚已写入部分，进入 `ROLLED_BACK`
- 终态 artifact 可按策略 keep / delete / archive

这里的恢复是“本地 artifact 驱动恢复”，不是分布式共识协议。

## 7. task event log 与 commit log

当前系统会把两类 durable 执行视图落盘。

### 7.1 task event log

task event log 记录：

- submit
- create branch
- read
- write
- savepoint
- rollback to savepoint
- stage
- commit attempt
- commit task
- abort task

作用：

- 重建 runtime 视角下的任务历史
- 推导 recovery decision
- 支持任务恢复和 continuation

对应实现：

- `[runtime/task_event_log_io.h](../data_agent_system/runtime/task_event_log_io.h)`
- `[runtime/task_event_log_replay.h](../data_agent_system/runtime/task_event_log_replay.h)`
- `[runtime/task_recovery.h](../data_agent_system/runtime/task_recovery.h)`

### 7.2 commit log

commit log 记录 committed writes：

- key
- expected version
- committed value

作用：

- 重建 committed store state
- 支持 replay 和 recovery

对应实现：

- `[agent_txn/commit_log_io.h](../data_agent_system/agent_txn/commit_log_io.h)`
- `[agent_txn/commit_log_replay.h](../data_agent_system/agent_txn/commit_log_replay.h)`
- `[agent_txn/commit_log_recovery.h](../data_agent_system/agent_txn/commit_log_recovery.h)`

## 8. 已经验证了哪些环节

当前仓库中，大部分关键设计都有可执行验证。

### 8.1 基本任务流

验证文件：

- `[examples/minimal_flow_demo.cc](../examples/minimal_flow_demo.cc)`
- `[examples/task_runtime_workflow.cc](../examples/task_runtime_workflow.cc)`

验证内容：

- task submission
- branch creation
- read / write
- winner selection
- commit / abort

### 8.2 savepoint 与局部回滚

验证文件：

- `[examples/task_runtime_workflow.cc](../examples/task_runtime_workflow.cc)`
- `[examples/kv_style_workflow.cc](../examples/kv_style_workflow.cc)`

验证内容：

- branch-local savepoint
- rollback to savepoint
- commit 前缓冲状态修正

### 8.3 多对象原子提交

验证文件：
`[examples/multi_object_atomicity_demo.cc](../examples/multi_object_atomicity_demo.cc)`

验证内容：

- 多 key winner commit
- 支持 batch 的 backend 上表现为 all-or-nothing

### 8.4 语义感知并发控制

验证文件：
`[examples/semantic_concurrency_demo.cc](../examples/semantic_concurrency_demo.cc)`

验证内容：

- `APPEND` 可和并发目标更新合并
- `DELTA` 可在并发目标更新后 rebasing
- `CAS` 只有条件仍成立时才成功

### 8.5 不同 intent class 下的冲突行为

验证文件：
`[examples/semantic_contention_demo.cc](../examples/semantic_contention_demo.cc)`

验证内容：

- 严格 `OVERWRITE` 在重复版本漂移下持续 abort
- `DELTA` 在同样冲突模式下持续 commit

这是当前最直接说明“语义并发控制确实带来行为差异”的实验。

### 8.6 fallback commit 与 crash recovery

验证文件：

- `[examples/non_batch_fallback_demo.cc](../examples/non_batch_fallback_demo.cc)`
- `[examples/fallback_crash_safe_demo.cc](../examples/fallback_crash_safe_demo.cc)`

验证内容：

- non-batch commit 可以成功
- 后续 key 冲突时能回滚已写入部分
- 新创建 key 可以在回滚时删掉
- 启动恢复时可以前滚或归档终态 artifact

### 8.7 retry 与 continuation recovery

验证文件：

- `[examples/agent_retry_loop_demo.cc](../examples/agent_retry_loop_demo.cc)`
- `[examples/task_runtime_recovery_demo.cc](../examples/task_runtime_recovery_demo.cc)`
- `[examples/task_runtime_recovery_continue_demo.cc](../examples/task_runtime_recovery_continue_demo.cc)`
- `[examples/workload_recovery_registry_demo.cc](../examples/workload_recovery_registry_demo.cc)`

验证内容：

- 验证失败后的 retry-from-scratch
- task log 到 recovery plan 的构造
- workload-specific continuation

## 9. 当前系统已经具备的能力

到目前为止，这个系统已经不是单纯设计草图，而是一个结构完整的原型：

- 事务语义已经上移到 Agent 侧
- 底层存储已经被压缩成可替换 versioned KV 接口
- winner/loser branch 执行模型已经跑通
- 语义感知并发控制已经有集中策略和实证 demo
- 支持 batch 的后端可以做多对象原子提交
- 不支持 batch 的后端也能走 fallback commit 和崩溃恢复
- runtime 已经能接管 task 日志、恢复、continuation 和 artifact 管理

## 10. 当前限制

当前实现仍然是一个“有核心机制、有验证”的 first-pass prototype，不是最终论文系统。

主要限制包括：

- 真实外部 RocksDB / Redis 集成还没有接上
- fallback commit 虽然 durable 且可恢复，但仍不是完整分布式事务协议
- workload 仍偏 demo 化，还不是标准 benchmark 套件
- 语义并发策略目前仍然是紧凑的一版，不是完整冲突代数
- 缺少与强 baseline 的系统化对比实验

## 11. 建议的下一步工作

下一阶段不应该无目标地继续堆机制，而应该转向“论文系统化”。

建议优先级：

1. 明确论文主张
  先确定主贡献到底是：
  - transaction-upward architecture
  - semantic-aware concurrency over versioned KV
  - 或两者的组合
2. 接入至少一个真实外部 backend
  用实证证明底层存储边界确实可替换
3. 建立 baseline 与 benchmark
  对比 strict OCC、传统 locking/OCC、以及当前 semantic Agent-side policy
4. 扩展 contention / recovery workload
  度量吞吐、abort rate、retry cost、recovery latency、semantic merge rate
5. 收紧文档结构
  这份文档负责端到端总览，设计文档负责架构定义，audit 文档负责实现证据

