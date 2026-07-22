#include <iostream>
#include <smart/execution/execution_context.hpp>

namespace
{
smart::ExecutionContext make_child(const smart::ExecutionContext& parent,
                                   smart::ExecutionEngineType engine,
                                   bool parallel,
                                   std::size_t budget)
{
    smart::detail::ExecutionContextScope parent_scope(parent);
    smart::ExecutionContext child = smart::detail::make_execution_context();
    child.engine = engine;
    child.parallel = parallel;
    child.concurrency_budget = budget;
    smart::inherit_execution_lineage(child, parent);
    return child;
}

bool report(const char* label, bool passed)
{
    std::cout << label << ": " << (passed ? "PASS" : "FAIL") << '\n';
    return passed;
}
} // namespace

int main()
{
    smart::ExecutionContext root;
    root.loop_id = 100;
    root.depth = 1;
    root.engine = smart::ExecutionEngineType::OneTbb;
    root.parallel = true;
    root.concurrency_budget = 8;
    smart::inherit_execution_lineage(root, {});

    const smart::ExecutionContext sequential = make_child(
        root, smart::ExecutionEngineType::Auto, false, 1);
    const smart::ExecutionContext reentered = make_child(
        sequential, smart::ExecutionEngineType::OneTbb, true, 4);
    const smart::ExecutionContext fourth_level = make_child(
        reentered, smart::ExecutionEngineType::ThreadPool, true, 2);

    const bool root_ok = report(
        "Root lineage initializes runtime ownership",
        root.root_loop_id == root.loop_id
            && root.runtime_owner_loop_id == root.loop_id
            && root.runtime_owner_engine == smart::ExecutionEngineType::OneTbb
            && root.inherited_concurrency_budget == 8);

    const bool sequential_ok = report(
        "Sequential region preserves oneTBB lineage",
        sequential.depth == 2
            && sequential.root_loop_id == root.loop_id
            && sequential.nearest_parallel_ancestor_loop_id == root.loop_id
            && sequential.nearest_parallel_ancestor_engine == smart::ExecutionEngineType::OneTbb
            && sequential.runtime_owner_loop_id == root.loop_id
            && sequential.runtime_owner_engine == smart::ExecutionEngineType::OneTbb
            && sequential.inherited_concurrency_budget == 8);

    const bool reentry_ok = report(
        "Parallel descendant re-enters inherited oneTBB domain",
        reentered.depth == 3
            && reentered.parent_loop_id == sequential.loop_id
            && reentered.root_loop_id == root.loop_id
            && reentered.runtime_owner_loop_id == root.loop_id
            && reentered.runtime_owner_engine == smart::ExecutionEngineType::OneTbb
            && reentered.inherited_concurrency_budget == 8);

    const bool depth_four_ok = report(
        "Fourth-level backend change keeps full root lineage",
        fourth_level.depth == 4
            && fourth_level.root_loop_id == root.loop_id
            && fourth_level.nearest_parallel_ancestor_loop_id == reentered.loop_id
            && fourth_level.nearest_parallel_ancestor_engine == smart::ExecutionEngineType::OneTbb
            && fourth_level.runtime_owner_loop_id == fourth_level.loop_id
            && fourth_level.runtime_owner_engine == smart::ExecutionEngineType::ThreadPool
            && fourth_level.inherited_concurrency_budget == 4);

    const bool passed = root_ok && sequential_ok && reentry_ok && depth_four_ok;
    std::cout << (passed
                      ? "PASS: execution lineage and runtime inheritance are correct.\n"
                      : "FAIL: execution lineage and runtime inheritance are incorrect.\n");
    return passed ? 0 : 1;
}
