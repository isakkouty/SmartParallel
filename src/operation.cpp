#include <smart/runtime/detail/operation.hpp>
#include <smart/runtime/detail/state.hpp>
#include <smart/execution/execution_override.hpp>
#include <smart/execution/parallel.hpp>
#include <smart/execution/runtime_capabilities.hpp>
#include <smart/decision/decision.hpp>
#include <smart/numerical/policy.hpp>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace smart::detail
{
namespace
{
std::string vector_identity(const std::vector<std::size_t>& values)
{
    std::ostringstream out;
    for (std::size_t index=0; index<values.size(); ++index)
    {
        if(index) out << ',';
        out << values[index];
    }
    return out.str();
}
std::string utc_now()
{
    const std::time_t now=std::time(nullptr);
    std::tm value{};
#if defined(_WIN32)
    gmtime_s(&value,&now);
#else
    gmtime_r(&now,&value);
#endif
    std::ostringstream out;
    out << std::put_time(&value,"%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}
std::string evaluation_order_name(EvaluationOrder value)
{
    switch(value)
    {
        case EvaluationOrder::Adaptive:return "adaptive";
        case EvaluationOrder::CanonicalDeterministic:return "canonical_deterministic";
        case EvaluationOrder::CanonicalPointwise:return "canonical_pointwise";
    }
    return "unknown";
}
std::string accumulation_name(AccumulationMethod value)
{
    switch(value)
    {
        case AccumulationMethod::Native:return "native";
        case AccumulationMethod::FixedPointwiseExpression:return "fixed_pointwise_expression";
        case AccumulationMethod::CanonicalPairwise:return "canonical_pairwise";
        case AccumulationMethod::Compensated:return "compensated";
        case AccumulationMethod::ScaledSumOfSquares:return "scaled_sum_of_squares";
    }
    return "unknown";
}
ExecutionPlan forced_runtime_plan(const RuntimeState& state)
{
    const Config& config=*state.configuration;
    if(config.execution_engine==ExecutionEngineType::Auto)
        throw std::runtime_error("SmartParallel Deterministic Runtime requires an Approved exact profile or explicit forced scheduler");
    if(!execution_backend_available(config.execution_engine))
        throw std::runtime_error("SmartParallel forced scheduler is unavailable in this build");
    const std::size_t workers=std::max<std::size_t>(1,config.nested_root_concurrency_budget);
    ExecutionPlan plan;
    plan.engine=config.execution_engine;
    plan.job_count=workers;
    plan.parallel=workers>1;
    plan.strategy=!plan.parallel?ExecutionStrategy::Sequential:
        config.execution_engine==ExecutionEngineType::StaticThread
            ?ExecutionStrategy::StaticChunks:ExecutionStrategy::DynamicChunks;
    plan.chunk_size=0;
    return plan;
}
void add_mismatch(ProfileCompatibilityReport& report,
                  CompatibilityIssueCode code,
                  const char* field,
                  const std::string& expected,
                  const std::string& actual)
{
    report.add({code,field,expected,actual,std::string(field)+" is incompatible"});
}
ProfileCompatibilityReport compatibility(const RuntimeState& state,
                                          const SemanticOperationDescriptor& descriptor,
                                          const std::string& workload_fingerprint,
                                          const OperationProfile& entry,
                                          bool deterministic)
{
    ProfileCompatibilityReport report;
    if(entry.operation!=descriptor.operation)add_mismatch(report,CompatibilityIssueCode::OperationMismatch,"operation",descriptor.operation,entry.operation);
    if(entry.operation_semantic_version!=descriptor.operation_semantic_version)add_mismatch(report,CompatibilityIssueCode::OperationSemanticVersionMismatch,"operation_semantic_version",descriptor.operation_semantic_version,entry.operation_semantic_version);
    if(entry.element_type!=descriptor.element_type)add_mismatch(report,CompatibilityIssueCode::DataTypeMismatch,"element_type",descriptor.element_type,entry.element_type);
    if(entry.numerical_policy!=descriptor.numerical_policy)add_mismatch(report,CompatibilityIssueCode::NumericalPolicyMismatch,"numerical_policy",numerical_policy_name(descriptor.numerical_policy),numerical_policy_name(entry.numerical_policy));
    if(entry.workload.exact_fingerprint!=workload_fingerprint)add_mismatch(report,CompatibilityIssueCode::WorkloadFingerprintMismatch,"workload_fingerprint",workload_fingerprint,entry.workload.exact_fingerprint);
    if(entry.workload.extents!=descriptor.extents)add_mismatch(report,CompatibilityIssueCode::WorkloadExtentMismatch,"extents",vector_identity(descriptor.extents),vector_identity(entry.workload.extents));
    if(entry.workload.strides!=descriptor.strides)add_mismatch(report,CompatibilityIssueCode::StrideMismatch,"strides",vector_identity(descriptor.strides),vector_identity(entry.workload.strides));
    if(entry.workload.layout!=descriptor.layout)add_mismatch(report,CompatibilityIssueCode::LayoutMismatch,"layout",descriptor.layout,entry.workload.layout);
    if(entry.workload.boundary_mode!=descriptor.boundary_mode)add_mismatch(report,CompatibilityIssueCode::BoundaryModeMismatch,"boundary_mode",descriptor.boundary_mode,entry.workload.boundary_mode);
    if(!descriptor.expected_evaluation_order.empty()&&entry.evaluation_order!=descriptor.expected_evaluation_order)add_mismatch(report,CompatibilityIssueCode::EvaluationOrderMismatch,"evaluation_order",descriptor.expected_evaluation_order,entry.evaluation_order);
    if(!descriptor.expected_accumulation.empty()&&entry.accumulation_algorithm!=descriptor.expected_accumulation)add_mismatch(report,CompatibilityIssueCode::AccumulationAlgorithmMismatch,"accumulation_algorithm",descriptor.expected_accumulation,entry.accumulation_algorithm);
    if(!descriptor.expected_canonical_plan.empty()&&entry.canonical_plan!=descriptor.expected_canonical_plan)add_mismatch(report,CompatibilityIssueCode::CanonicalPlanMismatch,"canonical_plan",descriptor.expected_canonical_plan,entry.canonical_plan);
    const auto& current=state.environment;
    const auto& saved=entry.environment;
    if(saved.architecture!=current.architecture)add_mismatch(report,CompatibilityIssueCode::ArchitectureMismatch,"architecture",current.architecture,saved.architecture);
    if(saved.pointer_width!=current.pointer_width)add_mismatch(report,CompatibilityIssueCode::ArchitectureMismatch,"pointer_width",std::to_string(current.pointer_width),std::to_string(saved.pointer_width));
    if(saved.endianness!=current.endianness)add_mismatch(report,CompatibilityIssueCode::ArchitectureMismatch,"endianness",current.endianness,saved.endianness);
    if(saved.os_family!=current.os_family)add_mismatch(report,CompatibilityIssueCode::CompilerBuildMismatch,"os_family",current.os_family,saved.os_family);
    if(saved.compiler_identity!=current.compiler_identity||saved.compiler_version!=current.compiler_version)
        add_mismatch(report,CompatibilityIssueCode::CompilerBuildMismatch,"compiler",current.compiler_identity+current.compiler_version,saved.compiler_identity+saved.compiler_version);
    if(saved.smartparallel_build_fingerprint!=current.smartparallel_build_fingerprint)
        add_mismatch(report,CompatibilityIssueCode::CompilerBuildMismatch,"smartparallel_build_fingerprint",current.smartparallel_build_fingerprint,saved.smartparallel_build_fingerprint);
    if(saved.floating_point_environment!=current.floating_point_environment)
        add_mismatch(report,CompatibilityIssueCode::FloatingPointEnvironmentMismatch,"floating_point_environment",current.floating_point_environment,saved.floating_point_environment);
    if(saved.application_build_identifier!=current.application_build_identifier)
        add_mismatch(report,CompatibilityIssueCode::BuildIdentifierMismatch,"application_build_identifier",current.application_build_identifier,saved.application_build_identifier);
    const std::size_t runtime_budget=state.configuration->nested_root_concurrency_budget;
    if(entry.exact_worker_budget==0 || entry.exact_worker_budget>runtime_budget)
        add_mismatch(report,CompatibilityIssueCode::WorkerBudgetMismatch,"worker_budget",
                     "profile budget <= Runtime ceiling "+std::to_string(runtime_budget),
                     std::to_string(entry.exact_worker_budget));
    if(entry.execution_plan.parallel&&!execution_backend_available(entry.execution_plan.engine))
        add_mismatch(report,CompatibilityIssueCode::SchedulerUnavailable,"scheduler","available",runtime_name(entry.execution_plan.engine));
    if(entry.execution_plan.parallel && entry.execution_plan.job_count!=entry.exact_worker_budget)
        add_mismatch(report,CompatibilityIssueCode::WorkerBudgetMismatch,"execution_plan.job_count",std::to_string(entry.exact_worker_budget),std::to_string(entry.execution_plan.job_count));
    if(entry.actual_worker_policy!="exact")
        add_mismatch(report,CompatibilityIssueCode::ProviderSettingMismatch,"actual_worker_policy","exact",entry.actual_worker_policy);
    if(deterministic && entry.resource_contract_present)
    {
        if(!entry.exact_grant_required)
            add_mismatch(report,CompatibilityIssueCode::WorkerBudgetMismatch,
                         "resource_contract.exact_grant_required","true","false");
        if(entry.requested_workers!=entry.exact_worker_budget
           || entry.minimum_workers!=entry.exact_worker_budget
           || entry.preferred_workers!=entry.exact_worker_budget
           || entry.maximum_workers!=entry.exact_worker_budget
           || entry.granted_workers!=entry.exact_worker_budget)
            add_mismatch(report,CompatibilityIssueCode::WorkerBudgetMismatch,
                         "resource_contract.grant",std::to_string(entry.exact_worker_budget),
                         std::to_string(entry.granted_workers));
        if(entry.scheduler_concurrency_cap!=(entry.execution_plan.parallel
                ? entry.execution_plan.job_count : std::size_t{1}))
            add_mismatch(report,CompatibilityIssueCode::WorkerBudgetMismatch,
                         "resource_contract.scheduler_concurrency_cap",
                         std::to_string(entry.execution_plan.parallel
                             ? entry.execution_plan.job_count : std::size_t{1}),
                         std::to_string(entry.scheduler_concurrency_cap));
        if(entry.execution_plan.engine==ExecutionEngineType::OneTbb
           && entry.provider_control_strength!=ControlStrength::UpperBound)
            add_mismatch(report,CompatibilityIssueCode::ProviderSettingMismatch,
                         "resource_contract.provider_control_strength","upper_bound",
                         control_strength_name(entry.provider_control_strength));
        if(entry.provider=="opencv"
           && entry.provider_control_strength!=ControlStrength::SerializedProcessGlobal)
            add_mismatch(report,CompatibilityIssueCode::ProviderSettingMismatch,
                         "resource_contract.provider_control_strength","serialized_process_global",
                         control_strength_name(entry.provider_control_strength));
    }
    if(entry.plan_semantic_version!="1.0")
        add_mismatch(report,CompatibilityIssueCode::CanonicalPlanMismatch,"plan_semantic_version","1.0",entry.plan_semantic_version);
    if(descriptor.operation!="smart.vision.threshold")
    {
        if(entry.provider!=descriptor.provider)
            add_mismatch(report,CompatibilityIssueCode::ProviderUnavailable,"provider",descriptor.provider,entry.provider);
        if(entry.provider_version!=descriptor.provider_version)
            add_mismatch(report,CompatibilityIssueCode::ProviderVersionMismatch,"provider_version",descriptor.provider_version,entry.provider_version);
    }
    if(!entry.provider_settings.empty())
        add_mismatch(report,CompatibilityIssueCode::ProviderSettingMismatch,"provider_settings","",entry.provider_settings);
    if(descriptor.operation!="smart.vision.threshold")
    {
        const std::string expected_route=entry.execution_plan.parallel
            ?std::string("native_")+runtime_name(entry.execution_plan.engine)
            :std::string("sequential");
        if(entry.implementation_route!=expected_route)
            add_mismatch(report,CompatibilityIssueCode::ProviderSettingMismatch,"implementation_route",expected_route,entry.implementation_route);
    }
    else
    {
        const bool supported=entry.implementation_route=="native_sequential"
            ||entry.implementation_route=="native_thread_pool"
            ||entry.implementation_route=="native_static_thread"
            ||entry.implementation_route=="native_one_tbb"
            ||entry.implementation_route=="opencv";
        if(!supported)
            add_mismatch(report,CompatibilityIssueCode::ProviderSettingMismatch,"implementation_route","supported threshold route",entry.implementation_route);
        if(entry.implementation_route=="native_sequential"&&entry.execution_plan.parallel)
            add_mismatch(report,CompatibilityIssueCode::ProviderSettingMismatch,"implementation_route","sequential plan",entry.implementation_route);
        if(entry.implementation_route=="native_thread_pool"&&entry.execution_plan.engine!=ExecutionEngineType::ThreadPool)
            add_mismatch(report,CompatibilityIssueCode::ProviderSettingMismatch,"scheduler","thread_pool",runtime_name(entry.execution_plan.engine));
        if(entry.implementation_route=="native_static_thread"&&entry.execution_plan.engine!=ExecutionEngineType::StaticThread)
            add_mismatch(report,CompatibilityIssueCode::ProviderSettingMismatch,"scheduler","static_thread",runtime_name(entry.execution_plan.engine));
        if(entry.implementation_route=="native_one_tbb"&&entry.execution_plan.engine!=ExecutionEngineType::OneTbb)
            add_mismatch(report,CompatibilityIssueCode::ProviderSettingMismatch,"scheduler","one_tbb",runtime_name(entry.execution_plan.engine));
    }
    if(entry.execution_plan.engine==ExecutionEngineType::OneTbb
       && saved.tbb_version!=current.tbb_version)
        add_mismatch(report,CompatibilityIssueCode::ProviderVersionMismatch,"tbb_version",current.tbb_version,saved.tbb_version);
    if(!entry.environment.required_isa.empty() && entry.environment.required_isa!="baseline")
        add_mismatch(report,CompatibilityIssueCode::RequiredIsaUnavailable,"required_isa","baseline",entry.environment.required_isa);
    if(!entry.evidence.expires_utc.empty() && entry.evidence.expires_utc<=utc_now())
        add_mismatch(report,CompatibilityIssueCode::ProfileExpired,"expires_utc","future timestamp",entry.evidence.expires_utc);
    if(deterministic&&entry.status!=ProfileStatus::Approved)
        add_mismatch(report,CompatibilityIssueCode::ProfileNotApproved,"profile_status","Approved",profile_status_name(entry.status));
    if(!entry.evidence.route_authenticated)
        add_mismatch(report,CompatibilityIssueCode::ProviderUnavailable,"route_authenticated","true","false");
    if(!entry.evidence.numerical_capability_passed)
        add_mismatch(report,CompatibilityIssueCode::NumericalPolicyMismatch,"numerical_capability","true","false");
    if(!entry.evidence.correctness_passed)
        add_mismatch(report,CompatibilityIssueCode::OperationMismatch,"correctness","true","false");
    return report;
}
std::string summarize_issues(const ProfileCompatibilityReport& report)
{
    std::ostringstream out;
    for(std::size_t i=0;i<report.issues.size();++i)
    {
        if(i)out<<"; ";
        out<<compatibility_issue_name(report.issues[i].code)<<"("<<report.issues[i].field<<")";
    }
    return out.str();
}
void refresh_database(ProfileDatabase& database)
{
    database=profile_database_from_json(profile_database_to_canonical_json(database,true));
}
std::string fingerprint_identity(const OperationExecutionFingerprint& f)
{
    std::ostringstream out;
    out<<"runtime="<<f.runtime_fingerprint<<";operation="<<f.operation
       <<";semantic_version="<<f.operation_semantic_version
       <<";workload="<<f.workload_fingerprint
       <<";numerical="<<numerical_policy_name(f.numerical_policy)
       <<";evaluation_order="<<f.evaluation_order
       <<";accumulation="<<f.accumulation_algorithm
       <<";canonical_plan="<<f.canonical_plan
       <<";execution_mode="<<execution_mode_name(f.execution_mode)
       <<";profile_access="<<profile_access_name(f.profile_access)
       <<";database="<<f.profile_database_hash
       <<";entry="<<f.profile_entry_hash
       <<";status="<<profile_status_name(f.profile_status)
       <<";route="<<f.selected_route
       <<";scheduler="<<runtime_name(f.selected_scheduler)
       <<";worker_budget="<<f.worker_budget
       <<";requested_workers="<<f.requested_workers
       <<";minimum_workers="<<f.minimum_workers
       <<";preferred_workers="<<f.preferred_workers
       <<";maximum_workers="<<f.maximum_workers
       <<";granted_workers="<<f.granted_workers
       <<";scheduler_cap="<<f.scheduler_concurrency_cap
       <<";exact_grant="<<(f.exact_grant_required?1:0)
       <<";lease_policy="<<lease_wait_policy_name(f.lease_wait_policy)
       <<";nested_lease="<<nested_lease_mode_name(f.nested_lease_mode)
       <<";control_scope="<<control_scope_name(f.provider_control_scope)
       <<";control_strength="<<control_strength_name(f.provider_control_strength)
       <<";provider_serialized="<<(f.provider_serialized?1:0)
       <<";resource="<<f.resource_fingerprint
       <<";simd="<<f.simd_kernel<<";provider="<<f.provider
       <<";provider_version="<<f.provider_version
       <<";forced="<<(f.forced_route?1:0)
       <<";warm="<<(f.warm_start?1:0)
       <<";deterministic="<<(f.deterministic_replay?1:0);
    return out.str();
}

std::string resource_identity(const ResourceDecisionReport& report)
{
    std::ostringstream out;
    out << "governor=" << report.governor_fingerprint
        << ";budget=" << report.process_cpu_budget
        << ";runtime_ceiling=" << report.runtime_worker_ceiling
        << ";requested=" << report.requested_workers
        << ";minimum=" << report.minimum_workers
        << ";preferred=" << report.preferred_workers
        << ";maximum=" << report.maximum_workers
        << ";granted=" << report.granted_workers
        << ";cap=" << report.scheduler_concurrency_cap
        << ";exact=" << (report.exact_grant_required ? 1 : 0)
        << ";policy=" << lease_wait_policy_name(report.wait_policy)
        << ";nested=" << nested_lease_mode_name(report.nested_mode)
        << ";control_scope=" << control_scope_name(report.provider_control_scope)
        << ";control_strength=" << control_strength_name(report.provider_control_strength)
        << ";serialized=" << (report.provider_serialized ? 1 : 0);
    return out.str();
}

std::size_t workload_items(const SemanticOperationDescriptor& descriptor) noexcept
{
    if (descriptor.extents.empty())
        return 1;
    std::size_t total = 1;
    for (const std::size_t extent : descriptor.extents)
    {
        if (extent == 0)
            return 0;
        if (total > std::numeric_limits<std::size_t>::max() / extent)
            return std::numeric_limits<std::size_t>::max();
        total *= extent;
    }
    return total;
}

std::size_t useful_concurrency(const PreparedSemanticOperation& result) noexcept
{
    const std::size_t ceiling = std::max<std::size_t>(
        1, result.state ? result.state->options.maximum_workers
                        : result.context.runtime_worker_ceiling);
    if (result.exact_plan)
    {
        const std::size_t plan_workers = result.exact_plan->parallel
            ? std::max<std::size_t>(1, result.exact_plan->job_count)
            : std::size_t{1};
        return std::min(ceiling, plan_workers);
    }

    const std::size_t items = workload_items(result.descriptor);
    if (items == 0)
        return 1;
    const std::size_t items_per_worker =
        result.descriptor.operation == "smart.vision.threshold" ? std::size_t{16'384}
        : result.descriptor.operation.find("stencil") != std::string::npos
            || result.descriptor.operation.find("heat") != std::string::npos
            ? std::size_t{8'192}
        : result.descriptor.operation.find("dot") != std::string::npos
            || result.descriptor.operation.find("norm") != std::string::npos
            ? std::size_t{32'768}
        : std::size_t{4'096};
    const std::size_t estimated =
        items > std::numeric_limits<std::size_t>::max() - (items_per_worker - 1)
        ? ceiling
        : std::max<std::size_t>(
            1, (items + items_per_worker - 1) / items_per_worker);
    return std::min(ceiling, estimated);
}

void admit_resources(PreparedSemanticOperation& result)
{
    if (!result.state || !result.context.resource_governor)
        return;

    const bool deterministic =
        result.state->options.execution_mode == ExecutionMode::Deterministic;
    const std::size_t ceiling =
        std::max<std::size_t>(1, result.state->options.maximum_workers);
    const std::size_t preferred = deterministic
        ? (result.exact_plan && result.exact_plan->parallel
            ? std::max<std::size_t>(1, result.exact_plan->job_count)
            : std::size_t{1})
        : useful_concurrency(result);
    const bool exact = deterministic;
    const std::size_t requested = preferred;
    const std::size_t minimum = exact ? requested : 1;
    const std::size_t maximum = exact ? requested : ceiling;

    ResourceDecisionReport report;
    report.operation_identity = result.descriptor.operation;
    report.runtime_fingerprint = result.state->runtime_fingerprint.hash;
    report.governor_fingerprint = result.context.resource_governor->fingerprint();
    report.process_cpu_budget = result.context.resource_governor->cpu_budget();
    report.runtime_worker_ceiling = result.state->options.maximum_workers;
    report.requested_workers = requested;
    report.minimum_workers = minimum;
    report.preferred_workers = preferred;
    report.maximum_workers = maximum;
    report.exact_grant_required = exact;
    report.wait_policy = result.state->options.lease_wait_policy;
    report.nesting_depth = result.context.depth;
    report.deterministic_requirement = deterministic;
    report.scheduler = result.exact_plan
        ? runtime_name(result.exact_plan->engine)
        : runtime_name(result.state->configuration->execution_engine);
    report.provider = result.descriptor.provider;

    if (result.context.resource_lease)
    {
        const std::size_t parent_grant =
            lease_control_granted_workers(result.context.resource_lease);
        if (exact && parent_grant < requested)
            throw std::runtime_error(
                "SmartParallel deterministic/nested resource admission cannot satisfy the exact worker grant before output mutation");
        report.granted_workers = std::min(requested, parent_grant);
        report.scheduler_concurrency_cap = std::max<std::size_t>(1, report.granted_workers);
        report.admission_status = LeaseAcquireStatus::Granted;
        report.nested_mode = report.granted_workers <= 1
            ? NestedLeaseMode::SequentialWithinParent : NestedLeaseMode::ReuseParent;
        report.lease_identity = lease_control_identity(result.context.resource_lease);
        report.parent_lease_identity = report.lease_identity;
        report.rejection_or_restriction_reason = "nested operation reused the parent lease";
        result.state->nested_lease_reuses.fetch_add(1, std::memory_order_relaxed);
    }
    else
    {
        LeaseRequest request;
        request.requested_workers = requested;
        request.minimum_workers = minimum;
        request.preferred_workers = preferred;
        request.maximum_workers = maximum;
        request.exact_grant_required = exact;
        request.wait_policy = result.state->options.lease_wait_policy;
        request.operation_identity = result.descriptor.operation;
        request.runtime_fingerprint = result.state->runtime_fingerprint.hash;
        if (request.wait_policy == LeaseWaitPolicy::WaitUntilDeadline)
        {
            if (result.state->options.lease_timeout.count() <= 0)
                throw std::invalid_argument(
                    "SmartParallel WaitUntilDeadline Runtime requires a positive lease_timeout");
            request.deadline = std::chrono::steady_clock::now()
                + result.state->options.lease_timeout;
        }
        result.state->lease_requests.fetch_add(1, std::memory_order_relaxed);
        LeaseAcquireResult acquired = result.context.resource_governor->acquire(request);
        report.admission_status = acquired.status;
        report.wait_duration = acquired.wait_duration;
        report.granted_workers = acquired.granted_workers;
        report.scheduler_concurrency_cap = acquired.granted_workers;
        report.rejection_or_restriction_reason = acquired.reason;
        if (acquired.wait_duration.count() > 0)
            result.state->lease_waits.fetch_add(1, std::memory_order_relaxed);
        if (!acquired)
        {
            result.state->lease_rejections.fetch_add(1, std::memory_order_relaxed);
            report.stable_fingerprint = sha256_hex(resource_identity(report));
            {
                std::lock_guard<std::mutex> lock(result.state->resource_report_mutex);
                result.state->last_resource_report = report;
            }
            throw std::runtime_error(
                std::string("SmartParallel resource admission failed before output mutation: ")
                + lease_acquire_status_name(acquired.status) + ": " + acquired.reason);
        }
        result.state->lease_grants.fetch_add(1, std::memory_order_relaxed);
        report.lease_identity = acquired.lease.identity();
        if (!deterministic && result.exact_plan
            && result.exact_plan->job_count != acquired.granted_workers)
        {
            result.exact_plan->job_count =
                std::max<std::size_t>(1, acquired.granted_workers);
            result.exact_plan->parallel = result.exact_plan->job_count > 1;
            if (!result.exact_plan->parallel)
            {
                result.exact_plan->strategy = ExecutionStrategy::Sequential;
                result.exact_plan->chunk_size = 0;
            }
        }
        result.resource_lease = std::move(acquired.lease);
        result.context.resource_lease = execution_lease_control(result.resource_lease);
    }

    report.stable_fingerprint = sha256_hex(resource_identity(report));
    result.resource_decision = report;
    result.context.resource_decision = report;
    result.context.concurrency_budget = std::max<std::size_t>(1, report.granted_workers);
    result.context.inherited_concurrency_budget = result.context.concurrency_budget;
    {
        std::lock_guard<std::mutex> lock(result.state->resource_report_mutex);
        result.state->last_resource_report = report;
    }
}
}

std::string exact_workload_fingerprint(const SemanticOperationDescriptor& descriptor)
{
    std::ostringstream out;
    out<<"operation="<<descriptor.operation<<";semantic_version="<<descriptor.operation_semantic_version
       <<";element_type="<<descriptor.element_type
       <<";numerical="<<numerical_policy_name(descriptor.numerical_policy)
       <<";extents="<<vector_identity(descriptor.extents)
       <<";strides="<<vector_identity(descriptor.strides)
       <<";layout="<<descriptor.layout<<";boundary="<<descriptor.boundary_mode
       <<";in_place="<<(descriptor.in_place?1:0)
       <<";constants="<<descriptor.semantic_constants;
    return sha256_hex(out.str());
}

std::string floating_value_identity(const void* value, std::size_t size)
{
    const auto* bytes=static_cast<const unsigned char*>(value);
    std::ostringstream out;
    out<<std::hex<<std::setfill('0');
    for(std::size_t index=0;index<size;++index)out<<std::setw(2)<<static_cast<unsigned>(bytes[index]);
    return out.str();
}

std::optional<ExecutionPlan> generic_context_execution_plan(const ExecutionContext& context)
{
    if(!context.runtime_state)return std::nullopt;
    const auto state=std::static_pointer_cast<RuntimeState>(context.runtime_state);
    state->operation_calls.fetch_add(1,std::memory_order_relaxed);
    if(state->options.execution_mode!=ExecutionMode::Deterministic)return std::nullopt;
    if(state->options.profile_access!=ProfileAccess::Disabled)
        throw std::runtime_error("SmartParallel does not persist arbitrary callbacks; Deterministic generic algorithms require ProfileAccess::Disabled and an explicit scheduler");
    state->deterministic_replays.fetch_add(1,std::memory_order_relaxed);
    return forced_runtime_plan(*state);
}

PreparedSemanticOperation prepare_semantic_operation(const ExecutionContext& context,
                                                      SemanticOperationDescriptor descriptor)
{
    PreparedSemanticOperation result;
    result.context=context;
    result.descriptor=std::move(descriptor);
    result.forced_route=!result.descriptor.forced_route.empty();
    result.workload_fingerprint=exact_workload_fingerprint(result.descriptor);
    if(!context.runtime_state)return result;
    result.state=std::static_pointer_cast<RuntimeState>(context.runtime_state);
    result.state->operation_calls.fetch_add(1,std::memory_order_relaxed);

    std::optional<OperationProfile> exact;
    {
        std::lock_guard<std::mutex> lock(result.state->profiles_mutex);
        if(const auto* found=result.state->profiles.find_exact(result.descriptor.operation,result.workload_fingerprint,result.descriptor.numerical_policy))exact=*found;
    }

    const bool deterministic=result.state->options.execution_mode==ExecutionMode::Deterministic;
    if(deterministic)
    {
        if(result.state->options.profile_access==ProfileAccess::Disabled)
        {
            result.exact_plan=forced_runtime_plan(*result.state);
            result.forced_route=true;
        }
        else
        {
            if(!exact)throw std::runtime_error("SmartParallel Deterministic execution requires an exact Approved profile before output mutation");
            const auto report=compatibility(*result.state,result.descriptor,result.workload_fingerprint,*exact,true);
            if(!report.compatible)throw std::runtime_error("SmartParallel Deterministic profile compatibility failure before output mutation: "+summarize_issues(report));
            result.profile=*exact;
            result.exact_plan=exact->execution_plan;
            result.deterministic_replay=true;
        }
        result.state->deterministic_replays.fetch_add(1,std::memory_order_relaxed);
        admit_resources(result);
        return result;
    }

    if(exact && result.state->profiles_loaded_from_file)
    {
        const auto report=compatibility(*result.state,result.descriptor,result.workload_fingerprint,*exact,false);
        if(report.compatible)
        {
            std::lock_guard<std::mutex> lock(result.state->profiles_mutex);
            const std::string key=exact->entry_hash.empty()?result.workload_fingerprint:exact->entry_hash;
            if(result.state->warm_started_entries.insert(key).second)
            {
                result.profile=*exact;
                result.exact_plan=exact->execution_plan;
                result.warm_start=true;
                result.state->adaptive_warm_starts.fetch_add(1,std::memory_order_relaxed);
                admit_resources(result);
                return result;
            }
        }
    }
    result.state->adaptive_cold_starts.fetch_add(1,std::memory_order_relaxed);
    admit_resources(result);
    return result;
}

SemanticOperationScope::SemanticOperationScope(const PreparedSemanticOperation& prepared)
{
    if(prepared.context.runtime_state)
        context_scope_=std::make_unique<ExecutionContextScope>(prepared.context);
    if(prepared.exact_plan)
        forced_scope_=std::make_unique<ForcedExecutionPlanScope>(&*prepared.exact_plan);
}
SemanticOperationScope::~SemanticOperationScope()=default;

void complete_semantic_operation(PreparedSemanticOperation& prepared,
                                 const std::string& selected_route,
                                 const std::string& simd_kernel,
                                 const std::string& provider,
                                 const std::string& provider_version)
{
    if(!prepared.state)return;
    const NumericalExecutionReport numerical=global_last_numerical_execution_report();
    const DecisionReport decision=global_last_decision_report();
    ExecutionPlan plan=prepared.exact_plan?*prepared.exact_plan:decision.plan;
    if (prepared.state->options.execution_mode == ExecutionMode::Adaptive
        && prepared.resource_decision.granted_workers > 0
        && plan.parallel
        && plan.job_count > prepared.resource_decision.granted_workers)
    {
        // Preserve the scheduler that actually executed. A governed
        // ThreadPool, StaticThread, or oneTBB route may legitimately run with
        // a one-participant cap; rewriting that observed route to Sequential
        // would corrupt the persisted deterministic scheduler identity.
        plan.job_count = prepared.resource_decision.granted_workers;
    }
    if(!plan.parallel)
    {
        plan.strategy=ExecutionStrategy::Sequential;
        plan.engine=ExecutionEngineType::Auto;
        plan.job_count=1;
    }

    prepared.resource_decision.scheduler = runtime_name(plan.parallel ? plan.engine : ExecutionEngineType::Auto);
    prepared.resource_decision.provider = provider;
    prepared.resource_decision.scheduler_concurrency_cap = plan.parallel
        ? std::min(std::max<std::size_t>(1, plan.job_count),
                   std::max<std::size_t>(1, prepared.resource_decision.granted_workers))
        : std::size_t{1};
    prepared.resource_decision.observed_participating_threads =
        prepared.resource_decision.scheduler_concurrency_cap;
    if (provider == "opencv")
    {
        prepared.resource_decision.provider_control_scope = ControlScope::ProcessGlobal;
        prepared.resource_decision.provider_control_strength = ControlStrength::SerializedProcessGlobal;
        prepared.resource_decision.provider_serialized = true;
    }
    else if (plan.engine == ExecutionEngineType::OneTbb)
    {
        prepared.resource_decision.provider_control_scope = ControlScope::PerTask;
        prepared.resource_decision.provider_control_strength = ControlStrength::UpperBound;
        prepared.resource_decision.provider_serialized = false;
    }
    else
    {
        prepared.resource_decision.provider_control_scope = ControlScope::PerCall;
        prepared.resource_decision.provider_control_strength = ControlStrength::Exact;
        prepared.resource_decision.provider_serialized = false;
    }
    prepared.resource_decision.stable_fingerprint =
        resource_decision_fingerprint(prepared.resource_decision);

    std::string database_hash;
    std::string entry_hash;
    ProfileStatus status=ProfileStatus::Candidate;
    if(prepared.profile)
    {
        entry_hash=prepared.profile->entry_hash;
        status=prepared.profile->status;
    }

    if(prepared.state->options.execution_mode==ExecutionMode::Adaptive
       && prepared.state->options.profile_access==ProfileAccess::ReadWrite
       && !prepared.warm_start && !prepared.forced_route)
    {
        std::lock_guard<std::mutex> lock(prepared.state->profiles_mutex);
        OperationProfile* entry=prepared.state->profiles.find_exact(prepared.descriptor.operation,prepared.workload_fingerprint,prepared.descriptor.numerical_policy);
        if(!entry)
        {
            OperationProfile created;
            created.operation=prepared.descriptor.operation;
            created.operation_semantic_version=prepared.descriptor.operation_semantic_version;
            created.element_type=prepared.descriptor.element_type;
            created.numerical_policy=prepared.descriptor.numerical_policy;
            created.workload.extents=prepared.descriptor.extents;
            created.workload.strides=prepared.descriptor.strides;
            created.workload.layout=prepared.descriptor.layout;
            created.workload.boundary_mode=prepared.descriptor.boundary_mode;
            created.workload.in_place=prepared.descriptor.in_place;
            created.workload.semantic_constants=prepared.descriptor.semantic_constants;
            created.workload.exact_fingerprint=prepared.workload_fingerprint;
            created.environment=prepared.state->environment;
            created.status=ProfileStatus::Candidate;
            prepared.state->profiles.entries.push_back(std::move(created));
            entry=&prepared.state->profiles.entries.back();
        }
        entry->evaluation_order=evaluation_order_name(numerical.evaluation_order);
        entry->accumulation_algorithm=accumulation_name(numerical.accumulation);
        entry->canonical_plan=numerical.canonical_plan;
        entry->implementation_route=selected_route;
        entry->execution_plan=plan;
        entry->exact_worker_budget=prepared.resource_decision.granted_workers == 0
            ? prepared.state->configuration->nested_root_concurrency_budget
            : prepared.resource_decision.granted_workers;
        entry->resource_contract_present=true;
        // Calibration may use a flexible admission request, but the persisted
        // execution plan is an exact deployment contract once approved.
        entry->requested_workers=entry->exact_worker_budget;
        entry->minimum_workers=entry->exact_worker_budget;
        entry->preferred_workers=entry->exact_worker_budget;
        entry->maximum_workers=entry->exact_worker_budget;
        entry->granted_workers=entry->exact_worker_budget;
        entry->scheduler_concurrency_cap=plan.parallel
            ? std::max<std::size_t>(1, plan.job_count) : std::size_t{1};
        entry->observed_participating_threads=prepared.resource_decision.observed_participating_threads;
        entry->exact_grant_required=true;
        entry->lease_wait_policy=prepared.resource_decision.wait_policy;
        entry->nested_lease_mode=prepared.resource_decision.nested_mode;
        entry->provider_control_scope=prepared.resource_decision.provider_control_scope;
        entry->provider_control_strength=prepared.resource_decision.provider_control_strength;
        entry->provider_serialized=prepared.resource_decision.provider_serialized;
        entry->simd_kernel=simd_kernel;
        entry->provider=provider;
        entry->provider_version=provider_version;
        entry->numerical_capability=numerical.capability_satisfied?"satisfied":"unsatisfied";
        entry->evidence.sample_count+=1;
        entry->evidence.confidence=std::min(1.0,0.5+0.1*static_cast<double>(entry->evidence.sample_count));
        entry->evidence.route_authenticated=numerical.route_authenticated
            || selected_route.find("native_")==0 || selected_route=="sequential"
            || selected_route=="opencv";
        entry->evidence.numerical_capability_passed=numerical.capability_satisfied;
        entry->evidence.correctness_passed=true;
        entry->evidence.holdout_passed=entry->evidence.sample_count>=2;
        if(entry->evidence.created_utc.empty())entry->evidence.created_utc=utc_now();
        refresh_database(prepared.state->profiles);
        if(const auto* refreshed=prepared.state->profiles.find_exact(prepared.descriptor.operation,prepared.workload_fingerprint,prepared.descriptor.numerical_policy))
        {
            entry_hash=refreshed->entry_hash;
            status=refreshed->status;
        }
        database_hash=prepared.state->profiles.content_hash;
        prepared.state->profile_mutations.fetch_add(1,std::memory_order_relaxed);
        prepared.state->learning_samples.fetch_add(1,std::memory_order_relaxed);
        if(global_last_parallel_for_profile_diagnostics().profiling_ms>0.0)
            prepared.state->timing_probes.fetch_add(1,std::memory_order_relaxed);
    }
    else
    {
        std::lock_guard<std::mutex> lock(prepared.state->profiles_mutex);
        database_hash=prepared.state->profiles.content_hash;
    }

    OperationExecutionFingerprint fingerprint;
    fingerprint.runtime_fingerprint=prepared.state->runtime_fingerprint.hash;
    fingerprint.operation=prepared.descriptor.operation;
    fingerprint.operation_semantic_version=prepared.descriptor.operation_semantic_version;
    fingerprint.workload_fingerprint=prepared.workload_fingerprint;
    fingerprint.numerical_policy=prepared.descriptor.numerical_policy;
    fingerprint.evaluation_order=evaluation_order_name(numerical.evaluation_order);
    fingerprint.accumulation_algorithm=accumulation_name(numerical.accumulation);
    fingerprint.canonical_plan=numerical.canonical_plan;
    fingerprint.execution_mode=prepared.state->options.execution_mode;
    fingerprint.profile_access=prepared.state->options.profile_access;
    fingerprint.profile_database_hash=database_hash;
    fingerprint.profile_entry_hash=entry_hash;
    fingerprint.profile_status=status;
    fingerprint.selected_route=selected_route;
    fingerprint.selected_scheduler=plan.parallel?plan.engine:ExecutionEngineType::Auto;
    fingerprint.worker_budget=prepared.state->configuration->nested_root_concurrency_budget;
    fingerprint.requested_workers=prepared.resource_decision.requested_workers == 0
        ? fingerprint.worker_budget : prepared.resource_decision.requested_workers;
    fingerprint.minimum_workers=prepared.resource_decision.minimum_workers == 0
        ? std::size_t{1} : prepared.resource_decision.minimum_workers;
    fingerprint.preferred_workers=prepared.resource_decision.preferred_workers == 0
        ? fingerprint.requested_workers : prepared.resource_decision.preferred_workers;
    fingerprint.maximum_workers=prepared.resource_decision.maximum_workers == 0
        ? fingerprint.requested_workers : prepared.resource_decision.maximum_workers;
    fingerprint.granted_workers=prepared.resource_decision.granted_workers == 0
        ? std::size_t{1} : prepared.resource_decision.granted_workers;
    fingerprint.scheduler_concurrency_cap=plan.parallel
        ? std::min(std::max<std::size_t>(1,plan.job_count), fingerprint.granted_workers)
        : std::size_t{1};
    if (!plan.parallel)
    {
        fingerprint.actual_worker_count = 1;
    }
    else
    {
        const auto& backend_result = smart::last_backend_execution_result();
        fingerprint.actual_worker_count =
            backend_result.executed && backend_result.backend == fingerprint.selected_scheduler
            ? std::max<std::size_t>(1, backend_result.observed_participating_threads)
            : fingerprint.scheduler_concurrency_cap;
    }
    fingerprint.exact_grant_required=prepared.resource_decision.exact_grant_required;
    fingerprint.lease_wait_policy=prepared.resource_decision.wait_policy;
    fingerprint.nested_lease_mode=prepared.resource_decision.nested_mode;
    fingerprint.provider_control_scope=prepared.resource_decision.provider_control_scope;
    fingerprint.provider_control_strength=prepared.resource_decision.provider_control_strength;
    fingerprint.provider_serialized=prepared.resource_decision.provider_serialized;
    fingerprint.resource_fingerprint=prepared.resource_decision.stable_fingerprint;
    fingerprint.simd_kernel=simd_kernel;
    fingerprint.provider=provider;
    fingerprint.provider_version=provider_version;
    prepared.resource_decision.provider = provider;
    prepared.resource_decision.scheduler = runtime_name(fingerprint.selected_scheduler);
    prepared.resource_decision.scheduler_concurrency_cap = fingerprint.scheduler_concurrency_cap;
    prepared.resource_decision.observed_participating_threads = fingerprint.actual_worker_count;
    if (provider == "opencv")
    {
        prepared.resource_decision.provider_control_scope = ControlScope::ProcessGlobal;
        prepared.resource_decision.provider_control_strength = ControlStrength::SerializedProcessGlobal;
        prepared.resource_decision.provider_serialized = true;
    }
    else if (fingerprint.selected_scheduler == ExecutionEngineType::OneTbb)
    {
        prepared.resource_decision.provider_control_scope = ControlScope::PerTask;
        prepared.resource_decision.provider_control_strength = ControlStrength::UpperBound;
    }
    prepared.resource_decision.stable_fingerprint = sha256_hex(resource_identity(prepared.resource_decision));
    fingerprint.provider_control_scope = prepared.resource_decision.provider_control_scope;
    fingerprint.provider_control_strength = prepared.resource_decision.provider_control_strength;
    fingerprint.provider_serialized = prepared.resource_decision.provider_serialized;
    fingerprint.resource_fingerprint = prepared.resource_decision.stable_fingerprint;
    {
        std::lock_guard<std::mutex> lock(prepared.state->resource_report_mutex);
        prepared.state->last_resource_report = prepared.resource_decision;
    }
    fingerprint.forced_route=prepared.forced_route;
    fingerprint.warm_start=prepared.warm_start;
    fingerprint.deterministic_replay=prepared.deterministic_replay;
    fingerprint.hash=sha256_hex(fingerprint_identity(fingerprint));
    {
        std::lock_guard<std::mutex> lock(prepared.state->fingerprint_mutex);
        prepared.state->last_operation=std::move(fingerprint);
    }
}
} // namespace smart::detail
