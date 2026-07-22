#include "real_world_benchmark.hpp"

#include <opencv2/core.hpp>
#include <opencv2/core/utility.hpp>
#include <opencv2/imgproc.hpp>

#include <array>
#include <cstring>
#include <random>

namespace rw = smart::real_world;

namespace
{
struct ImageShape
{
    int width = 0;
    int height = 0;
};

struct Preset
{
    std::string name;
    std::vector<ImageShape> shapes;
    int tile_size = 128;
};

std::vector<Preset> presets()
{
    std::vector<Preset> result;
    result.push_back({"tiny", {{64, 64}, {80, 48}, {48, 80}, {96, 64}}, 64});
    result.push_back({"one_large", {{2048, 2048}}, 256});
    result.push_back({"few_large", {{1536, 1024}, {1280, 1280}, {1920, 1080}, {1024, 1536}}, 256});

    Preset medium{"many_medium", {}, 128};
    medium.shapes.assign(64, {512, 512});
    result.push_back(std::move(medium));

    Preset small{"thousands_small", {}, 64};
    small.shapes.assign(1000, {64, 64});
    result.push_back(std::move(small));

    Preset mixed{"mixed_sizes", {}, 128};
    const std::array<ImageShape, 8> pattern{{
        {64, 64}, {128, 96}, {256, 192}, {512, 384},
        {1024, 768}, {320, 640}, {1536, 1024}, {96, 160}}};
    for (std::size_t i = 0; i < 64; ++i)
        mixed.shapes.push_back(pattern[i % pattern.size()]);
    result.push_back(std::move(mixed));
    return result;
}

struct Tile
{
    std::size_t image = 0;
    cv::Rect region;
};

struct WeightedWorkUnit
{
    std::size_t image = 0;
    std::size_t first_local_tile = 0;
    std::size_t last_local_tile = 0;
    std::uint64_t estimated_pixels = 0;
};

class ImagePipelineWorkload
{
  public:
    ImagePipelineWorkload(Preset preset, std::uint64_t seed, std::size_t workers)
        : preset_(std::move(preset)), workers_(workers)
    {
        generate_images(seed);
        build_tiles();
        build_weighted_work_units();
        output_.resize(tiles_.size());
        expected_.resize(tiles_.size());
        for (std::size_t i = 0; i < tiles_.size(); ++i)
            expected_[i] = process_tile(tiles_[i]);
        expected_checksum_ = checksum(expected_);
    }

    void reset()
    {
        std::fill(output_.begin(), output_.end(), 0);
        concurrency_.reset();
    }

    void execute(const rw::ModeSpec& mode)
    {
        switch (mode.kind)
        {
            case rw::ModeKind::Sequential:
            case rw::ModeKind::SmartForcedSequential:
                run_weighted_outer(mode, false, false, false);
                break;
            case rw::ModeKind::ManualBackend:
            case rw::ModeKind::OuterOnly:
                run_weighted_outer(mode, true, false, false);
                break;
            case rw::ModeKind::InnerOnly:
                run_nested_images(mode, false, true, false);
                break;
            case rw::ModeKind::AllLevels:
                run_weighted_outer(mode, true, true, false);
                break;
            case rw::ModeKind::SmartAuto:
            case rw::ModeKind::SmartForcedBackend:
                run_weighted_outer(mode, true, true, true);
                break;
            case rw::ModeKind::Flattened:
                rw::fixed_parallel_for(tiles_.size(), mode.backend, workers_,
                                       [&](std::size_t tile) { output_[tile] = process_tile(tiles_[tile]); });
                break;
        }
    }

    rw::ValidationResult validate() const
    {
        rw::ValidationResult result;
        result.checksum = checksum(output_);
        result.expected_checksum = expected_checksum_;
        result.correct = output_ == expected_ && result.checksum == result.expected_checksum;
        if (!result.correct)
            result.message = "OpenCV tile-output mismatch";
        return result;
    }

    std::size_t task_count() const noexcept { return tiles_.size(); }
    std::size_t observed_concurrency() const noexcept { return concurrency_.maximum(); }

    double pixels() const noexcept
    {
        double total = 0.0;
        for (const auto& image : images_)
            total += static_cast<double>(image.total());
        return total;
    }

    std::string parameters() const
    {
        std::ostringstream out;
        out << "images=" << images_.size() << ";tiles=" << tiles_.size()
            << ";weighted_units=" << weighted_units_.size()
            << ";tile=" << preset_.tile_size << ";pixels=" << static_cast<std::uint64_t>(pixels());
        return out.str();
    }

  private:
    void generate_images(std::uint64_t seed)
    {
        images_.reserve(preset_.shapes.size());
        for (std::size_t image_index = 0; image_index < preset_.shapes.size(); ++image_index)
        {
            const auto shape = preset_.shapes[image_index];
            cv::Mat image(shape.height, shape.width, CV_8UC3);
            for (int y = 0; y < image.rows; ++y)
            {
                auto* row = image.ptr<cv::Vec3b>(y);
                for (int x = 0; x < image.cols; ++x)
                {
                    const std::uint64_t value = rw::mix64(seed
                        ^ (static_cast<std::uint64_t>(image_index + 1) << 48)
                        ^ (static_cast<std::uint64_t>(y) << 24)
                        ^ static_cast<std::uint64_t>(x));
                    row[x] = cv::Vec3b{
                        static_cast<unsigned char>(value),
                        static_cast<unsigned char>(value >> 8),
                        static_cast<unsigned char>(value >> 16)};
                }
            }
            images_.push_back(std::move(image));
        }
    }

    void build_tiles()
    {
        image_tiles_.resize(images_.size());
        for (std::size_t image_index = 0; image_index < images_.size(); ++image_index)
        {
            const cv::Mat& image = images_[image_index];
            for (int y = 0; y < image.rows; y += preset_.tile_size)
            {
                for (int x = 0; x < image.cols; x += preset_.tile_size)
                {
                    const cv::Rect region(
                        x,
                        y,
                        std::min(preset_.tile_size, image.cols - x),
                        std::min(preset_.tile_size, image.rows - y));
                    image_tiles_[image_index].push_back(tiles_.size());
                    tiles_.push_back({image_index, region});
                }
            }
        }
    }

    void build_weighted_work_units()
    {
        weighted_units_.clear();
        std::uint64_t total_pixels = 0;
        std::vector<std::uint64_t> image_pixels(images_.size(), 0);
        for (std::size_t image = 0; image < images_.size(); ++image)
        {
            image_pixels[image] = static_cast<std::uint64_t>(images_[image].total());
            total_pixels += image_pixels[image];
        }

        const std::uint64_t worker_target = std::max<std::uint64_t>(
            1, total_pixels / std::max<std::size_t>(1, workers_));
        const std::uint64_t split_threshold = worker_target + worker_target / 4;
        const std::uint64_t unit_target = std::max<std::uint64_t>(
            1, total_pixels / std::max<std::size_t>(1, workers_ * 2));

        for (std::size_t image = 0; image < images_.size(); ++image)
        {
            const auto& local_tiles = image_tiles_[image];
            if (local_tiles.empty())
                continue;
            if (image_pixels[image] <= split_threshold || local_tiles.size() == 1)
            {
                weighted_units_.push_back(
                    {image, 0, local_tiles.size(), image_pixels[image]});
                continue;
            }

            std::size_t first = 0;
            std::uint64_t accumulated = 0;
            for (std::size_t local = 0; local < local_tiles.size(); ++local)
            {
                const cv::Rect& region = tiles_[local_tiles[local]].region;
                accumulated += static_cast<std::uint64_t>(region.width)
                    * static_cast<std::uint64_t>(region.height);
                const bool close_unit = accumulated >= unit_target
                    || local + 1 == local_tiles.size();
                if (close_unit)
                {
                    weighted_units_.push_back(
                        {image, first, local + 1, accumulated});
                    first = local + 1;
                    accumulated = 0;
                }
            }
        }

        std::stable_sort(
            weighted_units_.begin(), weighted_units_.end(),
            [](const WeightedWorkUnit& left, const WeightedWorkUnit& right)
            {
                if (left.estimated_pixels != right.estimated_pixels)
                    return left.estimated_pixels > right.estimated_pixels;
                if (left.image != right.image)
                    return left.image < right.image;
                return left.first_local_tile < right.first_local_tile;
            });
    }

    std::uint64_t process_tile(const Tile& tile)
    {
        auto active = concurrency_.scope();
        const cv::Mat source = images_[tile.image](tile.region);
        cv::Mat resized;
        cv::Mat gray;
        cv::Mat blurred;
        cv::Mat edges;
        cv::Mat thresholded;
        cv::Mat closed;
        const int target_width = std::max(16, source.cols * 3 / 4);
        const int target_height = std::max(16, source.rows * 3 / 4);
        cv::resize(source, resized, cv::Size(target_width, target_height), 0.0, 0.0, cv::INTER_AREA);
        cv::cvtColor(resized, gray, cv::COLOR_BGR2GRAY);
        cv::GaussianBlur(gray, blurred, cv::Size(5, 5), 1.1, 1.1, cv::BORDER_REFLECT_101);
        cv::Canny(blurred, edges, 48.0, 144.0, 3, false);
        cv::threshold(edges, thresholded, 32.0, 255.0, cv::THRESH_BINARY);
        const cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3));
        cv::morphologyEx(thresholded, closed, cv::MORPH_CLOSE, kernel);
        std::uint64_t hash = rw::hash_bytes(closed.data, closed.total() * closed.elemSize());
        hash = rw::mix64(hash ^ static_cast<std::uint64_t>(tile.image)
                         ^ (static_cast<std::uint64_t>(tile.region.x) << 16)
                         ^ (static_cast<std::uint64_t>(tile.region.y) << 32));
        return hash;
    }

    static std::uint64_t checksum(const std::vector<std::uint64_t>& values)
    {
        std::uint64_t result = 0xcbf29ce484222325ull;
        for (std::size_t i = 0; i < values.size(); ++i)
            result = rw::mix64(result ^ values[i] ^ static_cast<std::uint64_t>(i + 1));
        return result;
    }

    void process_image(std::size_t image,
                       const rw::ModeSpec& mode,
                       bool inner_parallel,
                       bool smart)
    {
        const auto& tile_indices = image_tiles_[image];
        const auto body = [&](std::size_t local)
        {
            const std::size_t tile_index = tile_indices[local];
            output_[tile_index] = process_tile(tiles_[tile_index]);
        };
        if (!inner_parallel)
            rw::sequential_for(tile_indices.size(), body);
        else if (smart)
            rw::smart_parallel_for(tile_indices.size(),
                                   mode.kind == rw::ModeKind::SmartForcedBackend
                                       ? mode.backend
                                       : smart::ExecutionEngineType::Auto,
                                   body);
        else
            rw::coordinated_parallel_for(tile_indices.size(), mode.backend, workers_, body);
    }

    void process_weighted_unit(std::size_t unit_index,
                               const rw::ModeSpec& mode,
                               bool inner_parallel,
                               bool smart)
    {
        const WeightedWorkUnit& unit = weighted_units_[unit_index];
        const auto& tile_indices = image_tiles_[unit.image];
        const std::size_t count = unit.last_local_tile - unit.first_local_tile;
        const auto body = [&](std::size_t local)
        {
            const std::size_t tile_index = tile_indices[unit.first_local_tile + local];
            output_[tile_index] = process_tile(tiles_[tile_index]);
        };
        if (!inner_parallel || count <= 1)
            rw::sequential_for(count, body);
        else if (smart)
            rw::smart_parallel_for(count,
                                   mode.kind == rw::ModeKind::SmartForcedBackend
                                       ? mode.backend
                                       : smart::ExecutionEngineType::Auto,
                                   body);
        else
            rw::coordinated_parallel_for(count, mode.backend, workers_, body);
    }

    void run_weighted_outer(const rw::ModeSpec& mode,
                            bool outer_parallel,
                            bool inner_parallel,
                            bool smart)
    {
        const auto outer = [&](std::size_t unit)
        {
            process_weighted_unit(unit, mode, inner_parallel, smart);
        };
        if (smart)
        {
            rw::smart_parallel_for(weighted_units_.size(),
                                   mode.kind == rw::ModeKind::SmartForcedBackend
                                       ? mode.backend
                                       : smart::ExecutionEngineType::Auto,
                                   outer);
            return;
        }
        if (!outer_parallel)
        {
            rw::sequential_for(weighted_units_.size(), outer);
            return;
        }
        if (mode.kind == rw::ModeKind::ManualBackend)
        {
            rw::fixed_parallel_for(weighted_units_.size(), mode.backend, workers_, outer);
            return;
        }
        rw::with_fixed_root_session(mode.backend, workers_, [&]
        {
            rw::coordinated_parallel_for(weighted_units_.size(), mode.backend, workers_, outer);
        });
    }

    void run_nested_images(const rw::ModeSpec& mode,
                    bool outer_parallel,
                    bool inner_parallel,
                    bool smart)
    {
        const auto outer = [&](std::size_t image)
        {
            process_image(image, mode, inner_parallel, smart);
        };
        if (smart)
        {
            rw::smart_parallel_for(images_.size(),
                                   mode.kind == rw::ModeKind::SmartForcedBackend
                                       ? mode.backend
                                       : smart::ExecutionEngineType::Auto,
                                   outer);
            return;
        }
        if (!outer_parallel)
        {
            if (inner_parallel)
            {
                rw::with_fixed_root_session(mode.backend, workers_, [&]
                {
                    rw::sequential_for(images_.size(), outer);
                });
            }
            else
            {
                rw::sequential_for(images_.size(), outer);
            }
            return;
        }
        if (mode.kind == rw::ModeKind::ManualBackend)
        {
            rw::fixed_parallel_for(images_.size(), mode.backend, workers_, outer);
            return;
        }
        rw::with_fixed_root_session(mode.backend, workers_, [&]
        {
            rw::coordinated_parallel_for(images_.size(), mode.backend, workers_, outer);
        });
    }

    Preset preset_;
    std::size_t workers_ = 1;
    std::vector<cv::Mat> images_;
    std::vector<Tile> tiles_;
    std::vector<std::vector<std::size_t>> image_tiles_;
    std::vector<WeightedWorkUnit> weighted_units_;
    std::vector<std::uint64_t> output_;
    std::vector<std::uint64_t> expected_;
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

        cv::setNumThreads(1);
        cv::setUseOptimized(true);
        const auto modes = rw::make_modes(options, true);
        rw::BenchmarkOutputs outputs;
        for (const auto& preset : all_presets)
        {
            if (!rw::contains(options.presets, "all")
                && !rw::contains(options.presets, preset.name))
                continue;
            ImagePipelineWorkload workload(preset, options.seed, options.workers);
            rw::CaseDefinition definition;
            definition.integration = "opencv";
            definition.workload = "image_pipeline";
            definition.preset = preset.name;
            definition.parameters = workload.parameters();
            definition.unit_name = "pixels_per_second";
            definition.throughput_units = workload.pixels();
            definition.task_count = workload.task_count();
            definition.reset = [&] { workload.reset(); };
            definition.execute = [&](const rw::ModeSpec& mode) { workload.execute(mode); };
            definition.validate = [&] { return workload.validate(); };
            definition.observed_concurrency = [&] { return workload.observed_concurrency(); };
            rw::run_case(definition, modes, options, outputs);
        }
        rw::write_outputs(
            "opencv",
            options,
            std::move(outputs),
            {{"opencv_version", CV_VERSION}, {"opencv_internal_threads", "1"},
             {"dataset", "deterministic_generated_images"}});
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "OpenCV real-world benchmark failed: " << error.what() << '\n';
        return 1;
    }
}
