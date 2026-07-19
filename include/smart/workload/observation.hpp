#pragma once

namespace smart
{
enum class ObservationSource
{
    Exact,
    Derived,
    Estimated,
    Sampled,
    Unavailable
};

enum class ObservationConfidence
{
    Unavailable,
    Low,
    Medium,
    High
};

struct ObservationMetadata
{
    ObservationSource source = ObservationSource::Unavailable;
    ObservationConfidence confidence = ObservationConfidence::Unavailable;
};

inline const char* observation_source_name(ObservationSource source)
{
    switch (source)
    {
        case ObservationSource::Exact:
            return "Exact";
        case ObservationSource::Derived:
            return "Derived";
        case ObservationSource::Estimated:
            return "Estimated";
        case ObservationSource::Sampled:
            return "Sampled";
        case ObservationSource::Unavailable:
            return "Unavailable";
    }

    return "Unknown";
}

inline const char* observation_confidence_name(ObservationConfidence confidence)
{
    switch (confidence)
    {
        case ObservationConfidence::Unavailable:
            return "Unavailable";
        case ObservationConfidence::Low:
            return "Low";
        case ObservationConfidence::Medium:
            return "Medium";
        case ObservationConfidence::High:
            return "High";
    }

    return "Unknown";
}
} // namespace smart
