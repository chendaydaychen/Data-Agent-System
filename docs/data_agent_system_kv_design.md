# 面向通用 KV 存储的 Data Agent System 设计方案

## 1. 设计定位

本方案面向一种 **agent-driven data management system**。系统不依赖传统数据库的事务语义，也不要求底层存储理解事务、分支、读写集、回滚或并发控制。底层只提供通用的版本化 KV 存储能力；任务级事务、多候选分支、读写意图、冲突验证、提交和回滚全部由 Data Agent System 在 Agent 侧完成。

核心定位可以概括为：

```text
Data Agent System = Agent 侧事务管理层 + 可替换版本化 KV 存储后端
```

底层 KV 存储可以是内存 KV、RocksDB、Redis、对象存储、LSM KV 或其他支持基础读写的存储系统。系统上层只依赖统一的 KV 接口，不绑定某一个数据库内核。

---

## 2. 为什么不依赖数据库事务语义

Data Agent System 中的数据任务通常不是一条固定执行路径，而是具有探索式执行特征。一个任务可能生成多个候选方案，每个候选方案形成一个分支，并在分支内读取对象、生成候选结果和产生潜在写入。最终只有一个 winner 分支提交，其余 loser 分支被计划性释放。

传统数据库事务模型主要管理固定读写序列：

```text
BEGIN → read/write → COMMIT 或 ABORT
```

而 Data Agent System 的执行过程更像：

```text
submit_task → candidate generation → branch execution → winner selection → commit winner
```

如果把事务语义放在数据库内部，会带来两个问题：

1. 数据库需要理解 Agent 分支语义、winner/loser 语义和候选写入语义，导致系统和具体数据库强绑定。
2. 系统难以接入其他存储后端，无法形成通用 agent-driven data management system。

因此，本方案将事务概念上移到 Agent 侧。底层 KV 只保存对象和值版本，不理解任务、分支和事务语义。

---

## 3. 系统总体架构

```text
User / Workload
      ↓
Agent / Planner
      ↓
Data Agent Runtime
      ↓
Agent Transaction Manager
      ↓
Branch Manager
      ↓
Intent Manager
      ↓
Agent-side Validation / Commit / Rollback
      ↓
Storage Adapter
      ↓
Versioned KV Store
```

系统分为两大部分。

### 3.1 Agent 侧数据管理层

Agent 侧负责：

- 接收任务目标；
- 生成候选方案；
- 创建和执行候选分支；
- 维护 Agent-level transaction；
- 维护每个分支的 read set、write buffer 和 intent log；
- 选择 winner branch；
- 对 winner branch 做冲突验证；
- 根据意图类型选择提交策略；
- 提交 winner，释放 loser；
- 支持任务级回滚、分支级回滚和操作级部分回滚。

### 3.2 底层版本化 KV 存储

底层 KV 只负责：

- 根据 key 读取 value；
- 返回 value 对应的 version；
- 根据版本条件执行写入；
- 支持批量条件写入，尽量保证多对象提交的原子性。

底层 KV 不负责：

- 事务管理；
- 分支管理；
- winner/loser 判断；
- 读写集维护；
- 语义并发控制；
- 回滚逻辑。

---

## 4. 底层 KV 存储接口

为了让 Agent 侧能够自己完成冲突验证和提交，底层 KV 至少要提供版本化读写能力。

### 4.1 最小接口

```cpp
struct VersionedValue {
    std::string value;
    uint64_t version;
};

struct VersionCheck {
    std::string key;
    uint64_t expected_version;
};

struct WriteOp {
    std::string key;
    std::string value;
};

class VersionedKVStore {
public:
    VersionedValue Get(const std::string &key);

    uint64_t GetVersion(const std::string &key);

    bool Put(const std::string &key, const std::string &value);

    bool PutIfVersion(
        const std::string &key,
        uint64_t expected_version,
        const std::string &value
    );

    bool BatchPutIfVersion(
        const std::vector<VersionCheck> &checks,
        const std::vector<WriteOp> &writes
    );
};
```

### 4.2 接口含义

| 接口 | 作用 |
|---|---|
| `Get` | 读取对象值和版本号 |
| `GetVersion` | 只读取对象当前版本号 |
| `Put` | 无条件写入，主要用于初始化或非事务写 |
| `PutIfVersion` | 当对象版本等于预期版本时写入 |
| `BatchPutIfVersion` | 批量检查版本并批量写入，用于 winner 提交 |

其中最重要的是 `PutIfVersion` 和 `BatchPutIfVersion`。如果底层不提供条件写，那么 Agent 侧验证和写入之间会出现时间窗口，可能导致并发覆盖。因此，底层 KV 不需要提供完整事务，但最好提供原子条件写能力。

---

## 5. 核心数据结构

## 5.1 Agent-level Transaction

Agent 侧用 `AgentTxnContext` 表示一次完整数据任务的事务上下文。

```cpp
struct AgentTxnContext {
    std::string txn_id;
    std::string task_id;
    TxnStatus status;

    std::vector<BranchContext> branches;
    std::string winner_branch_id;

    CommitLog commit_log;
    ValidationResult validation_result;
};
```

事务边界从任务提交开始，到 winner 提交或任务 abort 结束。

```text
submit_task → create branches → execute branches → select winner → validate → commit / abort
```

---

## 5.2 Branch Context

每个候选方案对应一个分支。

```cpp
struct BranchContext {
    std::string branch_id;
    BranchStatus status;

    ReadSet read_set;
    WriteBuffer write_buffer;
    IntentLog intent_log;
    CandidateResult candidate_result;

    std::vector<Savepoint> savepoints;
};
```

分支状态包括：

```text
CREATED
RUNNING
STAGED
WINNER
LOSER
COMMITTED
DISCARDED
ABORTED
```

---

## 5.3 Read Set

分支读取对象时，需要记录读到的版本。

```cpp
struct ReadRecord {
    std::string object_id;
    uint64_t observed_version;
};
```

示例：

```text
Branch B 读取 object:stock:8，读到版本 10
read_set = { object_id = object:stock:8, observed_version = 10 }
```

提交 winner 时，Agent 侧会检查对象当前版本是否仍然等于 `observed_version`。

---

## 5.4 Write Buffer

分支写入不直接落到底层 KV，而是先进入 Agent 侧写缓存。

```cpp
struct ObjectCacheEntry {
    std::string object_id;
    ObjectType object_type;

    std::string base_value;
    uint64_t base_version;

    std::string current_value;
    bool dirty;

    IntentType intent_type;
    UndoRecord undo_record;
};
```

对象可以是行数据、文本数据、候选结果或其他序列化对象。底层 KV 不关心对象类型，只保存 key、value 和 version。

---

## 5.5 Intent Log

写缓存记录“值变成什么”，intent log 记录“为什么这样改、以什么语义改”。

```cpp
struct WriteIntent {
    std::string object_id;
    IntentType intent_type;
    std::string payload;
    Condition condition;
};
```

第一版支持五类意图即可：

| 意图类型 | 含义 | 示例 |
|---|---|---|
| READ | 读取对象 | 读取库存、读取文本 |
| OVERWRITE | 覆盖写 | 替换结果、修改状态 |
| APPEND | 追加写 | 追加日志、追加文本片段 |
| DELTA | 增量写 | 库存扣减、计数器累加 |
| CAS | 条件写 | 状态从 pending 变为 confirmed |

---

## 6. 端到端执行流程

下面以一个通用探索式任务为例。

任务：

```text
task_001：生成多个候选方案，选择一个 winner，并写入最终结果。
```

---

### 6.1 提交任务

外部 Agent 或 workload 调用：

```text
submit_task(task_001)
```

Data Agent Runtime 创建事务上下文：

```text
txn_id = txn_001
status = RUNNING
```

此时事务对象在 Agent 侧创建，底层 KV 不知道事务存在。

---

### 6.2 生成候选分支

系统生成三个候选方案：

```text
candidate_A
candidate_B
candidate_C
```

并创建三个 branch：

```text
txn_001
  ├── Branch A
  ├── Branch B
  └── Branch C
```

---

### 6.3 分支执行与对象读取

Branch A 读取对象：

```text
value, version = store.Get(input:task_001)
```

Agent 侧记录：

```text
Branch A read_set:
  input:task_001, version = 10
```

Branch B、Branch C 同理。底层 KV 只返回值和版本，不记录分支信息。

---

### 6.4 分支写入缓存

Branch A 生成候选结果：

```text
result_A
```

写入 Agent 侧缓存：

```text
Branch A write_buffer:
  output:task_001 = result_A

Branch A intent_log:
  OVERWRITE(output:task_001, result_A)
```

Branch B 生成：

```text
output:task_001 = result_B
```

Branch C 生成：

```text
output:task_001 = result_C
```

此时底层 KV 没有任何变化。

---

### 6.5 选择 winner

系统根据候选结果选择 winner：

```text
Branch A score = 72
Branch B score = 91
Branch C score = 65

winner = Branch B
```

状态变化：

```text
Branch B → WINNER
Branch A → LOSER
Branch C → LOSER
```

loser 分支的缓存后续直接释放。

---

### 6.6 冲突验证

Agent 侧只验证 winner 分支的 read set。

```text
for read in BranchB.read_set:
    current_version = store.GetVersion(read.object_id)
    if current_version != read.observed_version:
        abort txn_001
```

如果 `input:task_001` 当前版本仍然是 10，则验证通过。若版本已经变为 11，说明 winner 分支基于过期数据生成，任务需要 abort 或重试。

---

### 6.7 语义提交策略

验证通过后，Agent 侧根据 winner 的 intent 类型选择提交策略。

- `OVERWRITE`：检查版本后覆盖写入；
- `APPEND`：读取当前值，追加内容，再条件写回；
- `DELTA`：读取当前值，重新计算增量结果，再条件写回；
- `CAS`：检查条件是否成立，成立才写入；
- `READ`：只参与版本验证，不产生写入。

对于当前例子，Branch B 的写意图是：

```text
OVERWRITE(output:task_001, result_B)
```

Agent 侧构造批量条件写：

```text
checks:
  input:task_001 version == 10

writes:
  output:task_001 = result_B
```

调用底层 KV：

```text
store.BatchPutIfVersion(checks, writes)
```

成功则任务提交；失败则任务 abort。

---

### 6.8 提交和释放

若提交成功：

```text
txn_001 → COMMITTED
Branch B → COMMITTED
Branch A → DISCARDED
Branch C → DISCARDED
```

若提交失败：

```text
txn_001 → ABORTED
所有 branch 缓存释放
底层 KV 不保留任何候选写入
```

---

## 7. 回滚设计

本系统的回滚分三类。

---

### 7.1 分支级回滚

loser 分支没有写入底层 KV，因此回滚方式是丢弃缓存：

```text
discard branch.write_buffer
discard branch.intent_log
branch.status = DISCARDED
```

这适用于 planned loser。

---

### 7.2 任务级回滚

如果 winner 验证失败或提交失败，则整个任务 abort：

```text
discard all branch buffers
discard all intent logs
txn.status = ABORTED
```

由于提交前所有写入都在 Agent 侧缓存中，任务级回滚通常不需要底层 undo。

---

### 7.3 操作级部分回滚

如果一个分支内部执行多个步骤，希望只回滚某个步骤之后的修改，可以引入保存点。

```cpp
struct Savepoint {
    std::string savepoint_id;
    size_t write_log_position;
    size_t intent_log_position;
};
```

创建保存点：

```text
savepoint_1 = current write_buffer / intent_log position
```

回滚到保存点：

```text
rollback_to(savepoint_1)
```

实现方式是恢复保存点之后被修改的缓存项，而不是操作底层 KV。

---

## 8. 冲突验证设计

本系统采用 Agent 侧验证。基本规则是：

```text
分支执行时记录 observed_version
winner 提交时读取 current_version
若 current_version != observed_version，则冲突
```

### 8.1 基础验证

适用于普通读取和覆盖写：

```text
for object in winner.read_set:
    if store.GetVersion(object) != observed_version:
        abort
```

### 8.2 写写冲突

如果 winner 要写某个对象，也需要确认该对象在提交前没有被其他任务改写。

```text
PutIfVersion(object_id, expected_version, new_value)
```

如果版本不匹配，则写入失败，任务 abort 或重试。

### 8.3 语义验证

不同 intent 可以放宽或调整验证规则。

| 意图类型 | 验证策略 |
|---|---|
| READ | 检查读版本是否未变 |
| OVERWRITE | 检查读版本和写版本，保守提交 |
| APPEND | 可重读当前值后追加，失败可重试 |
| DELTA | 可基于当前值重新计算增量结果 |
| CAS | 检查条件仍然成立 |

---

## 9. 对象缓存设计

### 9.1 行数据

行数据可以序列化成一个对象：

```text
key = row:inventory:store_1:item_8
value = { quantity: 100, price: 20 }
version = 10
```

分支内修改：

```text
base_value = { quantity: 100, price: 20 }
current_value = { quantity: 95, price: 20 }
intent = DELTA(-5)
```

### 9.2 文本数据

文本也作为 KV 对象：

```text
key = text:summary:task_001
value = "原始摘要"
version = 4
```

分支内修改：

```text
current_value = "新的摘要"
intent = OVERWRITE
```

追加文本：

```text
intent = APPEND("新增片段")
```

底层 KV 不理解行数据或文本数据。对象解释、修改语义和冲突策略由 Agent 侧负责。

---

## 10. 与传统事务引擎的区别

| 维度 | 传统事务引擎 | 本系统 |
|---|---|---|
| 事务位置 | 数据库内部 | Agent 侧 |
| 输入 | 固定读写逻辑 | 任务目标和候选方案 |
| 执行路径 | 单一路径 | 多候选分支 |
| 读写集维护 | DB 维护 | Agent 侧维护 |
| 并发控制 | DB 内部完成 | Agent 侧完成 |
| 回滚 | DB undo / abort | 丢弃缓存 / 保存点 / 批量条件写失败回滚 |
| 底层存储 | 事务数据库 | 可替换版本化 KV |
| winner/loser | 无 | 显式管理 |

本系统不是把 Agent 接到数据库事务上，而是在 Agent 侧实现数据管理能力。底层 KV 只是存储原语提供者。

---

## 11. 工程模块建议

推荐目录结构如下：

```text
data_agent_system/
├── runtime/
│   ├── task_runtime.h
│   ├── task_context.h
│   └── execution_plan.h
│
├── agent_txn/
│   ├── agent_txn_context.h
│   ├── agent_txn_manager.h
│   ├── validation_manager.h
│   ├── commit_manager.h
│   └── rollback_manager.h
│
├── branch/
│   ├── branch_context.h
│   ├── branch_manager.h
│   └── branch_result.h
│
├── intent/
│   ├── intent.h
│   ├── intent_type.h
│   ├── intent_log.h
│   └── policy_dispatcher.h
│
├── cache/
│   ├── object_cache.h
│   ├── write_buffer.h
│   ├── read_set.h
│   └── savepoint.h
│
├── storage/
│   ├── versioned_kv_store.h
│   ├── memory_kv_store.h
│   ├── rocksdb_adapter.h
│   └── redis_adapter.h
│
├── workloads/
│   ├── synthetic/
│   ├── kv_style/
│   └── agent_task/
│
└── experiments/
    ├── run_synthetic.sh
    ├── run_contention.sh
    └── parse_results.py
```

---

## 12. 最小可运行版本

第一版不需要接复杂 workload，只需要完成一个最小闭环。

### 12.1 必要组件

```text
VersionedKVStore
AgentTxnContext
BranchContext
ReadSet
WriteBuffer
IntentLog
WinnerSelect
ValidationManager
CommitManager
RollbackManager
```

### 12.2 最小流程

```text
submit_task
  ↓
create AgentTxnContext
  ↓
generate 3 branches
  ↓
branch get object from KV
  ↓
record read version
  ↓
write to branch buffer
  ↓
select winner
  ↓
validate winner read set
  ↓
batch_put_if_version winner writes
  ↓
commit or abort
```

### 12.3 最小实验指标

```text
task_count
branch_count
winner_commit_count
planned_loser_count
real_abort_count
conflict_abort_count
validation_fail_count
commit_latency
throughput
retry_count
```

---

## 13. 后续研究方向

### 13.1 更多工作负载

从 synthetic workload 扩展到更接近真实 Data Agent 的探索式事务负载，覆盖：

- 多候选方案选择；
- 资源更新；
- 状态迁移；
- 文本对象修改；
- 多对象联合提交；
- 高并发冲突场景。

### 13.2 自动 intent 识别

当前 intent 类型可以先由 workload adapter 标注。后续可以研究自动识别：

```text
覆盖写 → OVERWRITE
追加写 → APPEND
数值增减 → DELTA
条件状态迁移 → CAS
只读访问 → READ
```

自动识别可以基于规则、代码模板、任务描述或 LLM 辅助分析。

### 13.3 更强的提交原子性

如果底层 KV 不支持 `BatchPutIfVersion`，需要在 Agent 侧实现：

- 提交日志；
- undo log；
- 两阶段写入；
- 失败补偿；
- 幂等恢复。

第一版建议优先假设底层支持批量条件写，以降低工程复杂度。

---

## 14. 一句话总结

本方案将 Data Agent System 设计为一个 Agent 侧的数据管理系统。事务语义、分支管理、读写集、语义意图、冲突验证、提交和回滚全部由 Agent 侧维护；底层存储只提供版本化 KV 原语。这样系统不再依赖具体数据库的事务能力，可以接入任意支持版本化读写的存储后端，并更自然地表达 Agent 探索式任务中的 candidate branch、winner commit 和 loser discard 语义。
