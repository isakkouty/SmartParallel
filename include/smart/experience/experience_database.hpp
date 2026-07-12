#pragma once

#include <cstddef>
#include <fstream>
#include <cmath>
#include <limits>
#include <string>
#include <unordered_map>

#include <smart/experience/experience_entry.hpp>
#include <smart/experience/experience_plan_key.hpp>
#include <smart/experience/experience_record.hpp>
#include <smart/workload/fingerprint.hpp>

namespace smart
{
    class ExperienceDatabase
    {
    public:
        void record(
            const WorkloadFingerprint& fingerprint,
            const ExecutionPlan& plan,
            double elapsed_ms)
        {
            ExperienceRecord& record =
                records_[fingerprint.value];

            record.fingerprint = fingerprint;
            record.valid = true;

            ExperiencePlanKey key;
            key.engine = plan.engine;
            key.strategy = plan.strategy;
            key.job_count = plan.job_count;

            ExperienceEntry& entry =
                record.plans[key];

            entry.fingerprint = fingerprint;
            entry.engine = plan.engine;
            entry.strategy = plan.strategy;
            entry.job_count = plan.job_count;

            ++entry.sample_count;
            entry.last_elapsed_ms = elapsed_ms;

            if (!entry.valid ||
                elapsed_ms < entry.best_elapsed_ms)
            {
                entry.best_elapsed_ms = elapsed_ms;
            }

            if (entry.sample_count == 1)
            {
                entry.average_elapsed_ms = elapsed_ms;
            }
            else
            {
                const double previous_weight =
                    static_cast<double>(entry.sample_count - 1);

                entry.average_elapsed_ms =
                    ((entry.average_elapsed_ms * previous_weight) +
                    elapsed_ms) /
                    static_cast<double>(entry.sample_count);

                double delta =
                    elapsed_ms - entry.average_elapsed_ms;

                entry.variance_ms =
                    ((entry.variance_ms * previous_weight) +
                    (delta * delta)) /
                    static_cast<double>(entry.sample_count);

                entry.standard_deviation_ms =
                    std::sqrt(entry.variance_ms);
            }

            double sample_confidence =
                entry.sample_count >= 30
                    ? 1.0
                    : static_cast<double>(entry.sample_count) / 30.0;

            double stability_confidence = 1.0;

            if (entry.average_elapsed_ms > 0.0)
            {
                double relative_deviation =
                    entry.standard_deviation_ms / entry.average_elapsed_ms;

                stability_confidence =
                    relative_deviation >= 1.0
                        ? 0.0
                        : 1.0 - relative_deviation;
            }

            entry.confidence =
                sample_confidence * stability_confidence;

            entry.valid = true;
        }

        const ExperienceRecord* find_record(
            const WorkloadFingerprint& fingerprint) const
        {
            auto it = records_.find(fingerprint.value);

            if (it == records_.end())
                return nullptr;

            if (!it->second.valid)
                return nullptr;

            return &it->second;
        }

        const ExperienceEntry* best_entry(
            const WorkloadFingerprint& fingerprint) const
        {
            const ExperienceRecord* record =
                find_record(fingerprint);

            if (!record)
                return nullptr;

            const ExperienceEntry* best = nullptr;
            double best_average =
                std::numeric_limits<double>::max();

            for (const auto& pair : record->plans)
            {
                const ExperienceEntry& entry = pair.second;

                if (!entry.valid)
                    continue;

                if (entry.average_elapsed_ms < best_average)
                {
                    best_average = entry.average_elapsed_ms;
                    best = &entry;
                }
            }

            return best;
        }

        const ExperienceEntry* find(
            const WorkloadFingerprint& fingerprint) const
        {
            return best_entry(fingerprint);
        }

        std::size_t size() const
        {
            return records_.size();
        }

        void save_to_file(const std::string& path) const
        {
            std::ofstream file(path);

            if (!file)
                return;

            for (const auto& record_pair : records_)
            {
                const ExperienceRecord& record =
                    record_pair.second;

                if (!record.valid)
                    continue;

                for (const auto& plan_pair : record.plans)
                {
                    const ExperienceEntry& entry =
                        plan_pair.second;

                    if (!entry.valid)
                        continue;

                    file << entry.fingerprint.value << " "
                         << static_cast<int>(entry.engine) << " "
                         << static_cast<int>(entry.strategy) << " "
                         << entry.job_count << " "
                         << entry.best_elapsed_ms << " "
                         << entry.average_elapsed_ms << " "
                         << entry.last_elapsed_ms << " "
                         << entry.variance_ms << " "
                         << entry.standard_deviation_ms << " "
                         << entry.sample_count << " "
                         << entry.confidence << "\n";
                }
            }
        }

        void load_from_file(const std::string& path)
        {
            std::ifstream file(path);

            if (!file)
                return;

            records_.clear();

            std::size_t fingerprint_value = 0;
            int engine_value = 0;
            int strategy_value = 0;

            while (file)
            {
                ExperienceEntry entry;

                file >> fingerprint_value
                     >> engine_value
                     >> strategy_value
                     >> entry.job_count
                     >> entry.best_elapsed_ms
                     >> entry.average_elapsed_ms
                     >> entry.last_elapsed_ms
                     >> entry.variance_ms
                     >> entry.standard_deviation_ms
                     >> entry.sample_count
                     >> entry.confidence;

                if (!file)
                    break;

                entry.fingerprint.value = fingerprint_value;
                entry.engine =
                    static_cast<ExecutionEngineType>(engine_value);
                entry.strategy =
                    static_cast<ExecutionStrategy>(strategy_value);
                entry.valid = true;

                ExperienceRecord& record =
                    records_[fingerprint_value];

                record.fingerprint = entry.fingerprint;
                record.valid = true;

                ExperiencePlanKey key;
                key.engine = entry.engine;
                key.strategy = entry.strategy;
                key.job_count = entry.job_count;

                record.plans[key] = entry;
            }
        }

    private:
        std::unordered_map<std::size_t, ExperienceRecord> records_;
    };

    inline ExperienceDatabase& global_experience_database()
    {
        static ExperienceDatabase database;
        return database;
    }
}
