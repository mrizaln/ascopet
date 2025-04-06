#include <ascopet/ascopet.hpp>

#include <format>
#include <print>
#include <stop_token>
#include <thread>

using namespace std::chrono_literals;

void producer(std::stop_token st, ascopet::Duration duration, std::string_view name)
{
    std::this_thread::sleep_for(duration * 10);

    while (not st.stop_requested()) {
        auto trace = ascopet::trace(name);

        std::this_thread::sleep_for(duration);
        std::println(">> {}", name);
    }

    std::println(">> end {}", name);
}

void contention(std::size_t count, std::string_view name)
{
    for (auto i = 0u; i < count; ++i) {
        auto trace = ascopet::trace(name);    // timing overhead
    }
}

int main()
{
    auto ascopet = ascopet::init(true, 10240);

    {
        // auto thread1 = std::jthread{ producer, 10ms, "1" };
        // auto thread2 = std::jthread{ producer, 11ms, "2" };
        // auto thread3 = std::jthread{ producer, 12ms, "3" };
        // auto thread4 = std::jthread{ producer, 13ms, "4" };

        // std::this_thread::sleep_for(1s);

        auto thread1 = std::jthread{ contention, 1024, "contention1" };
        auto thread2 = std::jthread{ contention, 1024, "contention2" };
        auto thread3 = std::jthread{ contention, 1024, "contention3" };
        auto thread4 = std::jthread{ contention, 1024, "contention4" };
        auto thread5 = std::jthread{ contention, 1024, "contention5" };
        auto thread6 = std::jthread{ contention, 1024, "contention6" };
        auto thread7 = std::jthread{ contention, 1024, "contention7" };
        auto thread8 = std::jthread{ contention, 1024, "contention8" };

        std::this_thread::sleep_for(1s);
    }

    std::println("\nReport:");
    for (const auto& [id, traces] : ascopet->report()) {
        std::println("\tThread {}", id);
        for (const auto& [name, timing] : traces) {
            std::println("\t> {}", name);
            std::println(
                "\t\t> Dur   [ mean: {} (+/- {}) | median: {} | min: {} | max: {} ]",
                timing.m_duration_mean,
                timing.m_duration_stdev,
                timing.m_duration_median,
                timing.m_duration_min,
                timing.m_duration_max
            );
            std::println(
                "\t\t> Intvl [ mean: {} (+/- {}) | median: {} | min: {} | max: {} ]",
                timing.m_interval_mean,
                timing.m_interval_stdev,
                timing.m_interval_median,
                timing.m_interval_min,
                timing.m_interval_max
            );
            std::println("\t\t> Count: {}", timing.m_count);
        }
    }
}
