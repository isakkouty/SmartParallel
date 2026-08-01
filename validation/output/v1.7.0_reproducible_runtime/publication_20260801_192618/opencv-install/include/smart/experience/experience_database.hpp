#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <limits>
#include <mutex>
#include <optional>
#include <smart/core/config.hpp>
#include <smart/experience/experience_entry.hpp>
#include <smart/experience/experience_plan_key.hpp>
#include <smart/experience/experience_record.hpp>
#include <smart/workload/fingerprint.hpp>
#include <smart/workload/fingerprint_similarity.hpp>
#include <sstream>
#include <string>
#include <unordered_map>

namespace smart
{
struct SimilarExperienceSummary
{
    bool available = false;
    double elapsed_ms = 0.0;
    double regret_percent = 0.0;
    double success_rate = 0.5;
    double confidence = 0.0;
    double similarity = 0.0;

    double effective_weight = 0.0;
    std::size_t contributing_entries = 0;
};

class ExperienceDatabase
{
  public:
    void record(const WorkloadFingerprint& fingerprint,
                const ExecutionPlan& plan,
                double elapsed_ms,
                double predicted_ms = 0.0)
    {
        if (!std::isfinite(elapsed_ms) || elapsed_ms < 0.0)
            return;
        std::lock_guard<std::mutex> lock(mutex_);
        record_unlocked(fingerprint, plan, elapsed_ms, predicted_ms);
    }

    void record_outcome(const WorkloadFingerprint& fingerprint,
                        const ExecutionPlan& plan,
                        double elapsed_ms,
                        double best_elapsed_ms,
                        double predicted_ms = 0.0)
    {
        if (!std::isfinite(elapsed_ms) || elapsed_ms < 0.0)
            return;

        std::lock_guard<std::mutex> lock(mutex_);
        ExperienceEntry* entry = record_unlocked(fingerprint, plan, elapsed_ms, predicted_ms);
        if (entry == nullptr || !entry->valid || !std::isfinite(best_elapsed_ms)
            || best_elapsed_ms <= 0.0)
        {
            return;
        }

        const double regret =
            std::max(0.0, (elapsed_ms - best_elapsed_ms) / best_elapsed_ms * 100.0);
        const double success = regret <= effective_config().ranking_success_regret_percent ? 1.0 : 0.0;
        const double decay = std::clamp(effective_config().ranking_history_decay, 0.0, 1.0);

        const double old_weight = entry->effective_sample_weight * decay;
        const double new_weight = old_weight + 1.0;
        const auto update_ewma = [old_weight, new_weight](double old_value, double observation)
        {
            return new_weight > 0.0 ? (old_value * old_weight + observation) / new_weight
                                    : observation;
        };

        entry->decayed_elapsed_ms = entry->outcome_sample_count == 0
                                        ? elapsed_ms
                                        : update_ewma(entry->decayed_elapsed_ms, elapsed_ms);
        entry->decayed_regret_percent = entry->outcome_sample_count == 0
                                            ? regret
                                            : update_ewma(entry->decayed_regret_percent, regret);
        entry->decayed_success_rate = entry->outcome_sample_count == 0
                                          ? success
                                          : update_ewma(entry->decayed_success_rate, success);
        entry->effective_sample_weight = new_weight;
        entry->last_regret_percent = regret;
        saturating_increment(entry->outcome_sample_count);

        const double evidence = std::clamp(new_weight / 20.0, 0.0, 1.0);
        const double relative_deviation = entry->average_elapsed_ms > 0.0
            ? entry->standard_deviation_ms / entry->average_elapsed_ms
            : 1.0;
        const double stability = std::clamp(1.0 - relative_deviation, 0.0, 1.0);
        const double outcome_quality =
            std::clamp(0.25 + 0.75 * entry->decayed_success_rate, 0.0, 1.0);
        entry->confidence = evidence * stability * outcome_quality;
        touch(*entry);
        saturating_increment(dirty_records_);
    }

    SimilarExperienceSummary similar_plan_summary(const WorkloadFingerprint& fingerprint,
                                                  const ExecutionPlan& plan) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        SimilarExperienceSummary result;

        double weighted_elapsed = 0.0;
        double weighted_regret = 0.0;
        double weighted_success = 0.0;
        double weighted_confidence = 0.0;
        double similarity_sum = 0.0;
        const ExperiencePlanKey key = plan_key(plan);

        for (const auto& record_pair : records_)
        {
            const ExperienceRecord& record = record_pair.second;
            if (!record.valid || record.fingerprint.value == fingerprint.value)
                continue;

            const auto plan_it = record.plans.find(key);
            if (plan_it == record.plans.end() || !plan_it->second.valid)
                continue;

            const ExperienceEntry& entry = plan_it->second;
            if (entry.outcome_sample_count == 0)
                continue;

            const double similarity = fingerprint_similarity(fingerprint, record.fingerprint);
            if (similarity < effective_config().minimum_similarity)
                continue;

            const double weight = similarity * similarity * std::max(0.01, entry.confidence)
                                  * std::max(1.0, entry.effective_sample_weight);
            weighted_elapsed += entry.decayed_elapsed_ms * weight;
            weighted_regret += entry.decayed_regret_percent * weight;
            weighted_success += entry.decayed_success_rate * weight;
            weighted_confidence += entry.confidence * weight;
            similarity_sum += similarity * weight;
            result.effective_weight += weight;
            ++result.contributing_entries;
        }

        if (result.effective_weight > 0.0)
        {
            result.available = true;
            result.elapsed_ms = weighted_elapsed / result.effective_weight;
            result.regret_percent = weighted_regret / result.effective_weight;
            result.success_rate = weighted_success / result.effective_weight;
            result.confidence = weighted_confidence / result.effective_weight;
            result.similarity = similarity_sum / result.effective_weight;
        }
        return result;
    }

    std::optional<ExperienceRecord> find_record_copy(
        const WorkloadFingerprint& fingerprint) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = records_.find(fingerprint.value);
        if (it == records_.end() || !it->second.valid)
            return std::nullopt;
        touch(it->second);
        return it->second;
    }

    std::optional<ExperienceEntry> find_plan_copy(const WorkloadFingerprint& fingerprint,
                                                  const ExecutionPlan& plan) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto record_it = records_.find(fingerprint.value);
        if (record_it == records_.end() || !record_it->second.valid)
            return std::nullopt;
        auto plan_it = record_it->second.plans.find(plan_key(plan));
        if (plan_it == record_it->second.plans.end() || !plan_it->second.valid)
            return std::nullopt;
        touch(record_it->second);
        touch(plan_it->second);
        return plan_it->second;
    }

    std::optional<ExperienceEntry> best_entry_copy(
        const WorkloadFingerprint& fingerprint) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto record_it = records_.find(fingerprint.value);
        if (record_it == records_.end() || !record_it->second.valid)
            return std::nullopt;

        ExperienceEntry* best = nullptr;
        double best_average = std::numeric_limits<double>::max();
        for (auto& pair : record_it->second.plans)
        {
            ExperienceEntry& entry = pair.second;
            if (entry.valid && entry.average_elapsed_ms < best_average)
            {
                best_average = entry.average_elapsed_ms;
                best = &entry;
            }
        }
        if (best == nullptr)
            return std::nullopt;
        touch(record_it->second);
        touch(*best);
        return *best;
    }

    // Compatibility accessors return a thread-local copy. The pointer remains
    // valid until the next ExperienceDatabase pointer query on the same thread.
    const ExperienceRecord* find_record(const WorkloadFingerprint& fingerprint) const
    {
        thread_local std::optional<ExperienceRecord> copy;
        copy = find_record_copy(fingerprint);
        return copy ? &*copy : nullptr;
    }

    const ExperienceEntry* find_plan(const WorkloadFingerprint& fingerprint,
                                     const ExecutionPlan& plan) const
    {
        thread_local std::optional<ExperienceEntry> copy;
        copy = find_plan_copy(fingerprint, plan);
        return copy ? &*copy : nullptr;
    }

    const ExperienceEntry* best_entry(const WorkloadFingerprint& fingerprint) const
    {
        thread_local std::optional<ExperienceEntry> copy;
        copy = best_entry_copy(fingerprint);
        return copy ? &*copy : nullptr;
    }

    const ExperienceEntry* find(const WorkloadFingerprint& fingerprint) const
    {
        return best_entry(fingerprint);
    }

    std::size_t size() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return records_.size();
    }

    std::size_t plan_count() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::size_t count = 0;
        for (const auto& item : records_)
            count += item.second.plans.size();
        return count;
    }

    std::size_t maximum_plans_in_record() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::size_t maximum = 0;
        for (const auto& item : records_)
            maximum = std::max(maximum, item.second.plans.size());
        return maximum;
    }

    std::size_t dirty_records() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return dirty_records_;
    }

    bool loaded() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return loaded_;
    }

    const std::string& loaded_path() const
    {
        thread_local std::string copy;
        std::lock_guard<std::mutex> lock(mutex_);
        copy = loaded_path_;
        return copy;
    }

    void clear()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        records_.clear();
        dirty_records_ = 0;
        loaded_ = false;
        loaded_path_.clear();
        access_epoch_ = 0;
    }

    bool save_to_file(const std::string& path) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return save_to_file_unlocked(path);
    }

    bool load_from_file(const std::string& path)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return load_from_file_unlocked(path);
    }

    bool load_once(const std::string& path)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (loaded_ && loaded_path_ == path)
            return true;

        loaded_ = true;
        loaded_path_ = path;

        std::ifstream probe(path);
        if (!probe)
        {
            dirty_records_ = 0;
            return true;
        }
        probe.close();
        return load_from_file_unlocked(path);
    }

    bool save_if_due(const std::string& path, std::size_t interval) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const std::size_t effective_interval = std::max<std::size_t>(1, interval);
        if (dirty_records_ < effective_interval)
            return true;
        return save_to_file_unlocked(path);
    }

  private:
    static ExperiencePlanKey plan_key(const ExecutionPlan& plan)
    {
        ExperiencePlanKey key;
        key.engine = plan.engine;
        key.strategy = plan.strategy;
        key.job_count = plan.job_count;
        key.chunk_size = plan.chunk_size;
        return key;
    }

    static void saturating_increment(std::size_t& value) noexcept
    {
        if (value != std::numeric_limits<std::size_t>::max())
            ++value;
    }

    std::size_t record_limit() const noexcept
    {
        return runtime_limits::bounded_limit(effective_config().experience_cache_max_records,
                                             runtime_limits::experience_records);
    }

    std::size_t plan_limit() const noexcept
    {
        return runtime_limits::bounded_limit(
            effective_config().experience_cache_max_plans_per_record,
            runtime_limits::experience_plans_per_record);
    }

    void advance_access_epoch() const noexcept
    {
        if (access_epoch_ == std::numeric_limits<std::uint64_t>::max())
        {
            std::uint64_t next = 1;
            for (auto& item : records_)
            {
                item.second.last_access_epoch = next++;
                for (auto& plan : item.second.plans)
                    plan.second.last_access_epoch = next++;
            }
            access_epoch_ = next;
        }
        else
        {
            ++access_epoch_;
        }
    }

    void touch(ExperienceRecord& record) const noexcept
    {
        advance_access_epoch();
        record.last_access_epoch = access_epoch_;
    }

    void touch(ExperienceEntry& entry) const noexcept
    {
        advance_access_epoch();
        entry.last_access_epoch = access_epoch_;
    }

    void evict_record_if_needed(std::size_t incoming_fingerprint)
    {
        if (records_.find(incoming_fingerprint) != records_.end())
            return;
        const std::size_t maximum = record_limit();
        while (records_.size() >= maximum)
        {
            auto victim = records_.end();
            for (auto it = records_.begin(); it != records_.end(); ++it)
            {
                if (victim == records_.end()
                    || it->second.last_access_epoch < victim->second.last_access_epoch)
                    victim = it;
            }
            if (victim == records_.end())
                return;
            records_.erase(victim);
        }
    }

    void evict_plan_if_needed(ExperienceRecord& record, const ExperiencePlanKey& incoming_key)
    {
        if (record.plans.find(incoming_key) != record.plans.end())
            return;
        const std::size_t maximum = plan_limit();
        while (record.plans.size() >= maximum)
        {
            auto victim = record.plans.end();
            for (auto it = record.plans.begin(); it != record.plans.end(); ++it)
            {
                if (victim == record.plans.end()
                    || it->second.last_access_epoch < victim->second.last_access_epoch)
                    victim = it;
            }
            if (victim == record.plans.end())
                return;
            record.plans.erase(victim);
        }
    }

    ExperienceEntry* record_unlocked(const WorkloadFingerprint& fingerprint,
                                     const ExecutionPlan& plan,
                                     double elapsed_ms,
                                     double predicted_ms)
    {
        evict_record_if_needed(fingerprint.value);
        ExperienceRecord& record = records_[fingerprint.value];
        record.fingerprint = fingerprint;
        record.valid = true;
        touch(record);

        const ExperiencePlanKey key = plan_key(plan);
        evict_plan_if_needed(record, key);
        ExperienceEntry& entry = record.plans[key];
        entry.fingerprint = fingerprint;
        entry.engine = plan.engine;
        entry.strategy = plan.strategy;
        entry.job_count = plan.job_count;
        entry.chunk_size = plan.chunk_size;
        touch(entry);

        const std::size_t previous_samples = entry.sample_count;
        saturating_increment(entry.sample_count);
        entry.last_elapsed_ms = elapsed_ms;

        if (!entry.valid || previous_samples == 0 || elapsed_ms < entry.best_elapsed_ms)
            entry.best_elapsed_ms = elapsed_ms;

        if (previous_samples == 0)
        {
            entry.average_elapsed_ms = elapsed_ms;
            entry.variance_ms = 0.0;
        }
        else
        {
            const double old_mean = entry.average_elapsed_ms;
            const double delta = elapsed_ms - old_mean;
            const double new_mean = old_mean + delta / static_cast<double>(entry.sample_count);
            const double delta2 = elapsed_ms - new_mean;
            const double previous_m2 = entry.variance_ms * static_cast<double>(previous_samples);
            const double new_m2 = previous_m2 + delta * delta2;
            entry.average_elapsed_ms = new_mean;
            entry.variance_ms = new_m2 / static_cast<double>(entry.sample_count);
        }

        entry.standard_deviation_ms = std::sqrt(std::max(0.0, entry.variance_ms));

        if (std::isfinite(predicted_ms) && predicted_ms > 0.0)
        {
            entry.last_predicted_ms = predicted_ms;
            const double signed_error_percent = ((elapsed_ms - predicted_ms) / predicted_ms) * 100.0;
            const double absolute_error_percent = std::abs(signed_error_percent);
            const double correction = elapsed_ms / predicted_ms;
            entry.last_prediction_error_percent = signed_error_percent;

            const std::size_t previous_prediction_samples = entry.prediction_sample_count;
            saturating_increment(entry.prediction_sample_count);
            entry.average_absolute_prediction_error_percent =
                (entry.average_absolute_prediction_error_percent
                     * static_cast<double>(previous_prediction_samples)
                 + absolute_error_percent)
                / static_cast<double>(entry.prediction_sample_count);
            entry.average_runtime_correction =
                (entry.average_runtime_correction * static_cast<double>(previous_prediction_samples)
                 + correction)
                / static_cast<double>(entry.prediction_sample_count);
            entry.average_runtime_correction =
                std::clamp(entry.average_runtime_correction, 0.25, 4.0);
        }

        const double sample_confidence = entry.sample_count >= 30
            ? 1.0
            : static_cast<double>(entry.sample_count) / 30.0;
        double stability_confidence = 1.0;
        if (entry.average_elapsed_ms > 0.0)
        {
            const double relative_deviation =
                entry.standard_deviation_ms / entry.average_elapsed_ms;
            stability_confidence = std::clamp(1.0 - relative_deviation, 0.0, 1.0);
        }
        entry.confidence = sample_confidence * stability_confidence;
        entry.valid = true;
        saturating_increment(dirty_records_);
        return &entry;
    }

    bool save_to_file_unlocked(const std::string& path) const
    {
        std::ofstream file(path, std::ios::trunc);
        if (!file)
            return false;

        file << "SMARTPARALLEL_EXPERIENCE_V4\n";
        file << std::setprecision(17);
        for (const auto& record_pair : records_)
        {
            const ExperienceRecord& record = record_pair.second;
            if (!record.valid)
                continue;
            for (const auto& plan_pair : record.plans)
            {
                const ExperienceEntry& entry = plan_pair.second;
                if (!entry.valid)
                    continue;
                file << entry.fingerprint.value << " " << static_cast<int>(entry.engine) << " "
                     << static_cast<int>(entry.strategy) << " " << entry.job_count << " "
                     << entry.chunk_size << " " << entry.best_elapsed_ms << " "
                     << entry.average_elapsed_ms << " " << entry.last_elapsed_ms << " "
                     << entry.variance_ms << " " << entry.standard_deviation_ms << " "
                     << entry.sample_count << " " << entry.confidence << " "
                     << entry.last_predicted_ms << " " << entry.last_prediction_error_percent << " "
                     << entry.average_absolute_prediction_error_percent << " "
                     << entry.average_runtime_correction << " " << entry.prediction_sample_count
                     << " " << entry.fingerprint.kind_bucket << " "
                     << entry.fingerprint.iteration_bucket << " "
                     << entry.fingerprint.working_set_bucket << " "
                     << entry.fingerprint.object_size_bucket << " "
                     << entry.fingerprint.function_cost_bucket << " "
                     << entry.fingerprint.variation_bucket << " " << entry.effective_sample_weight
                     << " " << entry.decayed_elapsed_ms << " " << entry.decayed_regret_percent
                     << " " << entry.decayed_success_rate << " " << entry.last_regret_percent << " "
                     << entry.outcome_sample_count << "\n";
            }
        }

        if (!file.good())
            return false;
        dirty_records_ = 0;
        return true;
    }

    bool load_from_file_unlocked(const std::string& path)
    {
        std::ifstream file(path);
        if (!file)
            return false;

        std::string first_line;
        if (!std::getline(file, first_line))
            return false;

        records_.clear();
        access_epoch_ = 0;
        const bool version4 = first_line == "SMARTPARALLEL_EXPERIENCE_V4";
        const bool version3 = first_line == "SMARTPARALLEL_EXPERIENCE_V3";
        const bool version2 = first_line == "SMARTPARALLEL_EXPERIENCE_V2";
        if (!version2 && !version3 && !version4)
        {
            file.clear();
            file.seekg(0);
        }

        std::string line;
        while (std::getline(file, line))
        {
            if (line.empty())
                continue;

            std::istringstream input(line);
            ExperienceEntry entry;
            std::size_t fingerprint_value = 0;
            int engine_value = 0;
            int strategy_value = 0;
            input >> fingerprint_value >> engine_value >> strategy_value >> entry.job_count;
            if (version3 || version4)
                input >> entry.chunk_size;
            input >> entry.best_elapsed_ms >> entry.average_elapsed_ms >> entry.last_elapsed_ms
                >> entry.variance_ms >> entry.standard_deviation_ms >> entry.sample_count
                >> entry.confidence;
            if (!input)
                continue;

            if (version2 || version3 || version4)
            {
                input >> entry.last_predicted_ms >> entry.last_prediction_error_percent
                    >> entry.average_absolute_prediction_error_percent
                    >> entry.average_runtime_correction >> entry.prediction_sample_count;
                if (!input)
                    continue;
            }
            if (version4)
            {
                input >> entry.fingerprint.kind_bucket >> entry.fingerprint.iteration_bucket
                    >> entry.fingerprint.working_set_bucket >> entry.fingerprint.object_size_bucket
                    >> entry.fingerprint.function_cost_bucket >> entry.fingerprint.variation_bucket
                    >> entry.effective_sample_weight >> entry.decayed_elapsed_ms
                    >> entry.decayed_regret_percent >> entry.decayed_success_rate
                    >> entry.last_regret_percent >> entry.outcome_sample_count;
                if (!input)
                    continue;
            }

            entry.fingerprint.value = fingerprint_value;
            entry.engine = static_cast<ExecutionEngineType>(engine_value);
            entry.strategy = static_cast<ExecutionStrategy>(strategy_value);
            entry.valid = true;

            evict_record_if_needed(fingerprint_value);
            ExperienceRecord& record = records_[fingerprint_value];
            record.fingerprint = entry.fingerprint;
            record.valid = true;
            touch(record);
            ExperiencePlanKey key;
            key.engine = entry.engine;
            key.strategy = entry.strategy;
            key.job_count = entry.job_count;
            key.chunk_size = entry.chunk_size;
            evict_plan_if_needed(record, key);
            touch(entry);
            record.plans[key] = entry;
        }

        loaded_ = true;
        loaded_path_ = path;
        dirty_records_ = 0;
        return true;
    }

    mutable std::mutex mutex_;
    mutable std::unordered_map<std::size_t, ExperienceRecord> records_;
    mutable std::size_t dirty_records_ = 0;
    mutable std::uint64_t access_epoch_ = 0;
    bool loaded_ = false;
    std::string loaded_path_;
};

namespace detail
{
ExperienceDatabase* active_runtime_experience_database() noexcept;
}

inline ExperienceDatabase& global_experience_database()
{
    if (auto* database = detail::active_runtime_experience_database())
        return *database;
    static ExperienceDatabase database;
    return database;
}
} // namespace smart
