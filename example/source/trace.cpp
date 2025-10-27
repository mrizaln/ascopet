#include <ascopet/ascopet.hpp>

#include <chrono>
#include <format>
#include <print>
#include <stop_token>
#include <thread>

using namespace std::chrono_literals;

using Clock = std::chrono::steady_clock;

template <typename Duration>
void print_report(std::thread::id id, const ascopet::StrMap<ascopet::TimingStat>& timings)
{
    auto to_duration = [](auto dur) { return std::chrono::duration_cast<Duration>(dur); };

    std::println("\tThread {}", id);
    for (const auto& [name, timing] : timings) {
        auto [dur, intvl, count] = timing;
        std::println("\t> {}", name);
        std::println(
            "\t\t> Dur   [ mean: {} (+/- {}) | median: {} | min: {} | max: {} ]",
            to_duration(dur.mean),
            to_duration(dur.stdev),
            to_duration(dur.median),
            to_duration(dur.min),
            to_duration(dur.max)
        );
        std::println(
            "\t\t> Intvl [ mean: {} (+/- {}) | median: {} | min: {} | max: {} ]",
            to_duration(intvl.mean),
            to_duration(intvl.stdev),
            to_duration(intvl.median),
            to_duration(intvl.min),
            to_duration(intvl.max)
        );
        std::println("\t\t> Count: {}", count);
    }
}

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

    auto start = Clock::now();

    std::println(">> start {}", name);
    for (auto i = 0u; i < count; ++i) {
        auto trace = ascopet::trace(name);    // timing overhead
    }

    using Ms   = std::chrono::duration<double, std::milli>;
    auto to_ms = [&](auto dur) { return std::chrono::duration_cast<Ms>(dur); };

    auto duration = Clock::now() - start;
    std::println(">> end {} in {} ({}/iter)", name, to_ms(duration), duration / count);
}

void single_test(std::size_t count)
{
    auto flag = std::atomic<bool>{ true };
    contention(flag, count, "single_test");
}

void contention_test(std::size_t count)
{
    {
        auto flag = std::atomic<bool>{ false };

        // auto thread1 = std::jthread{ producer, 10ms, "1" };
        // auto thread2 = std::jthread{ producer, 11ms, "2" };
        // auto thread3 = std::jthread{ producer, 12ms, "3" };
        // auto thread4 = std::jthread{ producer, 13ms, "4" };
        // auto thread5 = std::jthread{ producer, 10ms, "5" };
        // auto thread6 = std::jthread{ producer, 11ms, "6" };

        auto thread7  = std::jthread{ contention, std::ref(flag), count, "contention1" };
        auto thread8  = std::jthread{ contention, std::ref(flag), count, "contention2" };
        auto thread9  = std::jthread{ contention, std::ref(flag), count, "contention3" };
        auto thread10 = std::jthread{ contention, std::ref(flag), count, "contention4" };
        auto thread11 = std::jthread{ contention, std::ref(flag), count, "contention5" };
        auto thread12 = std::jthread{ contention, std::ref(flag), count, "contention6" };

        std::this_thread::sleep_for(500ms);

        flag.store(true);
        flag.notify_all();
    }

    if (auto ascopet = ascopet::instance(); ascopet and ascopet->is_tracing()) {
        std::println("\ncontention_test:");
        for (const auto& [id, traces] : ascopet->report()) {
            print_report<std::chrono::nanoseconds>(id, traces);
        }
    } else {
        std::println("\ncontention_test: not initialized or not tracing");
    }
}

void sleep_test()
{
    using namespace std::chrono_literals;

    auto ascopet   = ascopet::instance();
    auto durations = { 1ms, 10ms, 100ms, 1000ms };

    auto sleep_func = [](std::chrono::milliseconds dur) {
        for (auto i = 0; i < 10000 / dur.count(); ++i) {
            auto trace = ascopet::trace("sleep");
            std::this_thread::sleep_for(dur);
        }
    };

    std::println("");

    for (auto dur : durations) {
        ascopet->clear(true);

        std::println("sleep_test: {}", dur);

        sleep_func(dur);

        const auto id = std::this_thread::get_id();
        print_report<std::chrono::duration<float, std::milli>>(id, ascopet->report()[id]);
    }
}

int main()
{
    static constexpr auto count = 10'240'000uz;

    std::println("\n{:-^80}", "uninitialized");
    single_test(count);
    std::println("");
    contention_test(count);

    std::println("\n{:-^80}", "init");
    auto ascopet = ascopet::init({
        .immediately_start = true,
        .poll_interval     = 25ms,
        .record_capacity   = 10240,    // per-label buffer; got collected from tls buffer every poll_interval
        .buffer_capacity   = 10240,    // per-thread buffer (tls) caching trace data on each thread
    });

    // record capacity can be resized on-the-fly. buffer capacity can't be resized
    ascopet->resize_record_capacity(512);

    // trace data are recorded using rdtsc, which you can check the frequency using this
    std::println(
        "tsc_freq: {} Hz ({} MHz)",
        ascopet->tsc_freq(),
        static_cast<float>(ascopet->tsc_freq()) / std::mega::num
    );

    std::println("\n{:-^80}", "paused");
    ascopet->pause_tracing();
    single_test(count);
    std::println("");
    contention_test(count);

    std::println("\n{:-^80}", "running");
    ascopet->start_tracing();
    single_test(count);
    std::println("");
    contention_test(count);
    // sleep_test();
}
