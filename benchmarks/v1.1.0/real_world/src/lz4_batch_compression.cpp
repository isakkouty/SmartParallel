#include "real_world_benchmark.hpp"

#include <lz4.h>

#include <atomic>
#include <cstring>
#include <memory>
#include <random>

namespace rw = smart::real_world;

namespace
{
enum class DataKind
{
    Compressible,
    Incompressible,
    Mixed
};

struct Preset
{
    std::string name;
    std::size_t blocks = 0;
    std::size_t minimum_size = 0;
    std::size_t maximum_size = 0;
    DataKind kind = DataKind::Compressible;
};

std::vector<Preset> presets()
{
    return {
        {"tiny_compressible", 4096, 128, 128, DataKind::Compressible},
        {"tiny_incompressible", 4096, 128, 128, DataKind::Incompressible},
        {"medium_compressible", 256, 64 * 1024, 64 * 1024, DataKind::Compressible},
        {"medium_incompressible", 256, 64 * 1024, 64 * 1024, DataKind::Incompressible},
        {"large_blocks", 16, 4 * 1024 * 1024, 4 * 1024 * 1024, DataKind::Mixed},
        {"mixed_sizes", 512, 64, 1024 * 1024, DataKind::Mixed},
    };
}

class Lz4Workload
{
  public:
    Lz4Workload(Preset preset, std::uint64_t seed, std::size_t workers)
        : preset_(std::move(preset)), workers_(workers), executions_(new std::atomic<unsigned>[preset_.blocks])
    {
        generate(seed);
        compressed_.resize(input_.size());
        restored_.resize(input_.size());
        compressed_sizes_.resize(input_.size());
        decompressed_sizes_.resize(input_.size());
        status_.resize(input_.size());
        for (std::size_t i = 0; i < input_.size(); ++i)
        {
            compressed_[i].resize(static_cast<std::size_t>(LZ4_compressBound(
                static_cast<int>(input_[i].size()))));
            restored_[i].resize(input_[i].size());
        }
        expected_checksum_ = checksum_input();
        reset();
    }

    void reset()
    {
        concurrency_.reset();
        for (std::size_t i = 0; i < input_.size(); ++i)
        {
            executions_[i].store(0, std::memory_order_relaxed);
            compressed_sizes_[i] = 0;
            decompressed_sizes_[i] = 0;
            status_[i] = 0;
        }
    }

    void execute(const rw::ModeSpec& mode)
    {
        const auto body = [&](std::size_t block) { process(block); };
        switch (mode.kind)
        {
            case rw::ModeKind::Sequential:
            case rw::ModeKind::SmartForcedSequential:
                rw::sequential_for(input_.size(), body);
                break;
            case rw::ModeKind::ManualBackend:
                rw::fixed_parallel_for(input_.size(), mode.backend, workers_, body);
                break;
            case rw::ModeKind::SmartAuto:
                rw::smart_parallel_for(input_.size(), smart::ExecutionEngineType::Auto, body);
                break;
            case rw::ModeKind::SmartForcedBackend:
                rw::smart_parallel_for(input_.size(), mode.backend, body);
                break;
            default:
                throw std::invalid_argument("nested-only mode requested for LZ4 benchmark");
        }
    }

    rw::ValidationResult validate() const
    {
        rw::ValidationResult result;
        result.expected_checksum = expected_checksum_;
        std::uint64_t hash = 0xcbf29ce484222325ull;
        bool correct = true;
        for (std::size_t i = 0; i < input_.size(); ++i)
        {
            correct = correct && executions_[i].load(std::memory_order_relaxed) == 1;
            correct = correct && status_[i] == 0;
            correct = correct && decompressed_sizes_[i] == static_cast<int>(input_[i].size());
            correct = correct && restored_[i] == input_[i];
            correct = correct && compressed_sizes_[i] > 0
                && compressed_sizes_[i] <= static_cast<int>(compressed_[i].size());
            hash = rw::mix64(hash ^ rw::hash_bytes(restored_[i].data(), restored_[i].size())
                             ^ static_cast<std::uint64_t>(input_[i].size())
                             ^ (static_cast<std::uint64_t>(i + 1) << 17));
        }
        result.checksum = hash;
        result.correct = correct && result.checksum == result.expected_checksum;
        if (!result.correct)
            result.message = "LZ4 round-trip or exact-once validation failed";
        return result;
    }

    double bytes() const noexcept { return static_cast<double>(total_bytes_); }
    std::size_t task_count() const noexcept { return input_.size(); }
    std::size_t observed_concurrency() const noexcept { return concurrency_.maximum(); }

    std::string parameters() const
    {
        std::ostringstream out;
        out << "blocks=" << input_.size() << ";bytes=" << total_bytes_
            << ";min_block=" << preset_.minimum_size << ";max_block=" << preset_.maximum_size;
        return out.str();
    }

  private:
    static std::uint64_t next_random(std::uint64_t& state)
    {
        state ^= state >> 12;
        state ^= state << 25;
        state ^= state >> 27;
        return state * 2685821657736338717ull;
    }

    void generate(std::uint64_t seed)
    {
        input_.resize(preset_.blocks);
        std::uint64_t state = seed == 0 ? 1 : seed;
        for (std::size_t block = 0; block < input_.size(); ++block)
        {
            std::size_t size = preset_.minimum_size;
            if (preset_.maximum_size > preset_.minimum_size)
            {
                const std::size_t span = preset_.maximum_size - preset_.minimum_size;
                const std::size_t bucket = static_cast<std::size_t>(next_random(state) % 12);
                const double fraction = static_cast<double>(bucket) / 11.0;
                size += static_cast<std::size_t>(fraction * fraction * static_cast<double>(span));
            }
            input_[block].resize(size);
            const bool compressible = preset_.kind == DataKind::Compressible
                || (preset_.kind == DataKind::Mixed && (block % 3 != 0));
            if (compressible)
            {
                const std::array<unsigned char, 16> pattern{{
                    static_cast<unsigned char>(block), 0, 0, 0, 17, 17, 17, 17,
                    31, 31, 31, 31, 0, 0, static_cast<unsigned char>(block >> 8), 0}};
                for (std::size_t i = 0; i < size; ++i)
                    input_[block][i] = static_cast<char>(pattern[i % pattern.size()]);
                for (std::size_t i = 0; i < size; i += 4096)
                    input_[block][i] = static_cast<char>(next_random(state));
            }
            else
            {
                for (std::size_t i = 0; i < size; ++i)
                    input_[block][i] = static_cast<char>(next_random(state) >> 56);
            }
            total_bytes_ += size;
        }
    }

    void process(std::size_t block)
    {
        auto active = concurrency_.scope();
        executions_[block].fetch_add(1, std::memory_order_relaxed);
        const int input_size = static_cast<int>(input_[block].size());
        const int compressed_size = LZ4_compress_default(
            input_[block].data(),
            compressed_[block].data(),
            input_size,
            static_cast<int>(compressed_[block].size()));
        compressed_sizes_[block] = compressed_size;
        if (compressed_size <= 0)
        {
            status_[block] = 1;
            return;
        }
        const int decompressed_size = LZ4_decompress_safe(
            compressed_[block].data(),
            restored_[block].data(),
            compressed_size,
            input_size);
        decompressed_sizes_[block] = decompressed_size;
        if (decompressed_size != input_size)
            status_[block] = 2;
    }

    std::uint64_t checksum_input() const
    {
        std::uint64_t hash = 0xcbf29ce484222325ull;
        for (std::size_t i = 0; i < input_.size(); ++i)
        {
            hash = rw::mix64(hash ^ rw::hash_bytes(input_[i].data(), input_[i].size())
                             ^ static_cast<std::uint64_t>(input_[i].size())
                             ^ (static_cast<std::uint64_t>(i + 1) << 17));
        }
        return hash;
    }

    Preset preset_;
    std::size_t workers_ = 1;
    std::vector<std::vector<char>> input_;
    std::vector<std::vector<char>> compressed_;
    std::vector<std::vector<char>> restored_;
    std::vector<int> compressed_sizes_;
    std::vector<int> decompressed_sizes_;
    std::vector<unsigned char> status_;
    std::unique_ptr<std::atomic<unsigned>[]> executions_;
    std::size_t total_bytes_ = 0;
    std::uint64_t expected_checksum_ = 0;
    rw::ConcurrencyProbe concurrency_;
};

} // namespace

int main(int argc, char** argv)
{
    try
    {
        const auto all_presets = presets();
        std::vector<std::string> preset_names;
        for (const auto& preset : all_presets)
            preset_names.push_back(preset.name);
        const rw::Options options = rw::parse_options(argc, argv);
        if (options.help || options.list_presets)
        {
            rw::print_common_help(argv[0], preset_names);
            return 0;
        }
        const auto modes = rw::make_modes(options, false);
        rw::BenchmarkOutputs outputs;
        for (const auto& preset : all_presets)
        {
            if (!rw::contains(options.presets, "all")
                && !rw::contains(options.presets, preset.name))
                continue;
            Lz4Workload workload(preset, options.seed, options.workers);
            rw::CaseDefinition definition;
            definition.integration = "lz4";
            definition.workload = "batch_round_trip";
            definition.preset = preset.name;
            definition.parameters = workload.parameters();
            definition.unit_name = "input_bytes_per_second";
            definition.throughput_units = workload.bytes();
            definition.task_count = workload.task_count();
            definition.reset = [&] { workload.reset(); };
            definition.execute = [&](const rw::ModeSpec& mode) { workload.execute(mode); };
            definition.validate = [&] { return workload.validate(); };
            definition.observed_concurrency = [&] { return workload.observed_concurrency(); };
            rw::run_case(definition, modes, options, outputs);
        }
        rw::write_outputs(
            "lz4",
            options,
            std::move(outputs),
            {{"lz4_version", LZ4_versionString()}, {"dataset", "deterministic_generated_blocks"}});
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "LZ4 real-world benchmark failed: " << error.what() << '\n';
        return 1;
    }
}
