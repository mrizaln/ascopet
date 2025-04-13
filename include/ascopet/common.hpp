#pragma once

#include <chrono>
#include <thread>
#include <unordered_map>

namespace ascopet
{
    using Clock     = std::chrono::steady_clock;
    using Timepoint = Clock::time_point;
    using Duration  = Clock::duration;

    struct Record
    {
        Timepoint start;
        Timepoint end;
    };

    struct NamedRecord
    {
        std::string_view name;
        Timepoint        start;
        Timepoint        end;
    };

    struct StrHash
    {
        using is_transparent = void;
        using hash           = std::hash<std::string_view>;

        std::size_t operator()(const char* str) const { return std::hash<std::string_view>{}(str); }
        std::size_t operator()(std::string_view str) const { return std::hash<std::string_view>{}(str); }
        std::size_t operator()(const std::string& str) const { return std::hash<std::string>{}(str); }
    };

    template <typename T>
    using StrMap = std::unordered_map<std::string, T, StrHash, std::equal_to<>>;

    template <typename T>
    using ThreadMap = std::unordered_map<std::thread::id, T>;
}
