#include "real_world_benchmark.hpp"

#include <array>
#include <atomic>
#include <memory>
#include <random>

namespace rw = smart::real_world;

namespace
{
struct Vec3
{
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct Aabb
{
    Vec3 minimum{std::numeric_limits<double>::infinity(),
                 std::numeric_limits<double>::infinity(),
                 std::numeric_limits<double>::infinity()};
    Vec3 maximum{-std::numeric_limits<double>::infinity(),
                 -std::numeric_limits<double>::infinity(),
                 -std::numeric_limits<double>::infinity()};
};

Aabb merge(const Aabb& a, const Aabb& b)
{
    return {{std::min(a.minimum.x, b.minimum.x),
             std::min(a.minimum.y, b.minimum.y),
             std::min(a.minimum.z, b.minimum.z)},
            {std::max(a.maximum.x, b.maximum.x),
             std::max(a.maximum.y, b.maximum.y),
             std::max(a.maximum.z, b.maximum.z)}};
}

bool intersects(const Aabb& a, const Aabb& b)
{
    return a.minimum.x <= b.maximum.x && a.maximum.x >= b.minimum.x
        && a.minimum.y <= b.maximum.y && a.maximum.y >= b.minimum.y
        && a.minimum.z <= b.maximum.z && a.maximum.z >= b.minimum.z;
}

bool contains(const Aabb& parent, const Aabb& child, double epsilon = 1e-10)
{
    return parent.minimum.x <= child.minimum.x + epsilon
        && parent.minimum.y <= child.minimum.y + epsilon
        && parent.minimum.z <= child.minimum.z + epsilon
        && parent.maximum.x + epsilon >= child.maximum.x
        && parent.maximum.y + epsilon >= child.maximum.y
        && parent.maximum.z + epsilon >= child.maximum.z;
}


struct Primitive
{
    Aabb bounds;
    Vec3 center;
    std::size_t id = 0;
};

struct Node
{
    Aabb bounds;
    std::unique_ptr<Node> left;
    std::unique_ptr<Node> right;
    std::vector<std::size_t> primitives;

    bool leaf() const noexcept { return !left && !right; }
};

enum class Distribution
{
    Uniform,
    Clustered,
    Skewed,
    Mixed
};

struct Preset
{
    std::string name;
    std::size_t primitives = 0;
    Distribution distribution = Distribution::Uniform;
};

std::vector<Preset> presets()
{
    return {
        {"small_uniform", 512, Distribution::Uniform},
        {"uniform", 20000, Distribution::Uniform},
        {"clustered", 20000, Distribution::Clustered},
        {"highly_unbalanced", 20000, Distribution::Skewed},
        {"mixed_distribution", 40000, Distribution::Mixed},
        {"large_uniform", 100000, Distribution::Uniform},
    };
}

class BvhWorkload
{
  public:
    BvhWorkload(Preset preset, std::uint64_t seed, std::size_t workers)
        : preset_(std::move(preset)), workers_(workers)
    {
        generate(seed);
        generate_queries(seed ^ 0xB7A4C3D2ull);
        expected_query_counts_.reserve(queries_.size());
        for (const auto& query : queries_)
            expected_query_counts_.push_back(brute_force_count(query));
        expected_checksum_ = query_checksum(expected_query_counts_);
    }

    void reset()
    {
        root_.reset();
        concurrency_.reset();
        inject_failure_.store(false, std::memory_order_relaxed);
        failure_thrown_.store(false, std::memory_order_relaxed);
    }

    void execute(const rw::ModeSpec& mode)
    {
        std::vector<std::size_t> indices(primitives_.size());
        std::iota(indices.begin(), indices.end(), 0);
        if (mode.kind == rw::ModeKind::Flattened)
        {
            root_ = build_flattened(std::move(indices), mode.backend);
            return;
        }
        const bool smart = mode.kind == rw::ModeKind::SmartAuto
            || mode.kind == rw::ModeKind::SmartForcedBackend;
        if (mode.kind == rw::ModeKind::ManualBackend)
        {
            // A manual BVH strategy exposes one coarse four-subtree frontier.
            root_ = build_node(std::move(indices), mode, 0, true, false, false);
            return;
        }
        if (smart)
        {
            root_ = build_node(std::move(indices), mode, 0, true, true, true);
            return;
        }
        if (mode.kind == rw::ModeKind::Sequential
            || mode.kind == rw::ModeKind::SmartForcedSequential)
        {
            root_ = build_node(std::move(indices), mode, 0, false, false, false);
            return;
        }
        rw::with_fixed_root_session(mode.backend, workers_, [&]
        {
            root_ = build_node(std::move(indices), mode, 0, true, false, true);
        });
    }

    rw::ValidationResult validate() const
    {
        rw::ValidationResult result;
        result.expected_checksum = expected_checksum_;
        if (!root_)
        {
            result.message = "missing BVH root";
            return result;
        }
        std::vector<unsigned> occurrences(primitives_.size(), 0);
        bool structure_ok = true;
        validate_node(*root_, occurrences, structure_ok);
        for (unsigned count : occurrences)
            structure_ok = structure_ok && count == 1;
        const Aabb expected_bounds = primitive_bounds(make_all_indices());
        structure_ok = structure_ok && contains(root_->bounds, expected_bounds)
            && contains(expected_bounds, root_->bounds);

        std::vector<std::size_t> counts;
        counts.reserve(queries_.size());
        for (const auto& query : queries_)
            counts.push_back(query_count(root_.get(), query));
        result.checksum = query_checksum(counts);
        result.correct = structure_ok && counts == expected_query_counts_
            && result.checksum == result.expected_checksum;
        if (!result.correct)
            result.message = "BVH structure or traversal mismatch";
        return result;
    }

    void validate_cancellation_recovery(smart::ExecutionEngineType backend)
    {
        rw::ModeSpec mode{"cancellation_probe", rw::ModeKind::SmartForcedBackend, backend};
        std::vector<std::size_t> indices = make_all_indices();
        inject_failure_.store(true, std::memory_order_relaxed);
        failure_thrown_.store(false, std::memory_order_relaxed);
        bool propagated = false;
        try
        {
            root_ = build_node(std::move(indices), mode, 0, true, true, true);
        }
        catch (const std::runtime_error& error)
        {
            propagated = std::string(error.what()) == "bvh_injected_failure";
        }
        if (!propagated || !failure_thrown_.load(std::memory_order_relaxed))
            throw std::runtime_error("BVH nested exception did not propagate from "
                                     + rw::backend_name(backend));
        inject_failure_.store(false, std::memory_order_relaxed);
        root_.reset();
        execute(mode);
        if (!validate().correct)
            throw std::runtime_error("BVH did not recover after nested cancellation on "
                                     + rw::backend_name(backend));
    }

    std::size_t task_count() const noexcept { return primitives_.size(); }
    std::size_t observed_concurrency() const noexcept { return concurrency_.maximum(); }
    double primitive_count() const noexcept { return static_cast<double>(primitives_.size()); }

    std::string parameters() const
    {
        std::ostringstream out;
        out << "primitives=" << primitives_.size() << ";leaf_size=" << leaf_size_
            << ";queries=" << queries_.size();
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
        primitives_.reserve(preset_.primitives);
        for (std::size_t i = 0; i < preset_.primitives; ++i)
        {
            Vec3 center{};
            switch (preset_.distribution)
            {
                case Distribution::Uniform:
                    center = {unit_random(state) * 100.0,
                              unit_random(state) * 100.0,
                              unit_random(state) * 100.0};
                    break;
                case Distribution::Clustered:
                {
                    const std::size_t cluster = i % 8;
                    center = {static_cast<double>(cluster & 1u) * 70.0 + unit_random(state) * 5.0,
                              static_cast<double>((cluster >> 1u) & 1u) * 70.0
                                  + unit_random(state) * 5.0,
                              static_cast<double>((cluster >> 2u) & 1u) * 70.0
                                  + unit_random(state) * 5.0};
                    break;
                }
                case Distribution::Skewed:
                {
                    const double t = std::pow(unit_random(state), 8.0);
                    center = {t * 100.0,
                              std::pow(unit_random(state), 5.0) * 10.0,
                              std::pow(unit_random(state), 5.0) * 10.0};
                    break;
                }
                case Distribution::Mixed:
                    if (i % 5 == 0)
                        center = {unit_random(state) * 100.0,
                                  unit_random(state) * 100.0,
                                  unit_random(state) * 100.0};
                    else
                    {
                        const double base = static_cast<double>((i / 5) % 4) * 25.0;
                        center = {base + unit_random(state) * 2.0,
                                  base + unit_random(state) * 2.0,
                                  base + unit_random(state) * 2.0};
                    }
                    break;
            }
            const double ex = 0.01 + unit_random(state) * 0.5;
            const double ey = 0.01 + unit_random(state) * 0.5;
            const double ez = 0.01 + unit_random(state) * 0.5;
            primitives_.push_back({{{center.x - ex, center.y - ey, center.z - ez},
                                    {center.x + ex, center.y + ey, center.z + ez}},
                                   center,
                                   i});
        }
    }

    void generate_queries(std::uint64_t seed)
    {
        std::uint64_t state = seed == 0 ? 1 : seed;
        for (std::size_t i = 0; i < 64; ++i)
        {
            const Vec3 center{unit_random(state) * 100.0,
                              unit_random(state) * 100.0,
                              unit_random(state) * 100.0};
            const double extent = 1.0 + unit_random(state) * 15.0;
            queries_.push_back({{center.x - extent, center.y - extent, center.z - extent},
                                {center.x + extent, center.y + extent, center.z + extent}});
        }
    }

    std::vector<std::size_t> make_all_indices() const
    {
        std::vector<std::size_t> indices(primitives_.size());
        std::iota(indices.begin(), indices.end(), 0);
        return indices;
    }

    Aabb primitive_bounds(const std::vector<std::size_t>& indices) const
    {
        Aabb result;
        for (std::size_t index : indices)
            result = merge(result, primitives_[index].bounds);
        return result;
    }

    std::pair<std::vector<std::size_t>, std::vector<std::size_t>>
    split_indices(std::vector<std::size_t> indices) const
    {
        Aabb centers;
        for (std::size_t index : indices)
        {
            const Vec3 c = primitives_[index].center;
            centers = merge(centers, {c, c});
        }
        const Vec3 extent{centers.maximum.x - centers.minimum.x,
                          centers.maximum.y - centers.minimum.y,
                          centers.maximum.z - centers.minimum.z};
        int axis = 0;
        if (extent.y > extent.x && extent.y >= extent.z)
            axis = 1;
        else if (extent.z > extent.x && extent.z > extent.y)
            axis = 2;
        const std::size_t middle = indices.size() / 2;
        auto coordinate = [&](std::size_t index)
        {
            const Vec3 c = primitives_[index].center;
            return axis == 0 ? c.x : (axis == 1 ? c.y : c.z);
        };
        std::nth_element(indices.begin(), indices.begin() + static_cast<std::ptrdiff_t>(middle),
                         indices.end(), [&](std::size_t left, std::size_t right)
        {
            const double a = coordinate(left);
            const double b = coordinate(right);
            return a == b ? left < right : a < b;
        });
        std::vector<std::size_t> left(indices.begin(),
                                      indices.begin() + static_cast<std::ptrdiff_t>(middle));
        std::vector<std::size_t> right(indices.begin() + static_cast<std::ptrdiff_t>(middle),
                                       indices.end());
        return {std::move(left), std::move(right)};
    }

    std::unique_ptr<Node> make_leaf(std::vector<std::size_t> indices) const
    {
        auto node = std::make_unique<Node>();
        node->bounds = primitive_bounds(indices);
        node->primitives = std::move(indices);
        return node;
    }

    std::unique_ptr<Node> make_parent(std::unique_ptr<Node> left,
                                      std::unique_ptr<Node> right) const
    {
        auto node = std::make_unique<Node>();
        node->left = std::move(left);
        node->right = std::move(right);
        node->bounds = merge(node->left->bounds, node->right->bounds);
        return node;
    }

    bool should_inject(const std::vector<std::size_t>& indices, std::size_t depth)
    {
        if (!inject_failure_.load(std::memory_order_relaxed) || depth < 2)
            return false;
        if (std::find(indices.begin(), indices.end(), std::size_t{0}) == indices.end())
            return false;
        bool expected = false;
        return failure_thrown_.compare_exchange_strong(expected, true, std::memory_order_acq_rel);
    }

    std::unique_ptr<Node> build_sequential(std::vector<std::size_t> indices, std::size_t depth)
    {
        if (should_inject(indices, depth))
            throw std::runtime_error("bvh_injected_failure");
        if (indices.size() <= leaf_size_)
            return make_leaf(std::move(indices));
        auto halves = split_indices(std::move(indices));
        auto left = build_sequential(std::move(halves.first), depth + 1);
        auto right = build_sequential(std::move(halves.second), depth + 1);
        return make_parent(std::move(left), std::move(right));
    }

    std::unique_ptr<Node> build_node(std::vector<std::size_t> indices,
                                     const rw::ModeSpec& mode,
                                     std::size_t schedule_depth,
                                     bool scheduling_allowed,
                                     bool smart,
                                     bool coordinated)
    {
        if (should_inject(indices, schedule_depth))
            throw std::runtime_error("bvh_injected_failure");
        if (indices.size() <= leaf_size_)
            return make_leaf(std::move(indices));
        if (indices.size() < leaf_size_ * 8)
            return build_sequential(std::move(indices), schedule_depth);

        auto halves = split_indices(std::move(indices));
        if (halves.first.size() <= leaf_size_ || halves.second.size() <= leaf_size_)
        {
            auto left = build_node(std::move(halves.first), mode, schedule_depth + 1,
                                   scheduling_allowed, smart, coordinated);
            auto right = build_node(std::move(halves.second), mode, schedule_depth + 1,
                                    scheduling_allowed, smart, coordinated);
            return make_parent(std::move(left), std::move(right));
        }
        auto left_quarters = split_indices(std::move(halves.first));
        auto right_quarters = split_indices(std::move(halves.second));
        std::array<std::vector<std::size_t>, 4> groups{
            std::move(left_quarters.first), std::move(left_quarters.second),
            std::move(right_quarters.first), std::move(right_quarters.second)};
        std::array<std::unique_ptr<Node>, 4> children;

        bool parallel_here = scheduling_allowed;
        if (mode.kind == rw::ModeKind::OuterOnly || mode.kind == rw::ModeKind::ManualBackend)
            parallel_here = schedule_depth == 0;
        else if (mode.kind == rw::ModeKind::InnerOnly)
            parallel_here = schedule_depth == 1;
        else if (mode.kind == rw::ModeKind::AllLevels)
            parallel_here = true;
        else if (mode.kind == rw::ModeKind::Sequential
                 || mode.kind == rw::ModeKind::SmartForcedSequential)
            parallel_here = false;

        const auto build_child = [&](std::size_t child)
        {
            auto active = concurrency_.scope();
            children[child] = build_node(std::move(groups[child]), mode, schedule_depth + 1,
                                         scheduling_allowed, smart, coordinated);
        };
        if (!parallel_here)
            rw::sequential_for(children.size(), build_child);
        else if (smart)
            rw::smart_parallel_for(children.size(),
                                   mode.kind == rw::ModeKind::SmartForcedBackend
                                       ? mode.backend
                                       : smart::ExecutionEngineType::Auto,
                                   build_child);
        else if (coordinated)
            rw::coordinated_parallel_for(children.size(), mode.backend, workers_, build_child);
        else
            rw::fixed_parallel_for(children.size(), mode.backend, workers_, build_child);

        auto left = make_parent(std::move(children[0]), std::move(children[1]));
        auto right = make_parent(std::move(children[2]), std::move(children[3]));
        return make_parent(std::move(left), std::move(right));
    }

    struct FrontierTask
    {
        std::unique_ptr<Node>* slot = nullptr;
        std::vector<std::size_t> indices;
    };

    void make_frontier(std::unique_ptr<Node>& slot,
                       std::vector<std::size_t> indices,
                       std::size_t levels,
                       std::vector<FrontierTask>& tasks)
    {
        if (indices.size() <= leaf_size_ || levels == 0)
        {
            tasks.push_back({&slot, std::move(indices)});
            return;
        }
        auto halves = split_indices(std::move(indices));
        slot = std::make_unique<Node>();
        slot->bounds = merge(primitive_bounds(halves.first), primitive_bounds(halves.second));
        make_frontier(slot->left, std::move(halves.first), levels - 1, tasks);
        make_frontier(slot->right, std::move(halves.second), levels - 1, tasks);
    }

    std::unique_ptr<Node> build_flattened(std::vector<std::size_t> indices,
                                          smart::ExecutionEngineType backend)
    {
        std::unique_ptr<Node> root;
        std::vector<FrontierTask> tasks;
        std::size_t levels = 0;
        std::size_t target = std::max<std::size_t>(workers_ * 4, 1);
        while ((std::size_t{1} << levels) < target && levels < 12)
            ++levels;
        make_frontier(root, std::move(indices), levels, tasks);
        rw::fixed_parallel_for(tasks.size(), backend, workers_, [&](std::size_t task)
        {
            auto active = concurrency_.scope();
            *tasks[task].slot = build_sequential(std::move(tasks[task].indices), levels);
        });
        return root;
    }

    void validate_node(const Node& node,
                       std::vector<unsigned>& occurrences,
                       bool& correct) const
    {
        if (node.leaf())
        {
            correct = correct && !node.primitives.empty()
                && node.primitives.size() <= leaf_size_;
            const Aabb expected = primitive_bounds(node.primitives);
            correct = correct && contains(node.bounds, expected) && contains(expected, node.bounds);
            for (std::size_t primitive : node.primitives)
            {
                if (primitive >= occurrences.size())
                    correct = false;
                else
                    ++occurrences[primitive];
            }
            return;
        }
        correct = correct && node.left && node.right && node.primitives.empty();
        if (!node.left || !node.right)
            return;
        correct = correct && contains(node.bounds, node.left->bounds)
            && contains(node.bounds, node.right->bounds);
        validate_node(*node.left, occurrences, correct);
        validate_node(*node.right, occurrences, correct);
    }

    std::size_t query_count(const Node* node, const Aabb& query) const
    {
        if (!node || !intersects(node->bounds, query))
            return 0;
        if (node->leaf())
        {
            std::size_t count = 0;
            for (std::size_t primitive : node->primitives)
                count += intersects(primitives_[primitive].bounds, query) ? 1u : 0u;
            return count;
        }
        return query_count(node->left.get(), query) + query_count(node->right.get(), query);
    }

    std::size_t brute_force_count(const Aabb& query) const
    {
        return static_cast<std::size_t>(std::count_if(
            primitives_.begin(), primitives_.end(),
            [&](const Primitive& primitive) { return intersects(primitive.bounds, query); }));
    }

    static std::uint64_t query_checksum(const std::vector<std::size_t>& counts)
    {
        std::uint64_t checksum = 0xcbf29ce484222325ull;
        for (std::size_t i = 0; i < counts.size(); ++i)
            checksum = rw::mix64(checksum ^ static_cast<std::uint64_t>(counts[i])
                                 ^ (static_cast<std::uint64_t>(i + 1) << 32));
        return checksum;
    }

    Preset preset_;
    std::size_t workers_ = 1;
    std::size_t leaf_size_ = 8;
    std::vector<Primitive> primitives_;
    std::vector<Aabb> queries_;
    std::vector<std::size_t> expected_query_counts_;
    std::uint64_t expected_checksum_ = 0;
    std::unique_ptr<Node> root_;
    std::atomic<bool> inject_failure_{false};
    std::atomic<bool> failure_thrown_{false};
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
        bool cancellation_checked = false;
        for (const auto& preset : all_presets)
        {
            if (!rw::contains(options.presets, "all")
                && !rw::contains(options.presets, preset.name))
                continue;
            BvhWorkload workload(preset, options.seed, options.workers);
            rw::CaseDefinition definition;
            definition.integration = "bvh";
            definition.workload = "median_split_builder";
            definition.preset = preset.name;
            definition.parameters = workload.parameters();
            definition.unit_name = "primitives_per_second";
            definition.throughput_units = workload.primitive_count();
            definition.task_count = workload.task_count();
            definition.reset = [&] { workload.reset(); };
            definition.execute = [&](const rw::ModeSpec& mode) { workload.execute(mode); };
            definition.validate = [&] { return workload.validate(); };
            definition.observed_concurrency = [&] { return workload.observed_concurrency(); };
            rw::run_case(definition, modes, options, outputs);

            if (!cancellation_checked && preset.primitives >= 20000)
            {
                for (auto backend : rw::selected_backends(options))
                    workload.validate_cancellation_recovery(backend);
                cancellation_checked = true;
            }
        }
        rw::write_outputs(
            "bvh",
            options,
            std::move(outputs),
            {{"bvh_builder", "deterministic_median_split_binary"},
             {"dataset", "deterministic_generated_aabbs"},
             {"cancellation_recovery_validation", cancellation_checked ? "pass" : "not_run"}});
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "BVH real-world benchmark failed: " << error.what() << '\n';
        return 1;
    }
}
