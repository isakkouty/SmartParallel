#include <smart/data/view.hpp>
#include <smart/numerical/policy.hpp>
#include <smart/scientific/stencil.hpp>

#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
smart::NumericalPolicy parse_policy(const std::string& text)
{
    if (text == "fast") return smart::NumericalPolicy::Fast;
    if (text == "reproducible") return smart::NumericalPolicy::Reproducible;
    if (text == "accurate") return smart::NumericalPolicy::Accurate;
    throw std::invalid_argument("policy must be fast, reproducible, or accurate");
}

void initialize(std::vector<double>& field, std::size_t rows, std::size_t columns)
{
    for (std::size_t row = 0; row < rows; ++row)
    {
        for (std::size_t column = 0; column < columns; ++column)
        {
            const bool boundary = row == 0 || column == 0 || row + 1 == rows || column + 1 == columns;
            field[row * columns + column] = boundary ? 100.0 : 10.0 + ((row * 17 + column * 13) % 11);
        }
    }
}

void reference_step(const std::vector<double>& input,
                    std::vector<double>& output,
                    std::size_t rows,
                    std::size_t columns)
{
    output = input;
    for (std::size_t row = 1; row + 1 < rows; ++row)
        for (std::size_t column = 1; column + 1 < columns; ++column)
            output[row * columns + column] =
                0.5 * input[row * columns + column]
                + 0.125 * input[(row - 1) * columns + column]
                + 0.125 * input[(row + 1) * columns + column]
                + 0.125 * input[row * columns + column - 1]
                + 0.125 * input[row * columns + column + 1];
}
}

int main(int argc, char** argv)
{
    try
    {
        const std::size_t rows = argc > 1 ? static_cast<std::size_t>(std::stoull(argv[1])) : 256;
        const std::size_t columns = argc > 2 ? static_cast<std::size_t>(std::stoull(argv[2])) : rows;
        const std::size_t iterations = argc > 3 ? static_cast<std::size_t>(std::stoull(argv[3])) : 100;
        const smart::NumericalPolicy policy = argc > 4 ? parse_policy(argv[4])
                                                       : smart::NumericalPolicy::Reproducible;
        if (rows < 1 || columns < 1)
            throw std::invalid_argument("grid dimensions must be positive");

        std::vector<double> first(rows * columns);
        std::vector<double> second(rows * columns);
        initialize(first, rows, columns);
        second = first;
        const auto initial = first;
        smart::scientific::Stencil2DCoefficients<double> coefficients{
            0.5, 0.125, 0.125, 0.125, 0.125};
        const auto start = std::chrono::steady_clock::now();
        for (std::size_t iteration = 0; iteration < iterations; ++iteration)
        {
            auto input = smart::data::MatrixView<const double>::contiguous(
                first.data(), {rows, columns});
            auto output = smart::data::MatrixView<double>::contiguous(
                second.data(), {rows, columns});
            smart::scientific::stencil_2d(
                input, output, coefficients, smart::NumericalOptions{policy});
            first.swap(second);
        }
        const double elapsed_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();

        bool reference_ok = true;
        if (rows * columns <= 65536 && iterations <= 1000)
        {
            std::vector<double> reference = initial;
            std::vector<double> scratch = reference;
            for (std::size_t iteration = 0; iteration < iterations; ++iteration)
            {
                reference_step(reference, scratch, rows, columns);
                reference.swap(scratch);
            }
            for (std::size_t index = 0; index < reference.size(); ++index)
                if (first[index] != reference[index]) { reference_ok = false; break; }
        }

        long double checksum = 0.0L;
        for (double value : first) checksum += value;
        const auto& report = smart::global_last_numerical_execution_report();
        std::cout << "SmartParallel v1.6 heat diffusion\n"
                  << "grid=" << rows << 'x' << columns << " iterations=" << iterations << '\n'
                  << "policy=" << smart::numerical_policy_name(policy)
                  << " plan=" << report.canonical_plan
                  << " scheduler=" << report.scheduler << '\n'
                  << "reference=" << (reference_ok ? "PASS" : "FAIL") << '\n'
                  << std::setprecision(17) << "checksum=" << checksum << '\n'
                  << "elapsed_ms=" << elapsed_ms << '\n';
        return reference_ok ? 0 : 1;
    }
    catch (const std::exception& error)
    {
        std::cerr << "heat_diffusion error: " << error.what() << '\n';
        return 2;
    }
}
