#pragma once

#include <cmath>
#include <cstddef>
#include <fstream>
#include <iomanip>
#include <smart/ranking/utility_model.hpp>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace smart::ranking
{
struct UtilityModelArtifact
{
    static constexpr int current_format_version = 1;

    int format_version = current_format_version;
    std::string feature_schema = "phase1_utility_v1";
    std::string promotion_status = "SHADOW_ONLY";
    std::string hardware_fingerprint = "unspecified";
    std::vector<double> scaler_means;

    std::vector<double> scaler_scales;
    LinearUtilityModel model;

    bool promoted() const noexcept
    {
        return promotion_status == "PROMOTED";
    }

    void validate() const
    {
        if (format_version != current_format_version)
            throw std::runtime_error("unsupported SmartParallel utility-model format version");
        if (feature_schema.empty())
            throw std::runtime_error("utility-model feature schema is empty");
        if (model.weights().empty())
            throw std::runtime_error("utility model contains no weights");
        if (scaler_means.size() + 1 != model.weights().size()
            || scaler_scales.size() != scaler_means.size())
            throw std::runtime_error("utility-model scaler dimensions do not match weights");
        for (double value : scaler_means)
            if (!std::isfinite(value))
                throw std::runtime_error("non-finite scaler mean");
        for (double value : scaler_scales)
            if (!std::isfinite(value) || value <= 0.0)
                throw std::runtime_error("invalid scaler scale");
        for (double value : model.weights())
            if (!std::isfinite(value))
                throw std::runtime_error("non-finite utility-model weight");
    }

    std::vector<double> transform(const std::vector<double>& raw_features) const
    {
        validate();
        if (raw_features.size() != scaler_means.size())
            throw std::invalid_argument("raw feature count does not match utility-model scaler");
        std::vector<double> result;
        result.reserve(raw_features.size() + 1);
        result.push_back(1.0);
        for (std::size_t i = 0; i < raw_features.size(); ++i)
        {
            const double normalized = (raw_features[i] - scaler_means[i]) / scaler_scales[i];
            result.push_back(std::max(-4.0, std::min(4.0, normalized)));
        }
        return result;
    }
};

namespace detail
{
inline void write_vector(std::ostream& output, const char* name, const std::vector<double>& values)
{
    output << name << ' ' << values.size();
    output << std::setprecision(17);
    for (double value : values)
        output << ' ' << value;
    output << '\n';
}

inline std::vector<double> read_vector(std::istringstream& input, const std::string& field)
{
    std::size_t count = 0;
    if (!(input >> count))
        throw std::runtime_error("missing count for " + field);
    std::vector<double> values(count);
    for (double& value : values)
        if (!(input >> value))
            throw std::runtime_error("invalid value in " + field);
    return values;
}
} // namespace detail

inline void save_utility_model_artifact(const UtilityModelArtifact& artifact,
                                        const std::string& path)
{
    artifact.validate();
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output)
        throw std::runtime_error("cannot open utility-model artifact for writing: " + path);
    output << "SMARTPARALLEL_UTILITY_MODEL " << artifact.format_version << '\n';
    output << "feature_schema " << artifact.feature_schema << '\n';
    output << "promotion_status " << artifact.promotion_status << '\n';
    output << "hardware_fingerprint " << artifact.hardware_fingerprint << '\n';
    detail::write_vector(output, "scaler_means", artifact.scaler_means);
    detail::write_vector(output, "scaler_scales", artifact.scaler_scales);
    detail::write_vector(output, "weights", artifact.model.weights());
    output << "END\n";
    if (!output)
        throw std::runtime_error("failed while writing utility-model artifact: " + path);
}

inline UtilityModelArtifact load_utility_model_artifact(const std::string& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::runtime_error("cannot open utility-model artifact: " + path);

    UtilityModelArtifact artifact;
    std::string line;
    bool header_seen = false;
    bool end_seen = false;
    std::vector<double> weights;
    while (std::getline(input, line))
    {
        if (line.empty())
            continue;
        std::istringstream row(line);
        std::string key;
        row >> key;
        if (!header_seen)
        {
            if (key != "SMARTPARALLEL_UTILITY_MODEL" || !(row >> artifact.format_version))
                throw std::runtime_error("invalid SmartParallel utility-model header");
            header_seen = true;
            continue;
        }
        if (key == "feature_schema")
            row >> artifact.feature_schema;
        else if (key == "promotion_status")
            row >> artifact.promotion_status;
        else if (key == "hardware_fingerprint")
            row >> artifact.hardware_fingerprint;
        else if (key == "scaler_means")
            artifact.scaler_means = detail::read_vector(row, key);
        else if (key == "scaler_scales")
            artifact.scaler_scales = detail::read_vector(row, key);
        else if (key == "weights")
            weights = detail::read_vector(row, key);
        else if (key == "END")
        {
            end_seen = true;
            break;
        }
        else
            throw std::runtime_error("unknown utility-model field: " + key);
    }
    if (!header_seen || !end_seen)
        throw std::runtime_error("incomplete SmartParallel utility-model artifact");
    artifact.model = LinearUtilityModel(weights.size());
    artifact.model.weights() = std::move(weights);
    artifact.validate();
    return artifact;
}
} // namespace smart::ranking
