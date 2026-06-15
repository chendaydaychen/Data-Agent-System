#pragma once

#include <cstdlib>
#include <string>
#include <unordered_map>
#include <vector>

#include "data_agent_system/agent_txn/agent_txn_context.h"
#include "data_agent_system/cache/object_cache.h"
#include "data_agent_system/intent/intent.h"
#include "data_agent_system/runtime/task_continuation.h"
#include "data_agent_system/runtime/execution_plan.h"
#include "data_agent_system/runtime/task_context.h"
#include "data_agent_system/runtime/task_runtime.h"

namespace data_agent_system::workloads::kv_style {

enum class StepKind {
  kRead,
  kWrite,
  kSavepoint,
  kRollbackToSavepoint,
};

struct KvStep {
  StepKind kind = StepKind::kRead;
  std::string object_id;
  data_agent_system::cache::ObjectType object_type =
      data_agent_system::cache::ObjectType::kGeneric;
  data_agent_system::intent::WriteIntent intent;
  std::string savepoint_id;
};

struct KvBranchProgram {
  std::string branch_id;
  double score = 0.0;
  std::string summary;
  std::vector<KvStep> steps;
};

struct KvTaskScript {
  std::string task_id;
  std::string objective;
  std::vector<KvBranchProgram> branches;
  std::vector<KvStep> recovery_steps;
  double recovery_score = 0.0;
  std::string recovery_summary;
  std::string recovery_preferred_branch_id;
};

inline std::string KvStepKindName(StepKind kind) {
  switch (kind) {
    case StepKind::kRead:
      return "read";
    case StepKind::kWrite:
      return "write";
    case StepKind::kSavepoint:
      return "savepoint";
    case StepKind::kRollbackToSavepoint:
      return "rollback_to_savepoint";
  }
  return "read";
}

inline StepKind KvStepKindFromName(const std::string& name) {
  if (name == "write") {
    return StepKind::kWrite;
  }
  if (name == "savepoint") {
    return StepKind::kSavepoint;
  }
  if (name == "rollback_to_savepoint") {
    return StepKind::kRollbackToSavepoint;
  }
  return StepKind::kRead;
}

inline void AddKvObjectReferenceIfMissing(
    std::unordered_map<std::string, std::string>* object_roles,
    std::vector<data_agent_system::runtime::ObjectReference>* objects,
    const std::string& object_id,
    const std::string& object_role) {
  if (object_roles == nullptr || objects == nullptr || object_id.empty()) {
    return;
  }
  const auto [it, inserted] = object_roles->emplace(object_id, object_role);
  if (inserted) {
    objects->push_back(data_agent_system::runtime::ObjectReference{object_id, object_role});
  }
}

inline data_agent_system::runtime::ExecutionPlan BuildExecutionPlan(const KvTaskScript& script) {
  data_agent_system::runtime::ExecutionPlan plan;
  for (const auto& branch : script.branches) {
    plan.AddBranchPlan(branch.branch_id, branch.branch_id + "_candidate", "scripted kv branch",
                       branch.score);
  }
  return plan;
}

inline data_agent_system::runtime::TaskContext BuildTaskContext(const KvTaskScript& script) {
  data_agent_system::runtime::TaskContext task;
  task.task_id = script.task_id;
  task.objective = script.objective;
  task.workload_name = "kv_style.scripted_kv_task";
  task.planner_id = "kv_style_script_runner";
  task.phase = data_agent_system::runtime::TaskPhase::kCreated;

  std::unordered_map<std::string, std::string> input_roles;
  std::unordered_map<std::string, std::string> output_roles;
  auto collect_step_refs = [&](const KvStep& step) {
    switch (step.kind) {
      case StepKind::kRead:
        AddKvObjectReferenceIfMissing(&input_roles, &task.input_objects, step.object_id,
                                      "kv_input");
        break;
      case StepKind::kWrite:
        AddKvObjectReferenceIfMissing(&output_roles, &task.output_objects, step.object_id,
                                      "kv_output");
        break;
      case StepKind::kSavepoint:
      case StepKind::kRollbackToSavepoint:
        break;
    }
  };

  for (const auto& branch : script.branches) {
    for (const auto& step : branch.steps) {
      collect_step_refs(step);
    }
  }
  for (const auto& step : script.recovery_steps) {
    collect_step_refs(step);
  }

  for (const auto& branch : script.branches) {
    task.SetMetadata("branch:" + branch.branch_id, branch.summary);
  }
  task.SetMetadata("recovery.step_count", std::to_string(script.recovery_steps.size()));
  for (std::size_t i = 0; i < script.recovery_steps.size(); ++i) {
    const auto& step = script.recovery_steps[i];
    task.SetMetadata("recovery.step." + std::to_string(i) + ".kind", KvStepKindName(step.kind));
    task.SetMetadata("recovery.step." + std::to_string(i) + ".object_id", step.object_id);
    task.SetMetadata("recovery.step." + std::to_string(i) + ".object_type",
                     std::to_string(static_cast<int>(step.object_type)));
    task.SetMetadata("recovery.step." + std::to_string(i) + ".intent_type",
                     std::to_string(static_cast<int>(step.intent.intent_type)));
    task.SetMetadata("recovery.step." + std::to_string(i) + ".payload", step.intent.payload);
    task.SetMetadata("recovery.step." + std::to_string(i) + ".condition_type",
                     std::to_string(static_cast<int>(step.intent.condition.type)));
    task.SetMetadata("recovery.step." + std::to_string(i) + ".condition_value",
                     step.intent.condition.expected_value);
    task.SetMetadata("recovery.step." + std::to_string(i) + ".savepoint_id", step.savepoint_id);
  }
  task.SetMetadata("recovery.score", std::to_string(script.recovery_score));
  task.SetMetadata("recovery.summary", script.recovery_summary);
  task.SetMetadata("recovery.preferred_branch_id", script.recovery_preferred_branch_id);
  return task;
}

inline data_agent_system::runtime::TaskSession RunScriptedKvTask(
    data_agent_system::runtime::TaskRuntime& runtime,
    const KvTaskScript& script) {
  auto session = runtime.SubmitTask(BuildTaskContext(script), BuildExecutionPlan(script));
  session.txn.metrics.branch_count = script.branches.size();
  session.txn.metrics.planned_loser_count =
      script.branches.empty() ? 0 : script.branches.size() - 1;

  for (const auto& branch : script.branches) {
    for (const auto& step : branch.steps) {
      switch (step.kind) {
        case StepKind::kRead:
          runtime.Read(session, branch.branch_id, step.object_id);
          break;
        case StepKind::kWrite:
          runtime.Write(session, branch.branch_id, step.object_id, step.object_type, step.intent);
          break;
        case StepKind::kSavepoint:
          runtime.Savepoint(session, branch.branch_id, step.savepoint_id);
          break;
        case StepKind::kRollbackToSavepoint:
          runtime.RollbackToSavepoint(session, branch.branch_id, step.savepoint_id);
          break;
      }
    }
    runtime.StageBranch(session, branch.branch_id, branch.score, branch.summary);
  }

  runtime.CommitTask(session);
  return session;
}

inline bool ContinueRecoveredKvTask(
    data_agent_system::runtime::TaskRuntime& runtime,
    data_agent_system::runtime::TaskRecoveryExecution* execution,
    const std::vector<KvStep>& continuation_steps,
    double score,
    const std::string& summary,
    const std::string& preferred_branch_id = std::string()) {
  data_agent_system::runtime::TaskContinuationSpec spec;
  spec.preferred_branch_id = preferred_branch_id;
  spec.score = score;
  spec.summary = summary;
  return data_agent_system::runtime::ContinueRecoveredTask(
      runtime, execution, spec,
      [&](data_agent_system::runtime::TaskRuntime& callback_runtime,
          data_agent_system::runtime::TaskRecoveryExecution& callback_execution,
          const std::string& branch_id) {
        for (const auto& step : continuation_steps) {
          switch (step.kind) {
            case StepKind::kRead:
              callback_runtime.Read(callback_execution.session, branch_id, step.object_id);
              break;
            case StepKind::kWrite:
              callback_runtime.Write(callback_execution.session, branch_id, step.object_id,
                                     step.object_type, step.intent);
              break;
            case StepKind::kSavepoint:
              callback_runtime.Savepoint(callback_execution.session, branch_id, step.savepoint_id);
              break;
            case StepKind::kRollbackToSavepoint:
              callback_runtime.RollbackToSavepoint(callback_execution.session, branch_id,
                                                   step.savepoint_id);
              break;
          }
        }
        return true;
      });
}

inline bool RegisterScriptedKvContinuation(
    data_agent_system::runtime::TaskRuntime& runtime,
    const std::vector<KvStep>& continuation_steps,
    double score,
    const std::string& summary,
    const std::string& preferred_branch_id = std::string()) {
  return runtime.RegisterContinuationHandler(
      "kv_style.scripted_kv_task",
      [=](data_agent_system::runtime::TaskRuntime& callback_runtime,
          data_agent_system::runtime::TaskRecoveryExecution& execution) {
        return ContinueRecoveredKvTask(callback_runtime, &execution, continuation_steps, score,
                                       summary, preferred_branch_id);
      });
}

inline std::vector<KvStep> LoadRecoveryStepsFromTask(
    const data_agent_system::runtime::TaskContext& task) {
  auto metadata_value = [&](const std::string& key) -> std::string {
    const auto it = task.metadata.find(key);
    return it == task.metadata.end() ? std::string() : it->second;
  };

  std::vector<KvStep> steps;
  const std::size_t step_count =
      static_cast<std::size_t>(std::strtoull(metadata_value("recovery.step_count").c_str(),
                                            nullptr, 10));
  steps.reserve(step_count);
  for (std::size_t i = 0; i < step_count; ++i) {
    KvStep step;
    step.kind = KvStepKindFromName(metadata_value("recovery.step." + std::to_string(i) + ".kind"));
    step.object_id = metadata_value("recovery.step." + std::to_string(i) + ".object_id");
    step.object_type = static_cast<data_agent_system::cache::ObjectType>(
        std::strtol(metadata_value("recovery.step." + std::to_string(i) + ".object_type").c_str(),
                    nullptr, 10));
    step.intent.object_id = step.object_id;
    step.intent.intent_type = static_cast<data_agent_system::intent::IntentType>(
        std::strtol(metadata_value("recovery.step." + std::to_string(i) + ".intent_type").c_str(),
                    nullptr, 10));
    step.intent.payload = metadata_value("recovery.step." + std::to_string(i) + ".payload");
    step.intent.condition.type = static_cast<data_agent_system::intent::ConditionType>(
        std::strtol(
            metadata_value("recovery.step." + std::to_string(i) + ".condition_type").c_str(),
            nullptr, 10));
    step.intent.condition.expected_value =
        metadata_value("recovery.step." + std::to_string(i) + ".condition_value");
    step.savepoint_id = metadata_value("recovery.step." + std::to_string(i) + ".savepoint_id");
    steps.push_back(step);
  }
  return steps;
}

inline bool RegisterScriptedKvContinuationFromTaskMetadata(
    data_agent_system::runtime::TaskRuntime& runtime) {
  return runtime.RegisterContinuationHandler(
      "kv_style.scripted_kv_task",
      [](data_agent_system::runtime::TaskRuntime& callback_runtime,
         data_agent_system::runtime::TaskRecoveryExecution& execution) {
        auto steps = LoadRecoveryStepsFromTask(execution.session.task);
        auto metadata_value = [&](const std::string& key) -> std::string {
          const auto it = execution.session.task.metadata.find(key);
          return it == execution.session.task.metadata.end() ? std::string() : it->second;
        };
        const double score = std::strtod(metadata_value("recovery.score").c_str(), nullptr);
        return ContinueRecoveredKvTask(
            callback_runtime, &execution, steps, score, metadata_value("recovery.summary"),
            metadata_value("recovery.preferred_branch_id"));
      });
}

inline KvStep MakeReadStep(const std::string& object_id) {
  KvStep step;
  step.kind = StepKind::kRead;
  step.object_id = object_id;
  return step;
}

inline KvStep MakeWriteStep(const std::string& object_id,
                            data_agent_system::cache::ObjectType object_type,
                            data_agent_system::intent::IntentType intent_type,
                            const std::string& payload,
                            const data_agent_system::intent::Condition& condition = {}) {
  KvStep step;
  step.kind = StepKind::kWrite;
  step.object_id = object_id;
  step.object_type = object_type;
  step.intent.object_id = object_id;
  step.intent.intent_type = intent_type;
  step.intent.payload = payload;
  step.intent.condition = condition;
  return step;
}

inline KvStep MakeSavepointStep(const std::string& savepoint_id) {
  KvStep step;
  step.kind = StepKind::kSavepoint;
  step.savepoint_id = savepoint_id;
  return step;
}

inline KvStep MakeRollbackStep(const std::string& savepoint_id) {
  KvStep step;
  step.kind = StepKind::kRollbackToSavepoint;
  step.savepoint_id = savepoint_id;
  return step;
}

}  // namespace data_agent_system::workloads::kv_style
