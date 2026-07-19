#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <iomanip>
#include <limits>
#include <mutex>
#include <smart/core/config.hpp>
#include <smart/decision/plan_prediction.hpp>
#include <smart/decision/residual_features.hpp>
#include <string>

namespace smart
{
struct HierarchicalResidualResult
{
    bool applied = false;
    double base_runtime_ms = 0.0;
    double corrected_runtime_ms = 0.0;
    double log_residual = 0.0;
    double factor = 1.0;
    double confidence = 0.0;

    double log_standard_deviation = 0.0;
    double runtime_standard_deviation_ms = 0.0;
    double shared_contribution = 0.0;
    double backend_contribution = 0.0;
    double family_backend_contribution = 0.0;

    double shared_confidence = 0.0;

    double backend_confidence = 0.0;
    double family_backend_confidence = 0.0;
    std::size_t shared_samples = 0;
    std::size_t backend_samples = 0;
    std::size_t family_backend_samples = 0;
};

namespace detail
{
template <std::size_t N>
class OnlineRidgeModel
{
  public:
    struct Prediction
    {
        double mean = 0.0;
        double standard_deviation = 0.35;
        double confidence = 0.0;
        std::size_t samples = 0;
        bool available = false;
    };

    void clear()
    {
        matrix_ = {};
        rhs_ = {};
        observations_ = 0;
        effective_samples_ = 0.0;
        residual_mean_ = 0.0;
        residual_m2_ = 0.0;
        residual_weight_ = 0.0;
    }

    Prediction predict(const std::array<double, N>& features,
                       std::size_t minimum_samples,
                       std::size_t full_confidence_samples,
                       double ridge_lambda,
                       double variance_scale,
                       double feature_confidence) const
    {
        Prediction result;
        result.samples = observations_;
        if (observations_ == 0 || effective_samples_ <= 0.0)
            return result;

        std::array<double, N> coefficients{};
        if (!solve_system(rhs_, coefficients, ridge_lambda))
            return result;

        result.mean = dot(coefficients, features);
        const double variance =
            residual_weight_ > 1.0 ? std::max(1.0e-4, residual_m2_ / residual_weight_) : 0.12;

        std::array<double, N> leverage_solution{};
        double leverage = 1.0;
        if (solve_system(features, leverage_solution, ridge_lambda))
        {
            leverage = std::max(0.0, dot(features, leverage_solution));
        }
        result.standard_deviation = std::sqrt(variance * (1.0 + std::min(4.0, leverage)));

        if (observations_ < minimum_samples)
            return result;

        const double sample_confidence = std::clamp(
            effective_samples_
                / (effective_samples_
                   + static_cast<double>(std::max<std::size_t>(1, full_confidence_samples))),
            0.0,
            1.0);
        const double stability_confidence =
            std::clamp(std::exp(-variance / std::max(1.0e-6, variance_scale)), 0.0, 1.0);
        result.confidence = std::clamp(sample_confidence * stability_confidence
                                           * std::clamp(feature_confidence, 0.0, 1.0),
                                       0.0,
                                       1.0);
        result.available = result.confidence > 0.0;
        return result;
    }

    void update(const std::array<double, N>& features,
                double target,
                double weight,
                double decay,
                double ridge_lambda)
    {
        if (!std::isfinite(target) || !std::isfinite(weight) || weight <= 0.0)
        {
            return;
        }

        const double effective_decay = std::clamp(decay, 0.90, 1.0);
        for (std::size_t row = 0; row < N; ++row)
        {
            rhs_[row] *= effective_decay;
            for (std::size_t column = 0; column < N; ++column)
                matrix_[row][column] *= effective_decay;
        }
        effective_samples_ *= effective_decay;
        residual_weight_ *= effective_decay;
        residual_m2_ *= effective_decay;

        std::array<double, N> coefficients{};
        double prediction = 0.0;
        if (observations_ > 0 && solve_system(rhs_, coefficients, ridge_lambda))
        {
            prediction = dot(coefficients, features);
        }
        const double residual = target - prediction;

        const double old_weight = residual_weight_;
        const double new_weight = old_weight + weight;
        const double delta = residual - residual_mean_;
        residual_mean_ += weight / new_weight * delta;
        const double delta2 = residual - residual_mean_;
        residual_m2_ += weight * delta * delta2;
        residual_weight_ = new_weight;

        for (std::size_t row = 0; row < N; ++row)
        {
            rhs_[row] += weight * features[row] * target;
            for (std::size_t column = 0; column < N; ++column)
            {
                matrix_[row][column] += weight * features[row] * features[column];
            }
        }
        effective_samples_ += weight;
        ++observations_;
    }

    bool save(std::ostream& output, const std::string& label) const
    {
        output << label << ' ' << observations_ << ' ' << effective_samples_ << ' '
               << residual_mean_ << ' ' << residual_m2_ << ' ' << residual_weight_ << '\n';
        for (double value : rhs_)
            output << value << ' ';
        output << '\n';
        for (const auto& row : matrix_)
        {
            for (double value : row)
                output << value << ' ';
            output << '\n';
        }
        return output.good();
    }

    bool load(std::istream& input, const std::string& expected_label)
    {
        std::string label;
        input >> label >> observations_ >> effective_samples_ >> residual_mean_ >> residual_m2_
            >> residual_weight_;
        if (!input || label != expected_label)
            return false;
        for (double& value : rhs_)
            input >> value;
        for (auto& row : matrix_)
        {
            for (double& value : row)
                input >> value;
        }
        return static_cast<bool>(input);
    }

  private:
    static double dot(const std::array<double, N>& left, const std::array<double, N>& right)
    {
        double result = 0.0;
        for (std::size_t index = 0; index < N; ++index)
            result += left[index] * right[index];
        return result;
    }

    bool solve_system(const std::array<double, N>& right,
                      std::array<double, N>& solution,
                      double ridge_lambda) const
    {
        std::array<std::array<double, N + 1>, N> augmented{};
        for (std::size_t row = 0; row < N; ++row)
        {
            for (std::size_t column = 0; column < N; ++column)
            {
                augmented[row][column] = matrix_[row][column];
                if (row == column)
                    augmented[row][column] += std::max(1.0e-6, ridge_lambda);
            }
            augmented[row][N] = right[row];
        }

        for (std::size_t pivot = 0; pivot < N; ++pivot)
        {
            std::size_t best = pivot;
            for (std::size_t row = pivot + 1; row < N; ++row)
            {
                if (std::abs(augmented[row][pivot]) > std::abs(augmented[best][pivot]))
                {
                    best = row;
                }
            }
            if (std::abs(augmented[best][pivot]) < 1.0e-12)
                return false;
            if (best != pivot)
                std::swap(augmented[best], augmented[pivot]);

            const double divisor = augmented[pivot][pivot];
            for (std::size_t column = pivot; column <= N; ++column)
                augmented[pivot][column] /= divisor;

            for (std::size_t row = 0; row < N; ++row)
            {
                if (row == pivot)
                    continue;
                const double factor = augmented[row][pivot];
                if (std::abs(factor) < 1.0e-18)
                    continue;
                for (std::size_t column = pivot; column <= N; ++column)
                {
                    augmented[row][column] -= factor * augmented[pivot][column];
                }
            }
        }

        for (std::size_t row = 0; row < N; ++row)
            solution[row] = augmented[row][N];
        return true;
    }

    std::array<std::array<double, N>, N> matrix_{};
    std::array<double, N> rhs_{};
    std::size_t observations_ = 0;
    double effective_samples_ = 0.0;
    double residual_mean_ = 0.0;
    double residual_m2_ = 0.0;

    double residual_weight_ = 0.0;
};
} // namespace detail

class HierarchicalResidualLearner
{
  public:
    HierarchicalResidualResult evaluate(const PlanCostEstimate& estimate) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return evaluate_unlocked(estimate);
    }

    HierarchicalResidualResult apply(PlanCostEstimate& estimate) const
    {
        const HierarchicalResidualResult result = evaluate(estimate);
        estimate.hierarchical_residual_applied = result.applied;
        estimate.hierarchical_residual_base_ms = result.base_runtime_ms;
        estimate.hierarchical_residual_factor = result.factor;
        estimate.hierarchical_residual_log_value = result.log_residual;
        estimate.hierarchical_residual_confidence = result.confidence;
        estimate.hierarchical_residual_log_stddev = result.log_standard_deviation;
        estimate.predicted_runtime_stddev_ms = result.runtime_standard_deviation_ms;
        estimate.hierarchical_shared_contribution = result.shared_contribution;
        estimate.hierarchical_backend_contribution = result.backend_contribution;
        estimate.hierarchical_family_backend_contribution = result.family_backend_contribution;
        estimate.hierarchical_shared_confidence = result.shared_confidence;
        estimate.hierarchical_backend_confidence = result.backend_confidence;
        estimate.hierarchical_family_backend_confidence = result.family_backend_confidence;
        estimate.hierarchical_shared_samples = result.shared_samples;
        estimate.hierarchical_backend_samples = result.backend_samples;
        estimate.hierarchical_family_backend_samples = result.family_backend_samples;

        if (!result.applied)
            return result;

        // Preserve component ownership. Learned residuals correct the
        // observed non-framework total but do not rewrite execution,
        // scheduling, memory, or imbalance component estimates.
        estimate.predicted_total_ms = result.corrected_runtime_ms + estimate.framework_overhead_ms;
        return result;
    }

    void record(const PlanCostEstimate& estimate, double actual_runtime_ms)
    {
        if (!estimate.residual_features.available || !std::isfinite(actual_runtime_ms)
            || actual_runtime_ms <= 0.0)
        {
            return;
        }

        const double base =
            estimate.hierarchical_residual_base_ms > 0.0
                ? estimate.hierarchical_residual_base_ms
                : std::max(0.0,
                           estimate.analytical_baseline_total_ms - estimate.framework_overhead_ms);
        if (!std::isfinite(base) || base <= 0.0)
            return;

        const double target =
            std::clamp(std::log(actual_runtime_ms / base), std::log(0.25), std::log(4.0));
        const Config& config = global_config();
        const std::size_t backend = backend_index(estimate.plan);
        const auto memberships = estimate.residual_features.family_membership.expert_weights();
        const auto shared_features = shared_feature_values(estimate.residual_features.values);
        const auto expert_features = expert_feature_values(estimate.residual_features.values);

        std::lock_guard<std::mutex> lock(mutex_);

        const auto shared_before =
            shared_.predict(shared_features,
                            1,
                            config.hierarchical_shared_full_confidence_samples,
                            config.hierarchical_residual_ridge_lambda,
                            config.hierarchical_residual_variance_scale,
                            estimate.residual_features.feature_confidence);
        const double shared_prediction = shared_before.available ? shared_before.mean : 0.0;

        const auto backend_before =
            backends_[backend].predict(expert_features,
                                       1,
                                       config.hierarchical_backend_full_confidence_samples,
                                       config.hierarchical_residual_ridge_lambda,
                                       config.hierarchical_residual_variance_scale,
                                       estimate.residual_features.feature_confidence);
        const double backend_prediction = backend_before.available ? backend_before.mean : 0.0;

        shared_.update(shared_features,
                       target,
                       1.0,
                       config.hierarchical_residual_decay,
                       config.hierarchical_residual_ridge_lambda);
        backends_[backend].update(expert_features,
                                  target - shared_prediction,
                                  1.0,
                                  config.hierarchical_residual_decay,
                                  config.hierarchical_residual_ridge_lambda);

        const double remaining = target - shared_prediction - backend_prediction;
        for (std::size_t family = 0; family < family_count; ++family)
        {
            const double membership = std::clamp(memberships[family], 0.0, 1.0);
            if (membership < 0.05)
                continue;
            family_backends_[family][backend].update(expert_features,
                                                     remaining,
                                                     membership,
                                                     config.hierarchical_residual_decay,
                                                     config.hierarchical_residual_ridge_lambda);
        }
    }

    void clear()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        shared_.clear();
        for (auto& model : backends_)
            model.clear();
        for (auto& family : family_backends_)
        {
            for (auto& model : family)
                model.clear();
        }
    }

    bool save_to_file(const std::string& path) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::ofstream output(path, std::ios::trunc);
        if (!output)
            return false;
        output << "SMARTPARALLEL_HIERARCHICAL_RESIDUAL_V1\n";
        output << std::setprecision(17);
        if (!shared_.save(output, "shared"))
            return false;
        for (std::size_t backend = 0; backend < backend_count; ++backend)
        {
            if (!backends_[backend].save(output, "backend_" + std::to_string(backend)))
            {
                return false;
            }
        }
        for (std::size_t family = 0; family < family_count; ++family)
        {
            for (std::size_t backend = 0; backend < backend_count; ++backend)
            {
                if (!family_backends_[family][backend].save(
                        output,
                        "family_" + std::to_string(family) + "_backend_" + std::to_string(backend)))
                {
                    return false;
                }
            }
        }
        return output.good();
    }

    bool load_from_file(const std::string& path)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::ifstream input(path);
        if (!input)
            return false;
        std::string header;
        std::getline(input, header);
        if (header != "SMARTPARALLEL_HIERARCHICAL_RESIDUAL_V1")
            return false;
        if (!shared_.load(input, "shared"))
            return false;
        for (std::size_t backend = 0; backend < backend_count; ++backend)
        {
            if (!backends_[backend].load(input, "backend_" + std::to_string(backend)))
            {
                return false;
            }
        }
        for (std::size_t family = 0; family < family_count; ++family)
        {
            for (std::size_t backend = 0; backend < backend_count; ++backend)
            {
                if (!family_backends_[family][backend].load(
                        input,
                        "family_" + std::to_string(family) + "_backend_" + std::to_string(backend)))
                {
                    return false;
                }
            }
        }
        return true;
    }

  private:
    static constexpr std::size_t backend_count = 4;
    static constexpr std::size_t family_count = 5;
    using Model = detail::OnlineRidgeModel<residual_feature_count>;

    HierarchicalResidualResult evaluate_unlocked(const PlanCostEstimate& estimate) const
    {
        HierarchicalResidualResult result;
        result.base_runtime_ms =
            std::max(0.0, estimate.predicted_total_ms - estimate.framework_overhead_ms);
        result.corrected_runtime_ms = result.base_runtime_ms;
        if (!estimate.residual_features.available || result.base_runtime_ms <= 0.0)
        {
            return result;
        }

        const Config& config = global_config();
        const std::size_t backend = backend_index(estimate.plan);

        const double feature_confidence = estimate.residual_features.feature_confidence;
        const auto shared_features = shared_feature_values(estimate.residual_features.values);
        const auto expert_features = expert_feature_values(estimate.residual_features.values);

        const auto shared = shared_.predict(shared_features,
                                            config.hierarchical_shared_minimum_samples,
                                            config.hierarchical_shared_full_confidence_samples,
                                            config.hierarchical_residual_ridge_lambda,
                                            config.hierarchical_residual_variance_scale,
                                            feature_confidence);

        const auto backend_model =
            backends_[backend].predict(expert_features,
                                       config.hierarchical_backend_minimum_samples,
                                       config.hierarchical_backend_full_confidence_samples,
                                       config.hierarchical_residual_ridge_lambda,
                                       config.hierarchical_residual_variance_scale,
                                       feature_confidence);

        result.shared_samples = shared.samples;
        result.backend_samples = backend_model.samples;
        result.shared_confidence = shared.confidence;
        result.backend_confidence = backend_model.confidence;
        result.shared_contribution = shared.mean * shared.confidence;
        result.backend_contribution = backend_model.mean * backend_model.confidence;

        const auto memberships = estimate.residual_features.family_membership.expert_weights();
        double family_contribution = 0.0;
        double family_confidence_mass = 0.0;
        double family_variance = 0.0;
        std::size_t family_samples = 0;
        for (std::size_t family = 0; family < family_count; ++family)
        {
            const double membership = std::clamp(memberships[family], 0.0, 1.0);
            if (membership <= 0.0)
                continue;
            const auto expert = family_backends_[family][backend].predict(
                expert_features,
                config.hierarchical_family_backend_minimum_samples,
                config.hierarchical_family_backend_full_confidence_samples,
                config.hierarchical_residual_ridge_lambda,
                config.hierarchical_residual_variance_scale,
                feature_confidence * membership);
            family_contribution += membership * expert.mean * expert.confidence;
            family_confidence_mass += membership * expert.confidence;
            family_variance +=
                membership * membership * expert.standard_deviation * expert.standard_deviation;
            family_samples = std::max(family_samples, expert.samples);
        }
        result.family_backend_samples = family_samples;
        result.family_backend_confidence = std::clamp(family_confidence_mass, 0.0, 1.0);
        result.family_backend_contribution = family_contribution;

        const double combined_log_residual = result.shared_contribution
                                             + result.backend_contribution
                                             + result.family_backend_contribution;
        result.confidence =
            std::clamp(1.0
                           - (1.0 - result.shared_confidence) * (1.0 - result.backend_confidence)
                                 * (1.0 - result.family_backend_confidence),
                       0.0,
                       1.0);

        const double weak_lower = std::log(std::max(0.01, config.hierarchical_weak_minimum_factor));
        const double weak_upper = std::log(std::max(1.0, config.hierarchical_weak_maximum_factor));
        const double mature_lower =
            std::log(std::max(0.01, config.hierarchical_mature_minimum_factor));
        const double mature_upper =
            std::log(std::max(1.0, config.hierarchical_mature_maximum_factor));
        const double lower = weak_lower + (mature_lower - weak_lower) * result.confidence;
        const double upper = weak_upper + (mature_upper - weak_upper) * result.confidence;
        result.log_residual = std::clamp(combined_log_residual, lower, upper);
        result.factor = std::exp(result.log_residual);
        result.corrected_runtime_ms = result.base_runtime_ms * result.factor;

        const double shared_variance = shared.standard_deviation * shared.standard_deviation
                                       * shared.confidence * shared.confidence;
        const double backend_variance = backend_model.standard_deviation
                                        * backend_model.standard_deviation
                                        * backend_model.confidence * backend_model.confidence;
        const double model_variance = shared_variance + backend_variance + family_variance;
        const double unsupported_variance =
            0.09 * (1.0 - result.confidence) * (1.0 - result.confidence);
        result.log_standard_deviation =
            std::sqrt(std::max(1.0e-4, model_variance + unsupported_variance));
        result.runtime_standard_deviation_ms =
            result.corrected_runtime_ms
            * std::sqrt(std::exp(result.log_standard_deviation * result.log_standard_deviation)
                        - 1.0);
        result.applied = result.confidence > 0.0 && std::abs(result.factor - 1.0) > 1.0e-6;
        return result;
    }

    static std::array<double, residual_feature_count>
    shared_feature_values(const std::array<double, residual_feature_count>& source)
    {
        auto features = source;
        // Backend identity belongs to the backend layer. Keeping those
        // one-hot values in the shared model would let both layers learn
        // the same systematic offset and make ownership unidentifiable.
        features[17] = 0.0;
        features[18] = 0.0;
        features[19] = 0.0;
        return features;
    }

    static std::array<double, residual_feature_count>
    expert_feature_values(const std::array<double, residual_feature_count>& source)
    {
        auto features = source;
        // Each expert is already keyed by backend, so backend one-hot
        // columns are redundant with its intercept.
        features[17] = 0.0;
        features[18] = 0.0;
        features[19] = 0.0;
        return features;
    }

    static std::size_t backend_index(const ExecutionPlan& plan)
    {
        if (!plan.parallel || plan.strategy == ExecutionStrategy::Sequential)
            return 0;
        switch (plan.engine)
        {
            case ExecutionEngineType::ThreadPool:
                return 1;
            case ExecutionEngineType::StaticThread:
                return 2;
            case ExecutionEngineType::OneTbb:
                return 3;
            case ExecutionEngineType::Auto:
            default:
                return 1;
        }
    }

    mutable std::mutex mutex_;
    Model shared_{};
    std::array<Model, backend_count> backends_{};
    std::array<std::array<Model, backend_count>, family_count> family_backends_{};
};

inline HierarchicalResidualLearner& global_hierarchical_residual_learner()
{
    static HierarchicalResidualLearner learner;
    return learner;
}
} // namespace smart
