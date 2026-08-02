#include <smart/runtime/profile.hpp>
#include <smart/execution/runtime_capabilities.hpp>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <map>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#  define NOMINMAX
#  include <windows.h>
#endif

namespace smart
{
namespace
{
constexpr std::size_t maximum_profile_bytes = 16u * 1024u * 1024u;
constexpr std::size_t maximum_json_depth = 64;
constexpr std::size_t maximum_json_string = 1024u * 1024u;
constexpr std::size_t maximum_profile_entries = 4096;
constexpr std::size_t maximum_vector_items = 16384;

struct Json
{
    enum class Type { Null, Boolean, Number, String, Array, Object };
    Type type = Type::Null;
    bool boolean = false;
    std::string text;
    std::vector<Json> array;
    std::map<std::string, Json> object;

    static Json string(std::string value) { Json j; j.type=Type::String; j.text=std::move(value); return j; }
    static Json number(std::string value) { Json j; j.type=Type::Number; j.text=std::move(value); return j; }
    static Json boolean_value(bool value) { Json j; j.type=Type::Boolean; j.boolean=value; return j; }
    static Json array_value() { Json j; j.type=Type::Array; return j; }
    static Json object_value() { Json j; j.type=Type::Object; return j; }
};

class JsonParser
{
  public:
    explicit JsonParser(const std::string& input) : input_(input)
    {
        if (input.size() > maximum_profile_bytes)
            fail("profile exceeds the 16 MiB parser limit");
    }

    Json parse()
    {
        skip_space();
        Json value = parse_value(0);
        skip_space();
        if (position_ != input_.size())
            fail("trailing data after JSON value");
        return value;
    }

  private:
    [[noreturn]] void fail(const std::string& reason) const
    {
        throw std::runtime_error("SmartParallel profile JSON error at byte "
                                 + std::to_string(position_) + ": " + reason);
    }

    void skip_space()
    {
        while (position_ < input_.size())
        {
            const char c = input_[position_];
            if (c!=' ' && c!='\n' && c!='\r' && c!='\t') break;
            ++position_;
        }
    }

    Json parse_value(std::size_t depth)
    {
        if (depth > maximum_json_depth) fail("maximum nesting depth exceeded");
        skip_space();
        if (position_ >= input_.size()) fail("unexpected end of input");
        const char c = input_[position_];
        if (c == '{') return parse_object(depth + 1);
        if (c == '[') return parse_array(depth + 1);
        if (c == '"') return Json::string(parse_string());
        if (c == 't' && consume("true")) return Json::boolean_value(true);
        if (c == 'f' && consume("false")) return Json::boolean_value(false);
        if (c == 'n' && consume("null")) return Json{};
        if (c == '-' || (c >= '0' && c <= '9')) return Json::number(parse_number());
        fail("invalid value");
    }

    bool consume(const char* literal)
    {
        const std::size_t length = std::strlen(literal);
        if (input_.compare(position_, length, literal) != 0) return false;
        position_ += length;
        return true;
    }

    Json parse_object(std::size_t depth)
    {
        Json result = Json::object_value();
        ++position_;
        skip_space();
        if (position_ < input_.size() && input_[position_] == '}') { ++position_; return result; }
        while (true)
        {
            skip_space();
            if (position_ >= input_.size() || input_[position_] != '"') fail("object key must be a string");
            std::string key = parse_string();
            skip_space();
            if (position_ >= input_.size() || input_[position_] != ':') fail("missing ':' after object key");
            ++position_;
            Json value = parse_value(depth);
            if (!result.object.emplace(key, std::move(value)).second)
                fail("duplicate object key '" + key + "'");
            skip_space();
            if (position_ >= input_.size()) fail("unterminated object");
            const char delimiter = input_[position_++];
            if (delimiter == '}') break;
            if (delimiter != ',') fail("expected ',' or '}'");
        }
        return result;
    }

    Json parse_array(std::size_t depth)
    {
        Json result = Json::array_value();
        ++position_;
        skip_space();
        if (position_ < input_.size() && input_[position_] == ']') { ++position_; return result; }
        while (true)
        {
            if (result.array.size() >= maximum_vector_items) fail("array item limit exceeded");
            result.array.push_back(parse_value(depth));
            skip_space();
            if (position_ >= input_.size()) fail("unterminated array");
            const char delimiter = input_[position_++];
            if (delimiter == ']') break;
            if (delimiter != ',') fail("expected ',' or ']'");
        }
        return result;
    }

    static void append_utf8(std::string& output, std::uint32_t codepoint)
    {
        if (codepoint <= 0x7fu) output.push_back(static_cast<char>(codepoint));
        else if (codepoint <= 0x7ffu)
        {
            output.push_back(static_cast<char>(0xc0u | (codepoint >> 6)));
            output.push_back(static_cast<char>(0x80u | (codepoint & 0x3fu)));
        }
        else if (codepoint <= 0xffffu)
        {
            output.push_back(static_cast<char>(0xe0u | (codepoint >> 12)));
            output.push_back(static_cast<char>(0x80u | ((codepoint >> 6) & 0x3fu)));
            output.push_back(static_cast<char>(0x80u | (codepoint & 0x3fu)));
        }
        else
        {
            output.push_back(static_cast<char>(0xf0u | (codepoint >> 18)));
            output.push_back(static_cast<char>(0x80u | ((codepoint >> 12) & 0x3fu)));
            output.push_back(static_cast<char>(0x80u | ((codepoint >> 6) & 0x3fu)));
            output.push_back(static_cast<char>(0x80u | (codepoint & 0x3fu)));
        }
    }

    std::uint32_t parse_hex4()
    {
        if (position_ + 4 > input_.size()) fail("truncated Unicode escape");
        std::uint32_t value = 0;
        for (int index = 0; index < 4; ++index)
        {
            const char c = input_[position_++];
            value <<= 4;
            if (c >= '0' && c <= '9') value |= static_cast<std::uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') value |= static_cast<std::uint32_t>(10 + c - 'a');
            else if (c >= 'A' && c <= 'F') value |= static_cast<std::uint32_t>(10 + c - 'A');
            else fail("invalid Unicode escape");
        }
        return value;
    }

    std::string parse_string()
    {
        ++position_;
        std::string result;
        while (position_ < input_.size())
        {
            const unsigned char c = static_cast<unsigned char>(input_[position_++]);
            if (c == '"') return result;
            if (c < 0x20u) fail("unescaped control character in string");
            if (c == '\\')
            {
                if (position_ >= input_.size()) fail("truncated string escape");
                const char escape = input_[position_++];
                switch (escape)
                {
                    case '"': result.push_back('"'); break;
                    case '\\': result.push_back('\\'); break;
                    case '/': result.push_back('/'); break;
                    case 'b': result.push_back('\b'); break;
                    case 'f': result.push_back('\f'); break;
                    case 'n': result.push_back('\n'); break;
                    case 'r': result.push_back('\r'); break;
                    case 't': result.push_back('\t'); break;
                    case 'u':
                    {
                        std::uint32_t codepoint = parse_hex4();
                        if (codepoint >= 0xd800u && codepoint <= 0xdbffu)
                        {
                            if (position_ + 2 > input_.size() || input_[position_] != '\\' || input_[position_+1] != 'u')
                                fail("unpaired high surrogate");
                            position_ += 2;
                            const std::uint32_t low = parse_hex4();
                            if (low < 0xdc00u || low > 0xdfffu) fail("invalid low surrogate");
                            codepoint = 0x10000u + ((codepoint - 0xd800u) << 10) + (low - 0xdc00u);
                        }
                        else if (codepoint >= 0xdc00u && codepoint <= 0xdfffu)
                            fail("unpaired low surrogate");
                        append_utf8(result, codepoint);
                        break;
                    }
                    default: fail("invalid string escape");
                }
            }
            else
            {
                if (c >= 0x80u)
                {
                    // Validate and copy one UTF-8 sequence.
                    int continuation = 0;
                    std::uint32_t codepoint = 0;
                    if ((c & 0xe0u) == 0xc0u) { continuation=1; codepoint=c & 0x1fu; if (codepoint < 2) fail("overlong UTF-8"); }
                    else if ((c & 0xf0u) == 0xe0u) { continuation=2; codepoint=c & 0x0fu; }
                    else if ((c & 0xf8u) == 0xf0u) { continuation=3; codepoint=c & 0x07u; }
                    else fail("invalid UTF-8 lead byte");
                    result.push_back(static_cast<char>(c));
                    for (int index=0; index<continuation; ++index)
                    {
                        if (position_ >= input_.size()) fail("truncated UTF-8");
                        const unsigned char next = static_cast<unsigned char>(input_[position_++]);
                        if ((next & 0xc0u) != 0x80u) fail("invalid UTF-8 continuation byte");
                        result.push_back(static_cast<char>(next));
                        codepoint = (codepoint << 6) | (next & 0x3fu);
                    }
                    if ((continuation==2 && codepoint < 0x800u)
                        || (continuation==3 && codepoint < 0x10000u)
                        || codepoint > 0x10ffffu
                        || (codepoint >= 0xd800u && codepoint <= 0xdfffu))
                        fail("invalid UTF-8 code point");
                }
                else result.push_back(static_cast<char>(c));
            }
            if (result.size() > maximum_json_string) fail("string length limit exceeded");
        }
        fail("unterminated string");
    }

    std::string parse_number()
    {
        const std::size_t begin = position_;
        if (input_[position_] == '-') ++position_;
        if (position_ >= input_.size()) fail("truncated number");
        if (input_[position_] == '0') ++position_;
        else
        {
            if (input_[position_] < '1' || input_[position_] > '9') fail("invalid integer part");
            while (position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9') ++position_;
        }
        if (position_ < input_.size() && input_[position_] == '.')
        {
            ++position_;
            const std::size_t fraction = position_;
            while (position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9') ++position_;
            if (position_ == fraction) fail("fraction requires digits");
        }
        if (position_ < input_.size() && (input_[position_] == 'e' || input_[position_] == 'E'))
        {
            ++position_;
            if (position_ < input_.size() && (input_[position_] == '+' || input_[position_] == '-')) ++position_;
            const std::size_t exponent = position_;
            while (position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9') ++position_;
            if (position_ == exponent) fail("exponent requires digits");
        }
        const std::string token = input_.substr(begin, position_ - begin);
        char* end = nullptr;
        errno = 0;
        const double value = std::strtod(token.c_str(), &end);
        if (errno == ERANGE || end != token.c_str() + token.size() || !std::isfinite(value))
            fail("number is non-finite or out of range");
        return token;
    }

    const std::string& input_;
    std::size_t position_ = 0;
};

std::string escape_json(const std::string& value)
{
    std::ostringstream out;
    out << '"';
    for (const unsigned char c : value)
    {
        switch (c)
        {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\b': out << "\\b"; break;
            case '\f': out << "\\f"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (c < 0x20u)
                    out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<unsigned>(c) << std::dec;
                else out << static_cast<char>(c);
        }
    }
    out << '"';
    return out.str();
}

std::string canonical_json(const Json& value)
{
    switch (value.type)
    {
        case Json::Type::Null: return "null";
        case Json::Type::Boolean: return value.boolean ? "true" : "false";
        case Json::Type::Number: return value.text;
        case Json::Type::String: return escape_json(value.text);
        case Json::Type::Array:
        {
            std::string result = "[";
            for (std::size_t index=0; index<value.array.size(); ++index)
            {
                if (index) result += ',';
                result += canonical_json(value.array[index]);
            }
            result += ']';
            return result;
        }
        case Json::Type::Object:
        {
            std::string result = "{";
            bool first = true;
            for (const auto& pair : value.object)
            {
                if (!first) result += ',';
                first = false;
                result += escape_json(pair.first);
                result += ':';
                result += canonical_json(pair.second);
            }
            result += '}';
            return result;
        }
    }
    throw std::logic_error("unknown JSON type");
}

std::string finite_number(double value)
{
    if (!std::isfinite(value)) throw std::invalid_argument("profile numbers must be finite");
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(std::numeric_limits<double>::max_digits10) << value;
    return out.str();
}

Json number(std::size_t value) { return Json::number(std::to_string(value)); }
Json number(double value) { return Json::number(finite_number(value)); }
Json array_of_sizes(const std::vector<std::size_t>& values)
{
    Json result=Json::array_value();
    for (auto v:values) result.array.push_back(number(v));
    return result;
}

Json environment_json(const ProfileEnvironment& e)
{
    Json j=Json::object_value();
    j.object["application_build_identifier"]=Json::string(e.application_build_identifier);
    j.object["architecture"]=Json::string(e.architecture);
    j.object["build_type"]=Json::string(e.build_type);
    j.object["compiler_identity"]=Json::string(e.compiler_identity);
    j.object["compiler_version"]=Json::string(e.compiler_version);
    j.object["cpu_identity"]=Json::string(e.cpu_identity);
    j.object["endianness"]=Json::string(e.endianness);
    j.object["feature_macros"]=Json::string(e.feature_macros);
    j.object["floating_point_environment"]=Json::string(e.floating_point_environment);
    j.object["opencv_version"]=Json::string(e.opencv_version);
    j.object["os_family"]=Json::string(e.os_family);
    j.object["pointer_width"]=number(e.pointer_width);
    j.object["required_isa"]=Json::string(e.required_isa);
    j.object["smartparallel_build_fingerprint"]=Json::string(e.smartparallel_build_fingerprint);
    j.object["smartparallel_version"]=Json::string(e.smartparallel_version);
    j.object["standard_library_identity"]=Json::string(e.standard_library_identity);
    j.object["tbb_version"]=Json::string(e.tbb_version);
    return j;
}

Json workload_json(const ProfileWorkloadIdentity& w)
{
    Json j=Json::object_value();
    j.object["boundary_mode"]=Json::string(w.boundary_mode);
    j.object["exact_fingerprint"]=Json::string(w.exact_fingerprint);
    j.object["extents"]=array_of_sizes(w.extents);
    j.object["in_place"]=Json::boolean_value(w.in_place);
    j.object["layout"]=Json::string(w.layout);
    j.object["semantic_constants"]=Json::string(w.semantic_constants);
    j.object["strides"]=array_of_sizes(w.strides);
    return j;
}

const char* numerical_name(NumericalPolicy p) { return numerical_policy_name(p); }
NumericalPolicy parse_numerical(const std::string& s)
{
    if (s=="Fast") return NumericalPolicy::Fast;
    if (s=="Reproducible") return NumericalPolicy::Reproducible;
    if (s=="Accurate") return NumericalPolicy::Accurate;
    throw std::runtime_error("SmartParallel profile contains invalid numerical policy '"+s+"'");
}
const char* strategy_name(ExecutionStrategy s)
{
    switch(s){case ExecutionStrategy::Sequential:return "sequential";case ExecutionStrategy::StaticChunks:return "static_chunks";case ExecutionStrategy::DynamicChunks:return "dynamic_chunks";}
    return "unknown";
}
ExecutionStrategy parse_strategy(const std::string& s)
{
    if(s=="sequential")return ExecutionStrategy::Sequential;
    if(s=="static_chunks")return ExecutionStrategy::StaticChunks;
    if(s=="dynamic_chunks")return ExecutionStrategy::DynamicChunks;
    throw std::runtime_error("SmartParallel profile contains invalid strategy '"+s+"'");
}
ExecutionEngineType parse_engine(const std::string& s)
{
    if(s=="auto")return ExecutionEngineType::Auto;
    if(s=="thread_pool")return ExecutionEngineType::ThreadPool;
    if(s=="static_thread")return ExecutionEngineType::StaticThread;
    if(s=="one_tbb")return ExecutionEngineType::OneTbb;
    throw std::runtime_error("SmartParallel profile contains invalid scheduler '"+s+"'");
}
LeaseWaitPolicy parse_lease_wait_policy(const std::string& s)
{
    if(s=="fail_immediately")return LeaseWaitPolicy::FailImmediately;
    if(s=="wait")return LeaseWaitPolicy::Wait;
    if(s=="wait_until_deadline")return LeaseWaitPolicy::WaitUntilDeadline;
    throw std::runtime_error("SmartParallel profile contains invalid lease wait policy '"+s+"'");
}
NestedLeaseMode parse_nested_lease_mode(const std::string& s)
{
    if(s=="not_nested")return NestedLeaseMode::NotNested;
    if(s=="reuse_parent")return NestedLeaseMode::ReuseParent;
    if(s=="partition_parent")return NestedLeaseMode::PartitionParent;
    if(s=="sequential_within_parent")return NestedLeaseMode::SequentialWithinParent;
    throw std::runtime_error("SmartParallel profile contains invalid nested lease mode '"+s+"'");
}
ControlScope parse_control_scope(const std::string& s)
{
    if(s=="per_call")return ControlScope::PerCall;
    if(s=="per_thread")return ControlScope::PerThread;
    if(s=="per_task")return ControlScope::PerTask;
    if(s=="process_global")return ControlScope::ProcessGlobal;
    throw std::runtime_error("SmartParallel profile contains invalid provider control scope '"+s+"'");
}
ControlStrength parse_control_strength(const std::string& s)
{
    if(s=="exact")return ControlStrength::Exact;
    if(s=="upper_bound")return ControlStrength::UpperBound;
    if(s=="advisory")return ControlStrength::Advisory;
    if(s=="serialized_process_global")return ControlStrength::SerializedProcessGlobal;
    if(s=="unsupported")return ControlStrength::Unsupported;
    throw std::runtime_error("SmartParallel profile contains invalid provider control strength '"+s+"'");
}

Json entry_json(const OperationProfile& e, bool include_entry_hash)
{
    Json j=Json::object_value();
    j.object["accumulation_algorithm"]=Json::string(e.accumulation_algorithm);
    j.object["actual_worker_policy"]=Json::string(e.actual_worker_policy);
    j.object["candidate_source_hash"]=Json::string(e.candidate_source_hash);
    j.object["canonical_plan"]=Json::string(e.canonical_plan);
    j.object["capability_requirements"]=Json::string(e.capability_requirements);
    j.object["element_type"]=Json::string(e.element_type);
    if(include_entry_hash)j.object["entry_hash"]=Json::string(e.entry_hash);
    j.object["environment"]=environment_json(e.environment);
    Json evidence=Json::object_value();
    evidence.object["confidence"]=number(e.evidence.confidence);
    evidence.object["correctness_passed"]=Json::boolean_value(e.evidence.correctness_passed);
    evidence.object["created_utc"]=Json::string(e.evidence.created_utc);
    evidence.object["expires_utc"]=Json::string(e.evidence.expires_utc);
    evidence.object["holdout_passed"]=Json::boolean_value(e.evidence.holdout_passed);
    evidence.object["median_duration_ms"]=number(e.evidence.median_duration_ms);
    evidence.object["numerical_capability_passed"]=Json::boolean_value(e.evidence.numerical_capability_passed);
    evidence.object["route_authenticated"]=Json::boolean_value(e.evidence.route_authenticated);
    evidence.object["route_switch_count"]=number(e.evidence.route_switch_count);
    evidence.object["sample_count"]=number(e.evidence.sample_count);
    evidence.object["source_calibration_id"]=Json::string(e.evidence.source_calibration_id);
    evidence.object["variability_ms"]=number(e.evidence.variability_ms);
    j.object["evidence"]=std::move(evidence);
    Json plan=Json::object_value();
    plan.object["chunk_size"]=number(e.execution_plan.chunk_size);
    plan.object["engine"]=Json::string(runtime_name(e.execution_plan.engine));
    plan.object["job_count"]=number(e.execution_plan.job_count);
    plan.object["parallel"]=Json::boolean_value(e.execution_plan.parallel);
    plan.object["strategy"]=Json::string(strategy_name(e.execution_plan.strategy));
    j.object["execution_plan"]=std::move(plan);
    j.object["exact_worker_budget"]=number(e.exact_worker_budget);
    if(e.resource_contract_present)
    {
        Json resource=Json::object_value();
        resource.object["exact_grant_required"]=Json::boolean_value(e.exact_grant_required);
        resource.object["granted_workers"]=number(e.granted_workers);
        resource.object["lease_wait_policy"]=Json::string(lease_wait_policy_name(e.lease_wait_policy));
        resource.object["minimum_workers"]=number(e.minimum_workers);
        resource.object["preferred_workers"]=number(e.preferred_workers);
        resource.object["maximum_workers"]=number(e.maximum_workers);
        resource.object["nested_lease_mode"]=Json::string(nested_lease_mode_name(e.nested_lease_mode));
        resource.object["observed_participating_threads"]=number(e.observed_participating_threads);
        resource.object["provider_control_scope"]=Json::string(control_scope_name(e.provider_control_scope));
        resource.object["provider_control_strength"]=Json::string(control_strength_name(e.provider_control_strength));
        resource.object["provider_serialized"]=Json::boolean_value(e.provider_serialized);
        resource.object["requested_workers"]=number(e.requested_workers);
        resource.object["scheduler_concurrency_cap"]=number(e.scheduler_concurrency_cap);
        j.object["resource_contract"]=std::move(resource);
    }
    j.object["evaluation_order"]=Json::string(e.evaluation_order);
    j.object["implementation_route"]=Json::string(e.implementation_route);
    j.object["numerical_capability"]=Json::string(e.numerical_capability);
    j.object["numerical_policy"]=Json::string(numerical_name(e.numerical_policy));
    j.object["operation"]=Json::string(e.operation);
    j.object["operation_semantic_version"]=Json::string(e.operation_semantic_version);
    j.object["plan_semantic_version"]=Json::string(e.plan_semantic_version);
    j.object["profile_status"]=Json::string(profile_status_name(e.status));
    j.object["provider"]=Json::string(e.provider);
    j.object["provider_settings"]=Json::string(e.provider_settings);
    j.object["provider_version"]=Json::string(e.provider_version);
    j.object["simd_kernel"]=Json::string(e.simd_kernel);
    j.object["workload"]=workload_json(e.workload);
    return j;
}

Json database_json(const ProfileDatabase& db, bool include_hash)
{
    Json root=Json::object_value();
    if(include_hash) root.object["content_hash"]=Json::string(db.content_hash);
    root.object["created_utc"]=Json::string(db.created_utc);
    Json entries=Json::array_value();
    entries.array.reserve(db.entries.size());
    for(const auto& source:db.entries)
    {
        OperationProfile entry=source;
        entry.entry_hash=sha256_hex(canonical_json(entry_json(entry,false)));
        entries.array.push_back(entry_json(entry,true));
    }
    root.object["entries"]=std::move(entries);
    root.object["environment"]=environment_json(db.environment);
    root.object["schema_version"]=number(db.schema_version);
    root.object["semantic_version"]=Json::string(db.semantic_version);
    root.object["smartparallel_version"]=Json::string(db.smartparallel_version);
    return root;
}

const Json& required(const Json& object, const char* key, Json::Type type)
{
    if(object.type!=Json::Type::Object) throw std::runtime_error("SmartParallel profile expected an object");
    auto it=object.object.find(key);
    if(it==object.object.end()) throw std::runtime_error(std::string("SmartParallel profile missing required field '")+key+"'");
    if(it->second.type!=type) throw std::runtime_error(std::string("SmartParallel profile field '")+key+"' has the wrong type");
    return it->second;
}
const Json* optional(const Json& object, const char* key, Json::Type type)
{
    if(object.type!=Json::Type::Object) throw std::runtime_error("SmartParallel profile expected an object");
    auto it=object.object.find(key);
    if(it==object.object.end()) return nullptr;
    if(it->second.type!=type) throw std::runtime_error(std::string("SmartParallel profile field '")+key+"' has the wrong type");
    return &it->second;
}
std::string string_value(const Json& o,const char* k){return required(o,k,Json::Type::String).text;}
bool bool_value(const Json& o,const char* k){return required(o,k,Json::Type::Boolean).boolean;}
double double_value(const Json& o,const char* k)
{
    const auto& j=required(o,k,Json::Type::Number); char* e=nullptr; errno=0; double v=std::strtod(j.text.c_str(),&e);
    if(errno==ERANGE||e!=j.text.c_str()+j.text.size()||!std::isfinite(v))
        throw std::runtime_error(std::string("invalid finite number in '")+k+"'");
    return v;
}
std::size_t size_value(const Json& o,const char* k)
{
    const auto& j=required(o,k,Json::Type::Number);
    if(j.text.empty()||j.text[0]=='-'||j.text.find_first_of(".eE")!=std::string::npos) throw std::runtime_error(std::string("field '")+k+"' requires an unsigned integer");
    std::size_t pos=0; unsigned long long value=0; try{value=std::stoull(j.text,&pos,10);}catch(...){throw std::runtime_error(std::string("integer overflow in '")+k+"'");}
    if(pos!=j.text.size()||value>std::numeric_limits<std::size_t>::max()) throw std::runtime_error(std::string("integer overflow in '")+k+"'");
    return static_cast<std::size_t>(value);
}
std::vector<std::size_t> sizes_value(const Json& o,const char* k)
{
    const auto& a=required(o,k,Json::Type::Array); if(a.array.size()>maximum_vector_items)throw std::runtime_error("profile vector limit exceeded");
    std::vector<std::size_t> result; result.reserve(a.array.size());
    for(const auto& j:a.array){Json wrapper=Json::object_value();wrapper.object["v"]=j;result.push_back(size_value(wrapper,"v"));}
    return result;
}

ProfileEnvironment parse_environment(const Json& j)
{
    ProfileEnvironment e;
    e.application_build_identifier=string_value(j,"application_build_identifier");
    e.architecture=string_value(j,"architecture"); e.build_type=string_value(j,"build_type");
    e.compiler_identity=string_value(j,"compiler_identity"); e.compiler_version=string_value(j,"compiler_version");
    e.cpu_identity=string_value(j,"cpu_identity"); e.endianness=string_value(j,"endianness");
    e.feature_macros=string_value(j,"feature_macros"); e.floating_point_environment=string_value(j,"floating_point_environment");
    e.opencv_version=string_value(j,"opencv_version"); e.os_family=string_value(j,"os_family");
    e.pointer_width=size_value(j,"pointer_width"); e.required_isa=string_value(j,"required_isa");
    e.smartparallel_build_fingerprint=string_value(j,"smartparallel_build_fingerprint");
    e.smartparallel_version=string_value(j,"smartparallel_version");
    e.standard_library_identity=string_value(j,"standard_library_identity"); e.tbb_version=string_value(j,"tbb_version");
    return e;
}

OperationProfile parse_entry(const Json& j)
{
    OperationProfile e;
    e.accumulation_algorithm=string_value(j,"accumulation_algorithm"); e.actual_worker_policy=string_value(j,"actual_worker_policy");
    e.candidate_source_hash=string_value(j,"candidate_source_hash"); e.canonical_plan=string_value(j,"canonical_plan");
    e.capability_requirements=string_value(j,"capability_requirements"); e.element_type=string_value(j,"element_type");
    e.entry_hash=string_value(j,"entry_hash"); e.environment=parse_environment(required(j,"environment",Json::Type::Object));
    const Json& ev=required(j,"evidence",Json::Type::Object);
    e.evidence.confidence=double_value(ev,"confidence"); e.evidence.correctness_passed=bool_value(ev,"correctness_passed");
    e.evidence.created_utc=string_value(ev,"created_utc"); e.evidence.expires_utc=string_value(ev,"expires_utc");
    e.evidence.holdout_passed=bool_value(ev,"holdout_passed"); e.evidence.median_duration_ms=double_value(ev,"median_duration_ms");
    e.evidence.numerical_capability_passed=bool_value(ev,"numerical_capability_passed");
    e.evidence.route_authenticated=bool_value(ev,"route_authenticated"); e.evidence.route_switch_count=size_value(ev,"route_switch_count");
    e.evidence.sample_count=size_value(ev,"sample_count"); e.evidence.source_calibration_id=string_value(ev,"source_calibration_id");
    e.evidence.variability_ms=double_value(ev,"variability_ms");
    const Json& p=required(j,"execution_plan",Json::Type::Object);
    e.execution_plan.chunk_size=size_value(p,"chunk_size"); e.execution_plan.engine=parse_engine(string_value(p,"engine"));
    e.execution_plan.job_count=size_value(p,"job_count"); e.execution_plan.parallel=bool_value(p,"parallel");
    e.execution_plan.strategy=parse_strategy(string_value(p,"strategy"));
    e.exact_worker_budget=size_value(j,"exact_worker_budget");
    if(const Json* resource=optional(j,"resource_contract",Json::Type::Object))
    {
        e.resource_contract_present=true;
        e.exact_grant_required=bool_value(*resource,"exact_grant_required");
        e.granted_workers=size_value(*resource,"granted_workers");
        e.lease_wait_policy=parse_lease_wait_policy(string_value(*resource,"lease_wait_policy"));
        e.minimum_workers=size_value(*resource,"minimum_workers");
        e.requested_workers=size_value(*resource,"requested_workers");
        if(const Json* preferred=optional(*resource,"preferred_workers",Json::Type::Number))
        {
            Json wrapper=Json::object_value(); wrapper.object["v"]=*preferred;
            e.preferred_workers=size_value(wrapper,"v");
        }
        else e.preferred_workers=e.requested_workers;
        if(const Json* maximum=optional(*resource,"maximum_workers",Json::Type::Number))
        {
            Json wrapper=Json::object_value(); wrapper.object["v"]=*maximum;
            e.maximum_workers=size_value(wrapper,"v");
        }
        else e.maximum_workers=std::max(e.requested_workers,e.preferred_workers);
        e.nested_lease_mode=parse_nested_lease_mode(string_value(*resource,"nested_lease_mode"));
        e.observed_participating_threads=size_value(*resource,"observed_participating_threads");
        e.provider_control_scope=parse_control_scope(string_value(*resource,"provider_control_scope"));
        e.provider_control_strength=parse_control_strength(string_value(*resource,"provider_control_strength"));
        e.provider_serialized=bool_value(*resource,"provider_serialized");
        e.scheduler_concurrency_cap=size_value(*resource,"scheduler_concurrency_cap");
    }
    else
    {
        e.resource_contract_present=false;
        e.requested_workers=e.exact_worker_budget;
        e.minimum_workers=e.exact_worker_budget;
        e.preferred_workers=e.exact_worker_budget;
        e.maximum_workers=e.exact_worker_budget;
        e.granted_workers=e.exact_worker_budget;
        e.scheduler_concurrency_cap=e.execution_plan.parallel
            ? e.execution_plan.job_count : std::size_t{1};
        e.observed_participating_threads=e.scheduler_concurrency_cap;
        e.exact_grant_required=true;
        e.lease_wait_policy=LeaseWaitPolicy::Wait;
        e.nested_lease_mode=NestedLeaseMode::NotNested;
        e.provider_control_scope=e.execution_plan.engine==ExecutionEngineType::OneTbb
            ? ControlScope::PerTask : ControlScope::PerCall;
        e.provider_control_strength=e.execution_plan.engine==ExecutionEngineType::OneTbb
            ? ControlStrength::UpperBound : ControlStrength::Exact;
        e.provider_serialized=e.provider=="opencv";
    }
    e.evaluation_order=string_value(j,"evaluation_order");
    e.implementation_route=string_value(j,"implementation_route"); e.numerical_capability=string_value(j,"numerical_capability");
    e.numerical_policy=parse_numerical(string_value(j,"numerical_policy")); e.operation=string_value(j,"operation");
    e.operation_semantic_version=string_value(j,"operation_semantic_version"); e.plan_semantic_version=string_value(j,"plan_semantic_version");
    const std::string status=string_value(j,"profile_status");
    if(status=="Candidate")e.status=ProfileStatus::Candidate;else if(status=="Approved")e.status=ProfileStatus::Approved;else throw std::runtime_error("invalid profile status");
    e.provider=string_value(j,"provider"); e.provider_settings=string_value(j,"provider_settings");
    e.provider_version=string_value(j,"provider_version"); e.simd_kernel=string_value(j,"simd_kernel");
    const Json& w=required(j,"workload",Json::Type::Object);
    e.workload.boundary_mode=string_value(w,"boundary_mode"); e.workload.exact_fingerprint=string_value(w,"exact_fingerprint");
    e.workload.extents=sizes_value(w,"extents"); e.workload.in_place=bool_value(w,"in_place");
    e.workload.layout=string_value(w,"layout"); e.workload.semantic_constants=string_value(w,"semantic_constants");
    e.workload.strides=sizes_value(w,"strides");
    const std::string expected_entry=sha256_hex(canonical_json(entry_json(e,false)));
    if(expected_entry!=e.entry_hash) throw std::runtime_error("SmartParallel profile entry integrity hash mismatch");
    return e;
}

std::string read_file(const std::filesystem::path& path)
{
    std::ifstream input(path,std::ios::binary);
    if(!input)throw std::runtime_error("SmartParallel could not open profile file: "+path.string());
    input.seekg(0,std::ios::end); const auto length=input.tellg();
    if(length<0||static_cast<unsigned long long>(length)>maximum_profile_bytes)throw std::runtime_error("SmartParallel profile file exceeds size limit");
    input.seekg(0); std::string data(static_cast<std::size_t>(length),'\0');
    if(!data.empty()&&!input.read(data.data(),static_cast<std::streamsize>(data.size())))throw std::runtime_error("SmartParallel failed to read profile file");
    return data;
}
}

const OperationProfile* ProfileDatabase::find_exact(const std::string& operation,
                                                     const std::string& workload_fingerprint,
                                                     NumericalPolicy policy) const noexcept
{
    for (const auto& entry : entries)
        if (entry.operation == operation && entry.workload.exact_fingerprint == workload_fingerprint
            && entry.numerical_policy == policy)
            return &entry;
    return nullptr;
}
OperationProfile* ProfileDatabase::find_exact(const std::string& operation,
                                               const std::string& workload_fingerprint,
                                               NumericalPolicy policy) noexcept
{
    for (auto& entry : entries)
        if (entry.operation == operation && entry.workload.exact_fingerprint == workload_fingerprint
            && entry.numerical_policy == policy)
            return &entry;
    return nullptr;
}

const char* profile_status_name(ProfileStatus status) noexcept
{
    return status == ProfileStatus::Approved ? "Approved" : "Candidate";
}
const char* compatibility_issue_name(CompatibilityIssueCode code) noexcept
{
    switch(code)
    {
        case CompatibilityIssueCode::SchemaMismatch:return "schema_mismatch";
        case CompatibilityIssueCode::OperationMismatch:return "operation_mismatch";
        case CompatibilityIssueCode::OperationSemanticVersionMismatch:return "operation_semantic_version_mismatch";
        case CompatibilityIssueCode::DataTypeMismatch:return "data_type_mismatch";
        case CompatibilityIssueCode::NumericalPolicyMismatch:return "numerical_policy_mismatch";
        case CompatibilityIssueCode::EvaluationOrderMismatch:return "evaluation_order_mismatch";
        case CompatibilityIssueCode::AccumulationAlgorithmMismatch:return "accumulation_algorithm_mismatch";
        case CompatibilityIssueCode::CanonicalPlanMismatch:return "canonical_plan_mismatch";
        case CompatibilityIssueCode::WorkloadExtentMismatch:return "workload_extent_mismatch";
        case CompatibilityIssueCode::StrideMismatch:return "stride_mismatch";
        case CompatibilityIssueCode::LayoutMismatch:return "layout_mismatch";
        case CompatibilityIssueCode::BoundaryModeMismatch:return "boundary_mode_mismatch";
        case CompatibilityIssueCode::ArchitectureMismatch:return "architecture_mismatch";
        case CompatibilityIssueCode::RequiredIsaUnavailable:return "required_isa_unavailable";
        case CompatibilityIssueCode::CompilerBuildMismatch:return "compiler_build_mismatch";
        case CompatibilityIssueCode::SchedulerUnavailable:return "scheduler_unavailable";
        case CompatibilityIssueCode::ProviderUnavailable:return "provider_unavailable";
        case CompatibilityIssueCode::ProviderVersionMismatch:return "provider_version_mismatch";
        case CompatibilityIssueCode::ProviderSettingMismatch:return "provider_setting_mismatch";
        case CompatibilityIssueCode::WorkerBudgetMismatch:return "worker_budget_mismatch";
        case CompatibilityIssueCode::FloatingPointEnvironmentMismatch:return "floating_point_environment_mismatch";
        case CompatibilityIssueCode::ProfileNotApproved:return "profile_not_approved";
        case CompatibilityIssueCode::ProfileExpired:return "profile_expired";
        case CompatibilityIssueCode::IntegrityFailure:return "integrity_failure";
        case CompatibilityIssueCode::WorkloadFingerprintMismatch:return "workload_fingerprint_mismatch";
        case CompatibilityIssueCode::BuildIdentifierMismatch:return "build_identifier_mismatch";
    }
    return "unknown";
}

std::string profile_database_to_canonical_json(const ProfileDatabase& database, bool include_hash)
{
    ProfileDatabase copy=database;
    copy.content_hash.clear();
    const std::string without_hash=canonical_json(database_json(copy,false));
    copy.content_hash=sha256_hex(without_hash);
    return canonical_json(database_json(copy,include_hash));
}

ProfileDatabase profile_database_from_json(const std::string& json)
{
    const Json root=JsonParser(json).parse();
    ProfileDatabase db;
    db.content_hash=string_value(root,"content_hash"); db.created_utc=string_value(root,"created_utc");
    db.schema_version=size_value(root,"schema_version");
    if(db.schema_version!=1)throw std::runtime_error("SmartParallel profile schema major version is unsupported");
    db.semantic_version=string_value(root,"semantic_version");
    if(db.semantic_version.empty()||db.semantic_version[0]!='1')throw std::runtime_error("SmartParallel profile semantic major version is unsupported");
    db.smartparallel_version=string_value(root,"smartparallel_version");
    db.environment=parse_environment(required(root,"environment",Json::Type::Object));
    const Json& entries=required(root,"entries",Json::Type::Array);
    if(entries.array.size()>maximum_profile_entries)throw std::runtime_error("SmartParallel profile entry count exceeds limit");
    db.entries.reserve(entries.array.size());
    for(const auto& item:entries.array)db.entries.push_back(parse_entry(item));
    const std::string expected=sha256_hex(canonical_json(database_json(db,false)));
    if(expected!=db.content_hash)throw std::runtime_error("SmartParallel profile database integrity hash mismatch");
    return db;
}

ProfileDatabase load_profile_database(const std::filesystem::path& path)
{
    return profile_database_from_json(read_file(path));
}

void save_profile_database_atomic(const ProfileDatabase& database,
                                  const std::filesystem::path& path)
{
    if(path.empty())throw std::invalid_argument("SmartParallel profile save path is empty");
    const std::string serialized=profile_database_to_canonical_json(database,true);
    const std::filesystem::path directory=path.has_parent_path()?path.parent_path():std::filesystem::current_path();
    std::error_code error; std::filesystem::create_directories(directory,error);
    if(error)throw std::runtime_error("SmartParallel could not create profile directory: "+error.message());
    const std::filesystem::path temporary=directory/(path.filename().string()+".tmp."+sha256_hex(serialized).substr(0,16));
    struct Cleanup { std::filesystem::path path; ~Cleanup(){std::error_code e; if(!path.empty())std::filesystem::remove(path,e);} } cleanup{temporary};
    {
        std::ofstream output(temporary,std::ios::binary|std::ios::trunc);
        if(!output)throw std::runtime_error("SmartParallel could not create temporary profile file");
        output.write(serialized.data(),static_cast<std::streamsize>(serialized.size()));
        output.flush();
        if(!output)throw std::runtime_error("SmartParallel failed writing temporary profile file");
    }
    const ProfileDatabase validated=load_profile_database(temporary);
    if(validated.content_hash!=sha256_hex(canonical_json(database_json(validated,false))))
        throw std::runtime_error("SmartParallel temporary profile validation failed");
#if defined(_WIN32)
    if(!MoveFileExW(temporary.wstring().c_str(),path.wstring().c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("SmartParallel atomic profile replacement failed with Windows error "+std::to_string(GetLastError()));
#else
    std::filesystem::rename(temporary,path,error);
    if(error)throw std::runtime_error("SmartParallel atomic profile replacement failed: "+error.message());
#endif
    cleanup.path.clear();
}

ProfileCompatibilityReport validate_profile_database_integrity(const ProfileDatabase& database)
{
    ProfileCompatibilityReport report;
    if(database.schema_version!=1)
        report.add({CompatibilityIssueCode::SchemaMismatch,"schema_version","1",std::to_string(database.schema_version),"unsupported profile schema"});
    const std::string expected=sha256_hex(canonical_json(database_json(database,false)));
    if(database.content_hash!=expected)
        report.add({CompatibilityIssueCode::IntegrityFailure,"content_hash",expected,database.content_hash,"profile content hash does not match canonical content"});
    for(const auto& entry:database.entries)
    {
        const std::string entry_expected=sha256_hex(canonical_json(entry_json(entry,false)));
        if(entry.entry_hash!=entry_expected)
            report.add({CompatibilityIssueCode::IntegrityFailure,"entry_hash",entry_expected,entry.entry_hash,"profile entry hash mismatch"});
    }
    return report;
}
} // namespace smart
