#pragma once

#include <smart/execution/execution_context.hpp>
#include <smart/runtime/runtime.hpp>

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <type_traits>
#include <cstdint>

namespace smart::detail
{
struct SemanticOperationDescriptor
{
    std::string operation;
    std::string operation_semantic_version = "1.0";
    std::string element_type;
    NumericalPolicy numerical_policy = NumericalPolicy::Fast;
    std::vector<std::size_t> extents;
    std::vector<std::size_t> strides;
    std::string layout = "contiguous";
    std::string boundary_mode = "none";
    bool in_place = false;
    std::string semantic_constants;
    std::string expected_evaluation_order;
    std::string expected_accumulation;
    std::string expected_canonical_plan = "none";
    std::string provider = "native";
    std::string provider_version;
    std::string forced_route;
};

struct PreparedSemanticOperation
{
    std::shared_ptr<RuntimeState> state;
    ExecutionContext context;
    SemanticOperationDescriptor descriptor;
    std::string workload_fingerprint;
    std::optional<ExecutionPlan> exact_plan;
    std::optional<OperationProfile> profile;
    bool deterministic_replay = false;
    bool warm_start = false;
    bool forced_route = false;
    ExecutionLease resource_lease;
    ResourceDecisionReport resource_decision;
};

PreparedSemanticOperation prepare_semantic_operation(
    const ExecutionContext& context,
    SemanticOperationDescriptor descriptor);
void complete_semantic_operation(PreparedSemanticOperation& prepared,
                                 const std::string& selected_route,
                                 const std::string& simd_kernel = "none",
                                 const std::string& provider = "native",
                                 const std::string& provider_version = "");
std::string exact_workload_fingerprint(const SemanticOperationDescriptor& descriptor);
std::optional<ExecutionPlan> generic_context_execution_plan(
    const ExecutionContext& context);
std::string floating_value_identity(const void* value, std::size_t size);

template <typename T>
inline const char* stable_element_type_name() noexcept
{
    if constexpr (std::is_same_v<T, float>) return "float32";
    else if constexpr (std::is_same_v<T, double>) return "float64";
    else if constexpr (std::is_same_v<T, std::uint8_t>) return "uint8";
    else if constexpr (std::is_same_v<T, std::int8_t>) return "int8";
    else if constexpr (std::is_same_v<T, std::uint16_t>) return "uint16";
    else if constexpr (std::is_same_v<T, std::int16_t>) return "int16";
    else if constexpr (std::is_same_v<T, std::uint32_t>) return "uint32";
    else if constexpr (std::is_same_v<T, std::int32_t>) return "int32";
    else if constexpr (std::is_same_v<T, std::uint64_t>) return "uint64";
    else if constexpr (std::is_same_v<T, std::int64_t>) return "int64";
    else return "unsupported";
}

class SemanticOperationScope
{
  public:
    explicit SemanticOperationScope(const PreparedSemanticOperation& prepared);
    ~SemanticOperationScope();
    SemanticOperationScope(const SemanticOperationScope&) = delete;
    SemanticOperationScope& operator=(const SemanticOperationScope&) = delete;

  private:
    std::unique_ptr<ExecutionContextScope> context_scope_;
    std::unique_ptr<class ForcedExecutionPlanScope> forced_scope_;
};
}
