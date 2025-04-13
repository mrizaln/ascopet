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

void contention(std::atomic<bool>& flag, std::size_t count, std::string_view name)
{
    flag.wait(false);

    std::println(">> start {}", name);
    for (auto i = 0u; i < count; ++i) {
        auto trace = ascopet::trace(name);    // timing overhead
    }
    std::println(">> end {}", name);
}

void baseline_overhead(std::size_t count)
{
    auto min_rep = std::numeric_limits<ascopet::Clock::rep>::max();
    auto max_rep = std::numeric_limits<ascopet::Clock::rep>::min();

    auto min = ascopet::Duration{ min_rep };
    auto max = ascopet::Duration{ max_rep };

    while (count-- > 0) {
        auto t1 = ascopet::Clock::now();
        auto t2 = ascopet::Clock::now();

        auto diff = t2 - t1;

        if (diff < min) {
            min = diff;
        }
        if (diff > max) {
            max = diff;
        }
    }

    std::println("Baseline overhead:");
    std::println("\tMin: {}", min);
    std::println("\tMax: {}", max);
}

int main()
{
    auto ascopet = ascopet::init({
        .immediately_start = true,
        .poll_interval     = 25ms,
        .buffer_capacity   = 10240,
    });

    auto flag = std::atomic<bool>{ false };

    {
        baseline_overhead(1'000'000);

        // auto thread1 = std::jthread{ producer, 10ms, "1" };
        // auto thread2 = std::jthread{ producer, 11ms, "2" };
        // auto thread3 = std::jthread{ producer, 12ms, "3" };
        // auto thread4 = std::jthread{ producer, 13ms, "4" };
        // auto thread5 = std::jthread{ producer, 10ms, "5" };
        // auto thread6 = std::jthread{ producer, 11ms, "6" };

        auto thread7  = std::jthread{ contention, std::ref(flag), 10'024'000, "contention1" };
        auto thread8  = std::jthread{ contention, std::ref(flag), 10'024'000, "contention2" };
        auto thread9  = std::jthread{ contention, std::ref(flag), 10'024'000, "contention3" };
        auto thread10 = std::jthread{ contention, std::ref(flag), 10'024'000, "contention4" };
        auto thread11 = std::jthread{ contention, std::ref(flag), 10'024'000, "contention5" };
        auto thread12 = std::jthread{ contention, std::ref(flag), 10'024'000, "contention6" };

        flag.store(true);
        flag.notify_all();

        ascopet->resize_record_capacity(512);

        std::this_thread::sleep_for(1s);
    }

    std::println("\nReport:");
    for (const auto& [id, traces] : ascopet->report()) {
        std::println("\tThread {}", id);
        for (const auto& [name, timing] : traces) {
            auto [dur, intvl, count] = timing;
            std::println("\t> {}", name);
            std::println(
                "\t\t> Dur   [ mean: {} (+/- {}) | median: {} | min: {} | max: {} ]",
                dur.mean,
                dur.stdev,
                dur.median,
                dur.min,
                dur.max
            );
            std::println(
                "\t\t> Intvl [ mean: {} (+/- {}) | median: {} | min: {} | max: {} ]",
                intvl.mean,
                intvl.stdev,
                intvl.median,
                intvl.min,
                intvl.max
            );
            std::println("\t\t> Count: {}", count);
        }
    }
}
