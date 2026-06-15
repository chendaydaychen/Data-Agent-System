#pragma once

#include <string>

#include "data_agent_system/cache/object_cache.h"
#include "data_agent_system/intent/intent.h"
#include "data_agent_system/runtime/execution_plan.h"
#include "data_agent_system/runtime/task_context.h"
#include "data_agent_system/runtime/task_runtime.h"

namespace data_agent_system::workloads::agent_task {

inline data_agent_system::runtime::TaskContext BuildMinimalCandidateTaskContext(
    const std::string& task_id,
    const std::string& input_key,
    const std::string& output_key) {
  data_agent_system::runtime::TaskContext task;
  task.task_id = task_id;
  task.objective = "generate candidate outputs and commit winner";
  task.workload_name = "agent_task.minimal_candidate_task";
  task.planner_id = "minimal_candidate_planner";
  task.phase = data_agent_system::runtime::TaskPhase::kCreated;
  task.AddInputObject(input_key, "task_input");
  task.AddOutputObject(output_key, "task_output");
  task.SetMetadata("branch_count", "3");
  return task;
}

inline data_agent_system::runtime::ExecutionPlan BuildMinimalCandidateExecutionPlan() {
  data_agent_system::runtime::ExecutionPlan plan;
  plan.AddBranchPlan("branch_A", "candidate_A", "baseline candidate", 72.0);
  plan.AddBranchPlan("branch_B", "candidate_B", "best candidate", 91.0);
  plan.AddBranchPlan("branch_C", "candidate_C", "fallback candidate", 65.0);
  return plan;
}

inline void ExecuteMinimalCandidateBranches(
    data_agent_system::runtime::TaskRuntime& runtime,
    data_agent_system::runtime::TaskSession& session,
    const std::string& input_key,
    const std::string& output_key) {
  runtime.Read(session, "branch_A", input_key);
  runtime.Write(session, "branch_A", output_key, data_agent_system::cache::ObjectType::kText,
                data_agent_system::intent::WriteIntent{
                    output_key,
                    data_agent_system::intent::IntentType::kOverwrite,
                    "result_A",
                    {},
                });
  runtime.StageBranch(session, "branch_A", 72.0, "candidate A");

  runtime.Read(session, "branch_B", input_key);
  runtime.Write(session, "branch_B", output_key, data_agent_system::cache::ObjectType::kText,
                data_agent_system::intent::WriteIntent{
                    output_key,
                    data_agent_system::intent::IntentType::kOverwrite,
                    "result_B",
                    {},
                });
  runtime.StageBranch(session, "branch_B", 91.0, "candidate B");

  runtime.Read(session, "branch_C", input_key);
  runtime.Write(session, "branch_C", output_key, data_agent_system::cache::ObjectType::kText,
                data_agent_system::intent::WriteIntent{
                    output_key,
                    data_agent_system::intent::IntentType::kOverwrite,
                    "result_C",
                    {},
                });
  runtime.StageBranch(session, "branch_C", 65.0, "candidate C");
}

inline data_agent_system::runtime::TaskSession RunMinimalCandidateTask(
    data_agent_system::runtime::TaskRuntime& runtime,
    const std::string& task_id,
    const std::string& input_key,
    const std::string& output_key) {
  auto session = runtime.SubmitTask(BuildMinimalCandidateTaskContext(task_id, input_key, output_key),
                                    BuildMinimalCandidateExecutionPlan());
  ExecuteMinimalCandidateBranches(runtime, session, input_key, output_key);

  runtime.CommitTask(session);
  return session;
}

}  // namespace data_agent_system::workloads::agent_task
