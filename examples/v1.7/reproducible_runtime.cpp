#include <smart/data/view.hpp>
#include <smart/linalg/operations.hpp>
#include <smart/runtime/runtime.hpp>

#include <cstddef>
#include <iostream>
#include <vector>

int main()
{
    smart::RuntimeOptions options;
    options.worker_budget = 4;
    options.default_numerical_policy = smart::NumericalPolicy::Reproducible;
    smart::Runtime runtime(options);

    std::vector<double> x(4096, 2.0), y(4096, 1.0);
    auto xv = smart::data::VectorView<const double>::contiguous(x.data(), {x.size()});
    auto yv = smart::data::VectorView<double>::contiguous(y.data(), {y.size()});
    smart::linalg::axpy(runtime.context(), yv, 0.5, xv,
        smart::NumericalOptions{smart::NumericalPolicy::Reproducible});

    std::cout << "runtime_fingerprint=" << runtime.fingerprint().hash << '\n'
              << "operation_fingerprint=" << runtime.last_operation_fingerprint().hash << '\n';
}
