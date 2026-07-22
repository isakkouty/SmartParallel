#include <smart/core/config.hpp>
#include <smart/decision/execution_plan.hpp>
#include <smart/execution/execution_context.hpp>
#include <smart/execution/nested_execution_coordinator.hpp>
#include <smart/execution/thread_pool.hpp>
#include <smart/execution/work_chunk.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;
std::uint64_t burn(std::size_t seed, std::size_t rounds) {
    std::uint64_t x = (seed + 1) * 0x9E3779B185EBCA87ull;
    for (std::size_t i=0;i<rounds;++i) { x ^= x >> 12; x ^= x << 25; x ^= x >> 27; x *= 0x2545F4914F6CDD1Dull; }
    return x;
}
template<class F> double ms(F&& f) { auto a=Clock::now(); f(); return std::chrono::duration<double,std::milli>(Clock::now()-a).count(); }
}

int main() {
    constexpr std::size_t workers=4, iterations=128;
    smart::ExecutionContext parent;
    parent.engine=smart::ExecutionEngineType::ThreadPool;
    parent.parallel=true;
    parent.concurrency_budget=workers;

    smart::ExecutionPlan requested;
    requested.parallel=true;
    requested.engine=smart::ExecutionEngineType::ThreadPool;
    requested.strategy=smart::ExecutionStrategy::DynamicChunks;
    requested.job_count=workers;
    requested.chunk_size=32;

    smart::NestedExecutionCoordinator coordinator;
    auto d=coordinator.coordinate(parent, requested);
    smart::NestedExecutionConstraints c;
    c.iteration_count=iterations;
    c.minimum_iterations_per_worker=8;
    c.minimum_chunks_per_worker=1;
    c.target_chunks_per_worker=2;
    d=coordinator.enforce_constraints(d,c);
    const bool tuned=d.chunk_size_tuned && d.original_chunk_size==32 && d.effective_chunk_size<=16 && d.effective_budget==4;
    std::cout << "Dynamic chunk tuning creates scheduler headroom: " << (tuned?"PASS":"FAIL")
              << " [original="<<d.original_chunk_size<<", tuned="<<d.effective_chunk_size<<", workers="<<d.effective_budget<<"]\n";

    std::vector<std::atomic<int>> visits(iterations); for(auto&v:visits)v=0;
    std::atomic<int> active{0}, max_active{0};
    smart::ThreadPool pool(workers);
    smart::SchedulerVisibleWork work(0, iterations, d.plan.chunk_size);
    const double parallel_ms=ms([&]{
        pool.execute_visible_work(work, d.effective_budget, [&](const smart::WorkChunk& chunk){
            int now=active.fetch_add(1)+1; int seen=max_active.load(); while(now>seen&&!max_active.compare_exchange_weak(seen,now)){}
            for(std::size_t i=chunk.begin;i<chunk.end;++i){ ++visits[i]; (void)burn(i,12000); }
            active.fetch_sub(1);
        });
    });
    bool exact=std::all_of(visits.begin(),visits.end(),[](const auto&v){return v.load()==1;});
    bool bounded=max_active.load()<=static_cast<int>(workers) && work.progress().complete();
    std::cout << "Tuned scheduler-visible execution is exact and bounded: " << ((exact&&bounded)?"PASS":"FAIL")
              << " [iterations="<<iterations<<", max_active="<<max_active.load()<<", chunks="<<work.total_chunks()<<"]\n";

    std::vector<std::uint64_t> seq(iterations), par(iterations);
    const double sequential_ms=ms([&]{for(std::size_t i=0;i<iterations;++i)seq[i]=burn(i,12000);});
    smart::ThreadPool pool2(workers); smart::SchedulerVisibleWork work2(0,iterations,d.plan.chunk_size);
    const double tuned_ms=ms([&]{pool2.execute_visible_work(work2,workers,[&](const smart::WorkChunk& ch){for(std::size_t i=ch.begin;i<ch.end;++i)par[i]=burn(i,12000);});});
    const bool match=seq==par;
    const double ratio=sequential_ms>0.0?tuned_ms/sequential_ms:0.0;
    const bool sane=match && ratio<8.0;
    std::cout<<std::fixed<<std::setprecision(2);
    std::cout << "Performance validation remains non-pathological: " << (sane?"PASS":"FAIL")
              << " [sequential="<<sequential_ms<<" ms, tuned="<<tuned_ms<<" ms, ratio="<<ratio<<"x]\n";
    const bool pass=tuned&&exact&&bounded&&sane;
    std::cout << (pass ? "PASS: performance tuning and scheduler validation are correct.\n" : "FAIL: performance tuning and scheduler validation are incorrect.\n");
    return pass?0:1;
}
