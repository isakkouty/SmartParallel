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
    if(entry.exact_worker_budget!=runtime_budget)
        add_mismatch(report,CompatibilityIssueCode::WorkerBudgetMismatch,"worker_budget",std::to_string(runtime_budget),std::to_string(entry.exact_worker_budget));
    if(entry.execution_plan.parallel&&!execution_backend_available(entry.execution_plan.engine))
        add_mismatch(report,CompatibilityIssueCode::SchedulerUnavailable,"scheduler","available",runtime_name(entry.execution_plan.engine));
    if(entry.execution_plan.parallel && entry.execution_plan.job_count!=entry.exact_worker_budget)
        add_mismatch(report,CompatibilityIssueCode::WorkerBudgetMismatch,"execution_plan.job_count",std::to_string(entry.exact_worker_budget),std::to_string(entry.execution_plan.job_count));
    if(entry.actual_worker_policy!="exact")
        add_mismatch(report,CompatibilityIssueCode::ProviderSettingMismatch,"actual_worker_policy","exact",entry.actual_worker_policy);
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
       <<";actual_workers="<<f.actual_worker_count
       <<";simd="<<f.simd_kernel<<";provider="<<f.provider
       <<";provider_version="<<f.provider_version
       <<";forced="<<(f.forced_route?1:0)
       <<";warm="<<(f.warm_start?1:0)
       <<";deterministic="<<(f.deterministic_replay?1:0);
    return out.str();
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
                return result;
            }
        }
    }
    result.state->adaptive_cold_starts.fetch_add(1,std::memory_order_relaxed);
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
    if(!plan.parallel)
    {
        plan.strategy=ExecutionStrategy::Sequential;
        plan.engine=ExecutionEngineType::Auto;
        plan.job_count=1;
    }

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
        entry->exact_worker_budget=prepared.state->configuration->nested_root_concurrency_budget;
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
    fingerprint.actual_worker_count=plan.parallel?std::max<std::size_t>(1,plan.job_count):1;
    fingerprint.simd_kernel=simd_kernel;
    fingerprint.provider=provider;
    fingerprint.provider_version=provider_version;
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
