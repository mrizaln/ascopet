#pragma once

#include <thread>
#include <unordered_map>

namespace ascopet
{
    struct Record
    {
        std::uint64_t start;
        std::uint64_t end;
    };

    struct NamedRecord
    {
        std::string_view name;
        std::uint64_t    start;
        std::uint64_t    end;
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
