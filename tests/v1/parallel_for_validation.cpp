#include <atomic>
#include <chrono>
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <smart/execution/parallel.hpp>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void reset() {
    smart::global_config().enable_experience = false;
    smart::global_config().enable_utility_model_runtime = false;
    smart::global_config().enable_parallel_for_auto_profiling = true;
    smart::global_config().enable_parallel_for_profile_cache = false;
    smart::global_config().enable_parallel_for_cached_sequential_fast_path = true;
    smart::global_config().parallel_for_sequential_fast_path_min_observations = 3;
    smart::global_config().parallel_for_sequential_fast_path_speedup_margin = 0.85;
    smart::global_config().parallel_for_sequential_fast_path_revalidate_interval = 16;
    smart::global_function_profile_cache().clear();
}
void exactly_once(std::size_t begin, std::size_t end) {
    std::vector<std::atomic<unsigned>> visits(end - begin);
    for (auto& v : visits) v.store(0);
    smart::parallel_for(begin, end, [&](std::size_t i) { visits[i - begin].fetch_add(1); });
    for (auto& v : visits) require(v.load() == 1, "index skipped or duplicated");
}
void burn(std::size_t rounds, std::size_t seed) {
    volatile double x = static_cast<double>(seed + 1);
    for (std::size_t k=0;k<rounds;++k) x = x * 1.0000001 + static_cast<double>(k & 7);
    (void)x;
}
}
int main() {
    try {
    reset();
    exactly_once(0, 0); exactly_once(0, 1); exactly_once(17, 18); exactly_once(11, 4099);
    bool threw=false; try { smart::parallel_for(9, 2, [](std::size_t){}); } catch(const std::invalid_argument&) { threw=true; }
    require(threw, "invalid range did not throw");

    // Deterministically verify that a tiny cheap loop remains sequential.
    const double saved_signal = smart::global_config().parallel_for_profile_min_signal_ms;
    smart::global_config().parallel_for_profile_min_signal_ms = 1000.0;
    smart::parallel_for(0, 100, [](std::size_t){});
    require(!smart::global_last_decision_report().plan.parallel, "cheap loop should remain sequential");
    smart::global_config().parallel_for_profile_min_signal_ms = saved_signal;

    std::atomic<std::size_t> count{0};
    smart::parallel_for(0, 4096, [&](std::size_t i){ count.fetch_add(1); burn(300, i); });
    require(count.load()==4096, "heavy callback count mismatch");
    require(smart::global_last_parallel_for_profile_diagnostics().profile_available, "heavy callback was not profiled");
    require(smart::global_last_decision_report().plan.parallel, "heavy loop should become parallel");

    // Cheap prefix, expensive tail: regional sampling must observe later work.
    smart::parallel_for(0, 6000, [&](std::size_t i){ if (i >= 2000) burn(600, i); });
    require(smart::global_last_parallel_for_profile_diagnostics().predicted_speedup >= 1.0,
            "regional sampler missed expensive tail");

    // Expensive prefix, cheap tail remains correct and produces a bounded estimate.
    exactly_once(0, 6000);
    smart::parallel_for(0, 6000, [&](std::size_t i){ if (i < 2000) burn(600, i); });
    require(smart::global_last_parallel_for_profile_diagnostics().estimated_sequential_ms >= 0.0,
            "invalid profile estimate");

    // Parallel cache reuse: independent confirmation is only required for cached
    // sequential candidates. A clearly parallel profile should skip sampling on
    // the second call.
    reset(); smart::global_config().enable_parallel_for_profile_cache = true;
    auto cached_loop = [](std::size_t i){ burn(350, i); };
    smart::parallel_for(0, 4096, cached_loop);
    require(!smart::global_last_parallel_for_profile_diagnostics().cache_hit, "unexpected first-call cache hit");
    smart::parallel_for(0, 4096, cached_loop);
    require(smart::global_last_parallel_for_profile_diagnostics().cache_hit, "profile cache was not reused");
    require(smart::global_last_parallel_for_profile_diagnostics().sampled_iterations == 0, "cached call still sampled");

    // Cached cheap callbacks require independent agreement before bypassing
    // analysis. Cache hits alone do not create confidence.
    reset(); smart::global_config().enable_parallel_for_profile_cache = true;
    smart::global_config().parallel_for_sequential_fast_path_min_observations = 3;
    smart::global_config().parallel_for_sequential_fast_path_revalidate_interval = 16;
    auto cheap_cached_loop = [](std::size_t){};
    smart::parallel_for(0, 16384, cheap_cached_loop);
    require(smart::global_last_parallel_for_profile_diagnostics().sampled_iterations > 0,
            "first cheap observation was not sampled");
    smart::parallel_for(0, 16384, cheap_cached_loop);
    require(smart::global_last_parallel_for_profile_diagnostics().sampled_iterations > 0,
            "second cheap observation incorrectly trusted a cache hit");
    smart::parallel_for(0, 16384, cheap_cached_loop);
    require(smart::global_last_parallel_for_profile_diagnostics().sampled_iterations > 0,
            "third cheap observation incorrectly trusted a cache hit");
    smart::parallel_for(0, 16384, cheap_cached_loop);
    require(smart::global_last_parallel_for_profile_diagnostics().cache_hit,
            "confirmed cheap callback profile cache was not reused");
    require(smart::global_last_parallel_for_profile_diagnostics().sequential_fast_path,
            "confirmed cheap callback did not use sequential fast path");

    // A sequential classification must not become permanent. After the configured
    // number of bypasses, the same callable type is sampled again. A contradictory
    // expensive observation must immediately escape the sequential fast path.
    reset(); smart::global_config().enable_parallel_for_profile_cache = true;
    smart::global_config().parallel_for_sequential_fast_path_min_observations = 2;
    smart::global_config().parallel_for_sequential_fast_path_revalidate_interval = 1;
    smart::global_config().parallel_for_profile_min_signal_ms = 1000.0;
    bool expensive_now = false;
    auto changing_loop = [&](std::size_t i){ if (expensive_now) burn(4000, i); };
    smart::parallel_for(0, 16384, changing_loop);
    smart::parallel_for(0, 16384, changing_loop);
    smart::parallel_for(0, 16384, changing_loop);
    require(smart::global_last_parallel_for_profile_diagnostics().sequential_fast_path,
            "confirmed cheap callback did not enter fast path");
    expensive_now = true;
    smart::parallel_for(0, 16384, changing_loop);
    require(smart::global_last_parallel_for_profile_diagnostics().sampled_iterations > 0,
            "sequential cache entry was not periodically revalidated");
    require(!smart::global_last_parallel_for_profile_diagnostics().sequential_fast_path,
            "contradictory expensive sample remained locked to sequential");
    require(smart::global_last_decision_report().plan.parallel,
            "expensive callback did not promote back to parallel execution");
    smart::global_config().parallel_for_profile_min_signal_ms = saved_signal;

    // Exception propagation; partial completion is allowed, swallowing is not.
    bool callback_threw=false;
    try { smart::parallel_for(0, 100, [](std::size_t i){ if (i==0) throw std::runtime_error("expected"); }); }
    catch(const std::runtime_error&) { callback_threw=true; }
    require(callback_threw, "callback exception was swallowed");

    std::cout << "parallel_for validation: PASS\n";
    return 0;
    } catch (const std::exception& error) {
        std::cerr << "parallel_for validation: FAIL: " << error.what() << "\n";
        return 1;
    }
}
