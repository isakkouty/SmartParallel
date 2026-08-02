#include <smart/execution/parallel.hpp>
#include <smart/runtime/runtime.hpp>
#include <atomic>
#include <iostream>
#include <memory>
#include <thread>
int main(){auto governor=std::make_shared<smart::ResourceGovernor>(smart::ResourceGovernorOptions{2,4});smart::RuntimeOptions o;o.governor=governor;o.maximum_workers=2;o.lease_wait_policy=smart::LeaseWaitPolicy::Wait;o.scheduler_config.execution_engine=smart::ExecutionEngineType::ThreadPool;o.scheduler_config.enable_parallel_for_auto_profiling=false;o.scheduler_config.enable_parallel_for_profile_cache=false;o.scheduler_config.enable_parallel_for_backend_calibration=false;o.scheduler_config.enable_experience=false;o.scheduler_config.small_workload_iteration_threshold=0;o.scheduler_config.cheap_workload_sequential_threshold=0;smart::Runtime a(o),b(o);std::atomic<std::size_t> visits{0};auto run=[&](smart::Runtime&r){smart::parallel_for(r.context(),std::size_t{0},std::size_t{128},[&](std::size_t){visits.fetch_add(1);});};std::thread first(run,std::ref(a)),second(run,std::ref(b));first.join();second.join();auto s=governor->snapshot();if(visits.load()!=256||s.active_permits!=0||s.maximum_active_permits>2){std::cerr<<"governor consumer failed\n";return 1;}std::cout<<"governor consumer passed\n";return 0;}
