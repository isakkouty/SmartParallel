#include "real_world_benchmark.hpp"

#include <array>
#include <cmath>
#include <cstring>

namespace rw = smart::real_world;

namespace
{
enum class Scenario
{
    Uniform,
    Clustered,
    Sparse,
    SuddenChange,
    GradualIncrease,
    MovingClusters
};

struct Preset
{
    std::string name;
    std::size_t particles = 0;
    std::size_t frames = 0;
    std::size_t grid = 32;
    Scenario scenario = Scenario::Uniform;
};

std::vector<Preset> presets()
{
    return {
        {"tiny", 128, 12, 1, Scenario::Uniform},
        {"uniform", 5000, 24, 32, Scenario::Uniform},
        {"clustered", 5000, 24, 32, Scenario::Clustered},
        {"sparse", 5000, 24, 64, Scenario::Sparse},
        {"sudden_count_change", 8000, 30, 40, Scenario::SuddenChange},
        {"gradual_count_increase", 8000, 30, 40, Scenario::GradualIncrease},
        {"moving_clusters", 6000, 36, 32, Scenario::MovingClusters},
    };
}

struct Particle
{
    double x = 0.0;
    double y = 0.0;
    double vx = 0.0;
    double vy = 0.0;
    double mass = 1.0;
};

class ParticleWorkload
{
  public:
    ParticleWorkload(Preset preset, std::uint64_t seed, std::size_t workers)
        : preset_(std::move(preset)), workers_(workers)
    {
        generate(seed);
        current_ = initial_;
        next_.resize(initial_.size());
        particle_cell_.resize(initial_.size());
        bins_.resize(preset_.grid * preset_.grid);
        output_active_count_ = 0;

        rw::ModeSpec sequential{"sequential", rw::ModeKind::Sequential,
                                smart::ExecutionEngineType::Auto};
        simulate(sequential);
        expected_ = current_;
        expected_active_count_ = output_active_count_;
        expected_checksum_ = state_checksum(expected_, expected_active_count_);
        reset();
    }

    void reset()
    {
        concurrency_.reset();
        current_ = initial_;
        std::fill(next_.begin(), next_.end(), Particle{});
        output_active_count_ = 0;
    }

    void execute(const rw::ModeSpec& mode) { simulate(mode); }

    rw::ValidationResult validate() const
    {
        rw::ValidationResult result;
        result.expected_checksum = expected_checksum_;
        result.checksum = state_checksum(current_, output_active_count_);
        bool correct = output_active_count_ == expected_active_count_
            && current_.size() == expected_.size();
        const double tolerance = 1e-11;
        for (std::size_t i = 0; correct && i < current_.size(); ++i)
        {
            const Particle& actual = current_[i];
            const Particle& expected = expected_[i];
            correct = std::isfinite(actual.x) && std::isfinite(actual.y)
                && std::isfinite(actual.vx) && std::isfinite(actual.vy)
                && std::abs(actual.x - expected.x) <= tolerance
                && std::abs(actual.y - expected.y) <= tolerance
                && std::abs(actual.vx - expected.vx) <= tolerance
                && std::abs(actual.vy - expected.vy) <= tolerance;
        }
        result.correct = correct && result.checksum == result.expected_checksum;
        if (!result.correct)
            result.message = "particle final state differs from deterministic sequential reference";
        return result;
    }

    double particle_updates() const noexcept { return static_cast<double>(total_updates_); }
    std::size_t task_count() const noexcept { return total_updates_; }
    std::size_t observed_concurrency() const noexcept { return concurrency_.maximum(); }

    std::string parameters() const
    {
        std::ostringstream out;
        out << "max_particles=" << preset_.particles << ";frames=" << preset_.frames
            << ";grid=" << preset_.grid << 'x' << preset_.grid
            << ";particle_updates=" << total_updates_;
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

    static double unit_random(std::uint64_t& state)
    {
        return static_cast<double>(next_random(state) >> 11)
               * (1.0 / static_cast<double>(1ull << 53));
    }

    void generate(std::uint64_t seed)
    {
        std::uint64_t state = seed == 0 ? 1 : seed;
        initial_.resize(preset_.particles);
        for (std::size_t i = 0; i < initial_.size(); ++i)
        {
            Particle particle;
            if (preset_.scenario == Scenario::Clustered
                || preset_.scenario == Scenario::MovingClusters)
            {
                const std::size_t cluster = i % 4;
                const double cx = (cluster & 1u) ? 0.72 : 0.28;
                const double cy = (cluster & 2u) ? 0.72 : 0.28;
                particle.x = cx + (unit_random(state) - 0.5) * 0.12;
                particle.y = cy + (unit_random(state) - 0.5) * 0.12;
                if (preset_.scenario == Scenario::MovingClusters)
                {
                    particle.vx = (cluster & 1u) ? -0.010 : 0.010;
                    particle.vy = (cluster & 2u) ? -0.008 : 0.008;
                }
            }
            else if (preset_.scenario == Scenario::Sparse)
            {
                particle.x = unit_random(state);
                particle.y = unit_random(state);
            }
            else
            {
                particle.x = unit_random(state);
                particle.y = unit_random(state);
            }
            particle.vx += (unit_random(state) - 0.5) * 0.002;
            particle.vy += (unit_random(state) - 0.5) * 0.002;
            particle.mass = 0.75 + unit_random(state) * 0.5;
            initial_[i] = particle;
        }
        total_updates_ = 0;
        for (std::size_t frame = 0; frame < preset_.frames; ++frame)
            total_updates_ += active_count(frame);
    }

    std::size_t active_count(std::size_t frame) const
    {
        switch (preset_.scenario)
        {
            case Scenario::SuddenChange:
                return frame < preset_.frames / 3
                    ? std::min<std::size_t>(1000, preset_.particles)
                    : preset_.particles;
            case Scenario::GradualIncrease:
            {
                const std::size_t minimum = std::min<std::size_t>(500, preset_.particles);
                const std::size_t span = preset_.particles - minimum;
                return minimum + span * frame / std::max<std::size_t>(1, preset_.frames - 1);
            }
            default:
                return preset_.particles;
        }
    }

    std::size_t cell_index(double x, double y) const
    {
        const std::size_t gx = std::min<std::size_t>(
            preset_.grid - 1,
            static_cast<std::size_t>(std::max(0.0, std::min(0.999999999, x))
                                     * static_cast<double>(preset_.grid)));
        const std::size_t gy = std::min<std::size_t>(
            preset_.grid - 1,
            static_cast<std::size_t>(std::max(0.0, std::min(0.999999999, y))
                                     * static_cast<double>(preset_.grid)));
        return gy * preset_.grid + gx;
    }

    void bin_particles(std::size_t active)
    {
        for (auto& bin : bins_)
            bin.clear();
        for (std::size_t particle = 0; particle < active; ++particle)
        {
            const std::size_t cell = cell_index(current_[particle].x, current_[particle].y);
            particle_cell_[particle] = cell;
            bins_[cell].push_back(particle);
        }
    }

    void update_particle(std::size_t particle, std::size_t active)
    {
        auto active_scope = concurrency_.scope();
        const Particle& source = current_[particle];
        const std::size_t cell = particle_cell_[particle];
        const std::size_t cx = cell % preset_.grid;
        const std::size_t cy = cell / preset_.grid;
        double fx = 0.0;
        double fy = 0.0;
        std::size_t visited = 0;
        for (int oy = -1; oy <= 1 && visited < max_neighbors_; ++oy)
        {
            const int y = static_cast<int>(cy) + oy;
            if (y < 0 || y >= static_cast<int>(preset_.grid))
                continue;
            for (int ox = -1; ox <= 1 && visited < max_neighbors_; ++ox)
            {
                const int x = static_cast<int>(cx) + ox;
                if (x < 0 || x >= static_cast<int>(preset_.grid))
                    continue;
                const auto& neighbors = bins_[static_cast<std::size_t>(y) * preset_.grid
                                              + static_cast<std::size_t>(x)];
                for (std::size_t other : neighbors)
                {
                    if (other == particle || other >= active)
                        continue;
                    const double dx = current_[other].x - source.x;
                    const double dy = current_[other].y - source.y;
                    const double distance2 = dx * dx + dy * dy + 1e-6;
                    const double inverse = 1.0 / std::sqrt(distance2);
                    const double attraction = 0.000002 * current_[other].mass * inverse * inverse;
                    fx += dx * inverse * attraction;
                    fy += dy * inverse * attraction;
                    if (++visited >= max_neighbors_)
                        break;
                }
            }
        }
        Particle result = source;
        result.vx = (source.vx + fx) * 0.9995;
        result.vy = (source.vy + fy) * 0.9995;
        result.x = source.x + result.vx;
        result.y = source.y + result.vy;
        if (result.x < 0.0)
        {
            result.x = -result.x;
            result.vx = -result.vx * 0.8;
        }
        else if (result.x > 1.0)
        {
            result.x = 2.0 - result.x;
            result.vx = -result.vx * 0.8;
        }
        if (result.y < 0.0)
        {
            result.y = -result.y;
            result.vy = -result.vy * 0.8;
        }
        else if (result.y > 1.0)
        {
            result.y = 2.0 - result.y;
            result.vy = -result.vy * 0.8;
        }
        next_[particle] = result;
    }

    void process_cell(std::size_t cell,
                      std::size_t active,
                      const rw::ModeSpec& mode,
                      bool inner_parallel,
                      bool smart)
    {
        const auto& particles = bins_[cell];
        const auto body = [&](std::size_t local)
        {
            update_particle(particles[local], active);
        };
        if (!inner_parallel)
            rw::sequential_for(particles.size(), body);
        else if (smart)
            rw::smart_parallel_for(particles.size(),
                                   mode.kind == rw::ModeKind::SmartForcedBackend
                                       ? mode.backend
                                       : smart::ExecutionEngineType::Auto,
                                   body);
        else
            rw::coordinated_parallel_for(particles.size(), mode.backend, workers_, body);
    }

    void run_frame(const rw::ModeSpec& mode, std::size_t frame)
    {
        const std::size_t active = active_count(frame);
        bin_particles(active);
        for (std::size_t i = active; i < next_.size(); ++i)
            next_[i] = current_[i];

        if (mode.kind == rw::ModeKind::Flattened)
        {
            rw::fixed_parallel_for(active, mode.backend, workers_,
                                   [&](std::size_t particle) { update_particle(particle, active); });
        }
        else
        {
            const bool smart = mode.kind == rw::ModeKind::SmartAuto
                || mode.kind == rw::ModeKind::SmartForcedBackend;
            const bool outer_parallel = mode.kind == rw::ModeKind::ManualBackend
                || mode.kind == rw::ModeKind::OuterOnly
                || mode.kind == rw::ModeKind::AllLevels || smart;
            const bool inner_parallel = mode.kind == rw::ModeKind::InnerOnly
                || mode.kind == rw::ModeKind::AllLevels || smart;
            const auto outer = [&](std::size_t cell)
            {
                process_cell(cell, active, mode, inner_parallel, smart);
            };
            if (smart)
            {
                rw::smart_parallel_for(bins_.size(),
                                       mode.kind == rw::ModeKind::SmartForcedBackend
                                           ? mode.backend
                                           : smart::ExecutionEngineType::Auto,
                                       outer);
            }
            else if (mode.kind == rw::ModeKind::ManualBackend)
            {
                rw::fixed_parallel_for(bins_.size(), mode.backend, workers_, outer);
            }
            else if (outer_parallel)
            {
                rw::with_fixed_root_session(mode.backend, workers_, [&]
                {
                    rw::coordinated_parallel_for(bins_.size(), mode.backend, workers_, outer);
                });
            }
            else if (inner_parallel)
            {
                rw::with_fixed_root_session(mode.backend, workers_, [&]
                {
                    rw::sequential_for(bins_.size(), outer);
                });
            }
            else
            {
                rw::sequential_for(bins_.size(), outer);
            }
        }
        current_.swap(next_);
        output_active_count_ = active;
    }

    void simulate(const rw::ModeSpec& mode)
    {
        for (std::size_t frame = 0; frame < preset_.frames; ++frame)
            run_frame(mode, frame);
    }

    static std::int64_t quantize(double value)
    {
        return static_cast<std::int64_t>(std::llround(value * 1.0e12));
    }

    static std::uint64_t state_checksum(const std::vector<Particle>& particles,
                                        std::size_t active)
    {
        std::uint64_t checksum = rw::mix64(active);
        for (std::size_t i = 0; i < particles.size(); ++i)
        {
            const Particle& particle = particles[i];
            checksum = rw::mix64(checksum
                ^ static_cast<std::uint64_t>(quantize(particle.x))
                ^ (static_cast<std::uint64_t>(quantize(particle.y)) << 1)
                ^ (static_cast<std::uint64_t>(quantize(particle.vx)) << 2)
                ^ (static_cast<std::uint64_t>(quantize(particle.vy)) << 3)
                ^ static_cast<std::uint64_t>(i + 1));
        }
        return checksum;
    }

    Preset preset_;
    std::size_t workers_ = 1;
    std::size_t max_neighbors_ = 96;
    std::size_t total_updates_ = 0;
    std::vector<Particle> initial_;
    std::vector<Particle> current_;
    std::vector<Particle> next_;
    std::vector<Particle> expected_;
    std::vector<std::size_t> particle_cell_;
    std::vector<std::vector<std::size_t>> bins_;
    std::size_t output_active_count_ = 0;
    std::size_t expected_active_count_ = 0;
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
        const auto modes = rw::make_modes(options, true);
        rw::BenchmarkOutputs outputs;
        for (const auto& preset : all_presets)
        {
            if (!rw::contains(options.presets, "all")
                && !rw::contains(options.presets, preset.name))
                continue;
            ParticleWorkload workload(preset, options.seed, options.workers);
            rw::CaseDefinition definition;
            definition.integration = "particles";
            definition.workload = "uniform_grid_simulation";
            definition.preset = preset.name;
            definition.parameters = workload.parameters();
            definition.unit_name = "particle_updates_per_second";
            definition.throughput_units = workload.particle_updates();
            definition.task_count = workload.task_count();
            definition.reset = [&] { workload.reset(); };
            definition.execute = [&](const rw::ModeSpec& mode) { workload.execute(mode); };
            definition.validate = [&] { return workload.validate(); };
            definition.observed_concurrency = [&] { return workload.observed_concurrency(); };
            rw::run_case(definition, modes, options, outputs);
        }
        rw::write_outputs(
            "particles",
            options,
            std::move(outputs),
            {{"simulation", "deterministic_uniform_grid_neighbor_forces"},
             {"numeric_validation", "absolute_tolerance_1e-11"},
             {"dataset", "deterministic_generated_particles"}});
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Particle real-world benchmark failed: " << error.what() << '\n';
        return 1;
    }
}
