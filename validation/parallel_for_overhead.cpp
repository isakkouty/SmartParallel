#include <chrono>
#include <cstddef>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <smart/execution/parallel.hpp>

template <typename F>
static double ms(F&& f) {
    const auto start=std::chrono::steady_clock::now(); f();
    return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
}
int main(int argc,char**argv){
    const char* path=argc>1?argv[1]:"validation/output/parallel_for_overhead.csv";
    std::ofstream out(path); out<<"iterations,sequential_ms,smart_cold_ms,smart_cached_ms,cold_overhead_ms,cached_overhead_ms\n";
    smart::global_config().enable_experience=false;
    smart::global_config().enable_parallel_for_profile_cache=true;
    for(std::size_t n: {10u,100u,1000u,10000u,100000u}){
        volatile std::size_t sink=0;
        auto body=[&](std::size_t i){ sink += (i&1u); };
        smart::global_function_profile_cache().clear();
        double seq=ms([&]{for(std::size_t i=0;i<n;++i) body(i);});
        double cold=ms([&]{smart::parallel_for(0,n,body);});
        double cached=ms([&]{smart::parallel_for(0,n,body);});
        out<<n<<','<<std::setprecision(10)<<seq<<','<<cold<<','<<cached<<','<<(cold-seq)<<','<<(cached-seq)<<'\n';
        std::cout<<n<<" seq="<<seq<<" ms cold="<<cold<<" ms cached="<<cached<<" ms\n";
    }
    return 0;
}
