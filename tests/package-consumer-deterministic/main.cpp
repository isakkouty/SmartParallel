#include <smart/execution/parallel.hpp>
#include <smart/runtime/runtime.hpp>
#include <algorithm>
#include <iostream>
#include <memory>
#include <vector>
int main(){auto g=std::make_shared<smart::ResourceGovernor>(smart::ResourceGovernorOptions{2,2});smart::LeaseRequest q;q.requested_workers=1;q.minimum_workers=1;q.exact_grant_required=true;auto held=g->acquire(q);if(!held)return 2;smart::RuntimeOptions o;o.governor=g;o.maximum_workers=2;o.execution_mode=smart::ExecutionMode::Deterministic;o.profile_access=smart::ProfileAccess::Disabled;o.lease_wait_policy=smart::LeaseWaitPolicy::FailImmediately;o.scheduler_config.execution_engine=smart::ExecutionEngineType::ThreadPool;smart::Runtime r(o);std::vector<int> output(64,7);bool rejected=false;try{smart::parallel_for(r.context(),std::size_t{0},output.size(),[&](std::size_t i){output[i]=9;});}catch(const std::runtime_error&){rejected=true;}if(!rejected||!std::all_of(output.begin(),output.end(),[](int v){return v==7;})){std::cerr<<"deterministic governor consumer failed\n";return 1;}std::cout<<"deterministic governor consumer passed\n";return 0;}
