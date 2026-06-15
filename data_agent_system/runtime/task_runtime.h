#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>

#include "data_agent_system/agent_txn/agent_txn_manager.h"
#include "data_agent_system/agent_txn/rollback_manager.h"
#include "data_agent_system/branch/branch_manager.h"
#include "data_agent_system/runtime/execution_plan.h"
#include "data_agent_system/runtime/task_continuation_registry.h"
#include "data_agent_system/runtime/task_context.h"
#include "data_agent_system/runtime/task_recovery.h"
#include "data_agent_system/runtime/task_recovery_execution.h"
#include "data_agent_system/runtime/task_recovery_manager.h"
#include "data_agent_system/runtime/task_session.h"
#include "data_agent_system/storage/versioned_kv_store.h"

namespace data_agent_system::runtime {

class TaskRuntime {
 public:
  explicit TaskRuntime(data_agent_system::storage::VersionedKVStore& store) : store_(store) {}

  bool RegisterContinuationHandler(const std::string& workload_name,
                                   const TaskContinuationHandler& handler) {
    return continuation_registry_.RegisterHandler(workload_name, handler);
  }

  const TaskContinuationHandler* FindContinuationHandler(const std::string& workload_name) const {
    return continuation_registry_.FindHandler(workload_name);
  }

  bool ContinueRecoveredTask(TaskRecoveryExecution* execution) {
    return continuation_registry_.Continue(*this, execution);
  }

  TaskSession SubmitTask(
      const TaskContext& task,
      const ExecutionPlan& plan) const {
    task.Validate();
    plan.Validate();
    TaskSession session;
    session.task = task;
    session.plan = plan;
    session.txn = txn_manager_.Begin("txn:" + task.task_id, task.task_id);
    session.RecordEvent(TaskEventType::kSubmitTask, std::string(), std::string(), task.objective);
    for (const auto& branch_plan : plan.branch_plans) {
      txn_manager_.CreateBranch(session.txn, branch_plan.branch_id);
      session.RecordEvent(TaskEventType::kCreateBranch, branch_plan.branch_id, std::string(),
                          branch_plan.candidate_id, static_cast<std::int64_t>(branch_plan.initial_priority));
    }
    session.MarkRunning();
    return session;
  }

  data_agent_system::branch::BranchContext* FindBranch(TaskSession& session,
                                                       const std::string& branch_id) const {
    return FindBranch(session.txn, branch_id);
  }

  data_agent_system::branch::BranchContext* FindBranch(
      data_agent_system::agent_txn::AgentTxnContext& txn,
      const std::string& branch_id) const {
    return branch_manager_.FindBranch(txn.branches, branch_id);
  }

  data_agent_system::branch::ReadResult Read(TaskSession& session,
                                             const std::string& branch_id,
                                             const std::string& object_id) const {
    const auto result = Read(session.txn, branch_id, object_id);
    session.RecordEvent(TaskEventType::kRead, branch_id, object_id, result.value,
                        static_cast<std::int64_t>(result.version));
    return result;
  }

  data_agent_system::branch::ReadResult Read(
      data_agent_system::agent_txn::AgentTxnContext& txn,
      const std::string& branch_id,
      const std::string& object_id) const {
    auto* branch = FindBranch(txn, branch_id);
    if (branch == nullptr) {
      return {};
    }
    return branch_manager_.ReadObject(*branch, object_id, store_);
  }

  void Write(TaskSession& session,
             const std::string& branch_id,
             const std::string& object_id,
             data_agent_system::cache::ObjectType object_type,
             const data_agent_system::intent::WriteIntent& intent) const {
    Write(session.txn, branch_id, object_id, object_type, intent);
    session.RecordEvent(TaskEventType::kWrite, branch_id, object_id, intent.payload,
                        static_cast<std::int64_t>(intent.intent_type));
  }

  void Write(data_agent_system::agent_txn::AgentTxnContext& txn,
             const std::string& branch_id,
             const std::string& object_id,
             data_agent_system::cache::ObjectType object_type,
             const data_agent_system::intent::WriteIntent& intent) const {
    auto* branch = FindBranch(txn, branch_id);
    if (branch == nullptr) {
      return;
    }
    branch_manager_.BufferWrite(*branch, object_id, object_type, intent, store_);
  }

  void Savepoint(TaskSession& session,
                 const std::string& branch_id,
                 const std::string& savepoint_id) const {
    Savepoint(session.txn, branch_id, savepoint_id);
    session.RecordEvent(TaskEventType::kSavepoint, branch_id, std::string(), savepoint_id);
  }

  void Savepoint(data_agent_system::agent_txn::AgentTxnContext& txn,
                 const std::string& branch_id,
                 const std::string& savepoint_id) const {
    auto* branch = FindBranch(txn, branch_id);
    if (branch == nullptr) {
      return;
    }
    branch_manager_.CreateSavepoint(*branch, savepoint_id);
  }

  bool RollbackToSavepoint(TaskSession& session,
                           const std::string& branch_id,
                           const std::string& savepoint_id) const {
    const bool rolled_back = RollbackToSavepoint(session.txn, branch_id, savepoint_id);
    if (rolled_back) {
      session.RecordEvent(TaskEventType::kRollbackToSavepoint, branch_id, std::string(),
                          savepoint_id);
    }
    return rolled_back;
  }

  bool RollbackToSavepoint(data_agent_system::agent_txn::AgentTxnContext& txn,
                           const std::string& branch_id,
                           const std::string& savepoint_id) const {
    auto* branch = FindBranch(txn, branch_id);
    if (branch == nullptr) {
      return false;
    }
    return rollback_manager_.RollbackToSavepoint(*branch, savepoint_id);
  }

  void StageBranch(TaskSession& session,
                   const std::string& branch_id,
                   double score,
                   const std::string& summary) const {
    StageBranch(session.txn, branch_id, score, summary);
    session.RecordEvent(TaskEventType::kStageBranch, branch_id, std::string(), summary,
                        static_cast<std::int64_t>(score));
  }

  void StageBranch(data_agent_system::agent_txn::AgentTxnContext& txn,
                   const std::string& branch_id,
                   double score,
                   const std::string& summary) const {
    auto* branch = FindBranch(txn, branch_id);
    if (branch == nullptr) {
      return;
    }
    branch_manager_.StageBranch(*branch, score, summary);
  }

  bool CommitTask(TaskSession& session) const {
    session.commit_attempts += 1;
    session.RecordEvent(TaskEventType::kCommitAttempt, std::string(), std::string(),
                        "commit_attempt", static_cast<std::int64_t>(session.commit_attempts));
    const bool committed = CommitTask(session.txn);
    if (committed) {
      session.MarkCommitted();
      session.RecordEvent(TaskEventType::kCommitTask, session.txn.winner_branch_id,
                          std::string(), session.txn.validation_result.reason);
    } else {
      session.MarkAborted();
      session.RecordEvent(TaskEventType::kAbortTask, session.txn.winner_branch_id,
                          std::string(), session.txn.validation_result.reason);
    }
    return committed;
  }

  bool CommitTask(data_agent_system::agent_txn::AgentTxnContext& txn) const {
    return txn_manager_.SelectWinnerAndCommit(txn, store_);
  }

  void AbortTask(TaskSession& session) const {
    AbortTask(session.txn);
    session.MarkAborted();
    session.RecordEvent(TaskEventType::kAbortTask, std::string(), std::string(),
                        "abort_requested");
  }

  void AbortTask(data_agent_system::agent_txn::AgentTxnContext& txn) const {
    rollback_manager_.AbortTransaction(txn);
  }

  TaskRecoveryPlan LoadRecoveryPlanFromLog(const std::string& task_log_path) const {
    ParsedTaskEventLogArtifact artifact;
    if (!ParseTaskEventLogArtifact(task_log_path, &artifact)) {
      throw std::runtime_error("failed to parse task event log: " + task_log_path);
    }
    return BuildTaskRecoveryPlan(task_log_path, artifact);
  }

  RecoveredTaskSession RecoverSessionFromLog(const std::string& task_log_path) const {
    ParsedTaskEventLogArtifact artifact;
    if (!ParseTaskEventLogArtifact(task_log_path, &artifact)) {
      throw std::runtime_error("failed to parse task event log: " + task_log_path);
    }
    auto recovered = RecoverTaskSessionFromArtifact(task_log_path, artifact);
    RebuildRecoveredSessionState(&recovered.session, artifact);
    ApplyRecoveredTerminalState(&recovered);
    return recovered;
  }

  std::vector<RecoveredTaskSession> RecoverSessionsFromDirectory(
      const std::string& task_log_dir) const {
    std::vector<RecoveredTaskSession> recovered_sessions;
    for (const auto& path : task_recovery_manager_.ListTaskLogs(task_log_dir)) {
      recovered_sessions.push_back(RecoverSessionFromLog(path));
    }
    return recovered_sessions;
  }

  TaskRecoveryExecution BuildRecoveryExecutionFromLog(const std::string& task_log_path) const {
    return BuildTaskRecoveryExecution(RecoverSessionFromLog(task_log_path));
  }

  std::vector<TaskRecoveryExecution> BuildRecoveryExecutionsFromDirectory(
      const std::string& task_log_dir) const {
    return BuildTaskRecoveryExecutions(RecoverSessionsFromDirectory(task_log_dir));
  }

  bool ExecuteRecoveryExecution(TaskRecoveryExecution* execution) const {
    if (execution == nullptr) {
      return false;
    }

    switch (execution->command_type) {
      case TaskRecoveryCommandType::kNoop:
        execution->session.task.SetMetadata("recovery_execution_status", "noop");
        return true;
      case TaskRecoveryCommandType::kSubmitFreshTask: {
        const std::size_t previous_retry_count = execution->session.txn.metrics.retry_count;
        auto fresh_task = execution->recovery_plan.task;
        fresh_task.phase = TaskPhase::kCreated;
        fresh_task.SetMetadata("recovery_execution_status", "fresh_submit");
        execution->session = SubmitTask(fresh_task, execution->recovery_plan.plan);
        execution->session.txn.metrics.retry_count = previous_retry_count + 1;
        return true;
      }
      case TaskRecoveryCommandType::kResumeBranch: {
        auto* branch = FindBranch(execution->session, execution->target_branch_id);
        if (branch == nullptr) {
          return false;
        }
        branch->status = data_agent_system::branch::BranchStatus::kRunning;
        execution->session.task.phase = TaskPhase::kRunning;
        execution->session.task.SetMetadata("recovery_execution_status", "branch_resumed");
        return true;
      }
      case TaskRecoveryCommandType::kResumeFromSavepoint: {
        auto* branch = FindBranch(execution->session, execution->target_branch_id);
        if (branch == nullptr) {
          return false;
        }
        if (!RollbackToSavepoint(execution->session.txn, execution->target_branch_id,
                                 execution->target_savepoint_id)) {
          return false;
        }
        branch->status = data_agent_system::branch::BranchStatus::kRunning;
        execution->session.task.phase = TaskPhase::kRunning;
        execution->session.task.SetMetadata("recovery_execution_status", "savepoint_resumed");
        return true;
      }
      case TaskRecoveryCommandType::kRevalidateCommit:
        execution->session.task.SetMetadata("recovery_execution_status", "revalidate_commit");
        return CommitTask(execution->session);
    }
    return false;
  }

 private:
  static data_agent_system::intent::IntentType IntentTypeFromPersistedValue(std::int64_t value) {
    using data_agent_system::intent::IntentType;
    switch (value) {
      case 0:
        return IntentType::kRead;
      case 1:
        return IntentType::kOverwrite;
      case 2:
        return IntentType::kAppend;
      case 3:
        return IntentType::kDelta;
      case 4:
        return IntentType::kCas;
      default:
        return IntentType::kOverwrite;
    }
  }

  void RebuildRecoveredSessionState(TaskSession* session,
                                    const ParsedTaskEventLogArtifact& artifact) const {
    if (session == nullptr) {
      return;
    }

    auto rebuilt = SubmitTask(session->task, session->plan);
    rebuilt.events.clear();

    for (const auto& parsed_event : artifact.events) {
      switch (TaskEventTypeFromName(parsed_event.event_type)) {
        case TaskEventType::kSubmitTask:
        case TaskEventType::kCreateBranch:
          break;
        case TaskEventType::kRead: {
          auto* branch = FindBranch(rebuilt, parsed_event.branch_id);
          if (branch != nullptr) {
            branch->status = data_agent_system::branch::BranchStatus::kRunning;
            branch->read_set.Record(parsed_event.object_id,
                                    static_cast<std::uint64_t>(parsed_event.numeric_value));
          }
          break;
        }
        case TaskEventType::kWrite: {
          data_agent_system::intent::WriteIntent intent;
          intent.object_id = parsed_event.object_id;
          intent.intent_type = IntentTypeFromPersistedValue(parsed_event.numeric_value);
          intent.payload = parsed_event.detail;
          Write(rebuilt.txn, parsed_event.branch_id, parsed_event.object_id,
                data_agent_system::cache::ObjectType::kGeneric, intent);
          break;
        }
        case TaskEventType::kSavepoint:
          Savepoint(rebuilt.txn, parsed_event.branch_id, parsed_event.detail);
          break;
        case TaskEventType::kRollbackToSavepoint:
          RollbackToSavepoint(rebuilt.txn, parsed_event.branch_id, parsed_event.detail);
          break;
        case TaskEventType::kStageBranch:
          StageBranch(rebuilt.txn, parsed_event.branch_id,
                      static_cast<double>(parsed_event.numeric_value), parsed_event.detail);
          break;
        case TaskEventType::kCommitAttempt:
        case TaskEventType::kCommitTask:
        case TaskEventType::kAbortTask:
          break;
      }
    }

    rebuilt.commit_attempts =
        static_cast<std::size_t>(std::strtoull(artifact.metadata.at("commit_attempts").c_str(),
                                              nullptr, 10));
    rebuilt.committed = artifact.metadata.at("committed") == "1";
    rebuilt.events.clear();
    for (const auto& parsed_event : artifact.events) {
      TaskEvent event;
      event.sequence_number = parsed_event.sequence_number;
      event.type = TaskEventTypeFromName(parsed_event.event_type);
      event.branch_id = parsed_event.branch_id;
      event.object_id = parsed_event.object_id;
      event.detail = parsed_event.detail;
      event.numeric_value = parsed_event.numeric_value;
      rebuilt.events.push_back(event);
    }

    *session = rebuilt;
  }

  static void ApplyRecoveredTerminalState(RecoveredTaskSession* recovered) {
    if (recovered == nullptr) {
      return;
    }

    auto& session = recovered->session;
    const auto action = recovered->recovery_plan.decision.action;
    if (session.committed) {
      session.txn.status = data_agent_system::agent_txn::TxnStatus::kCommitted;
      session.task.phase = TaskPhase::kCommitted;
      for (auto& branch : session.txn.branches) {
        if (branch.branch_id == recovered->recovery_plan.decision.resume_branch_id) {
          branch.status = data_agent_system::branch::BranchStatus::kCommitted;
        } else {
          branch.status = data_agent_system::branch::BranchStatus::kDiscarded;
        }
      }
      return;
    }

    if (action == TaskRecoveryAction::kRetryFromScratch) {
      session.txn.status = data_agent_system::agent_txn::TxnStatus::kAborted;
      session.task.phase = TaskPhase::kAborted;
      for (auto& branch : session.txn.branches) {
        if (branch.branch_id == recovered->recovery_plan.decision.resume_branch_id) {
          branch.status = data_agent_system::branch::BranchStatus::kAborted;
        } else {
          branch.status = data_agent_system::branch::BranchStatus::kDiscarded;
        }
      }
      return;
    }

    session.txn.status = data_agent_system::agent_txn::TxnStatus::kRunning;
    session.task.phase = TaskPhase::kRunning;
    for (auto& branch : session.txn.branches) {
      if (branch.branch_id == recovered->recovery_plan.decision.resume_branch_id) {
        branch.status = data_agent_system::branch::BranchStatus::kRunning;
      }
    }
  }

  data_agent_system::storage::VersionedKVStore& store_;
  data_agent_system::agent_txn::AgentTxnManager txn_manager_;
  data_agent_system::agent_txn::RollbackManager rollback_manager_;
  data_agent_system::branch::BranchManager branch_manager_;
  TaskRecoveryManager task_recovery_manager_;
  TaskContinuationRegistry continuation_registry_;
};

}  // namespace data_agent_system::runtime
