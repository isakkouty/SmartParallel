#pragma once

#include <string>

#include <smart/experience/experience_database.hpp>

namespace smart
{
    inline void save_experience(const std::string& path)
    {
        global_experience_database().save_to_file(path);
    }

    inline void load_experience(const std::string& path)
    {
        global_experience_database().load_from_file(path);
    }
}
