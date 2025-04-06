#include <ascopet/queue.hpp>

#include <iostream>
#include <stop_token>
#include <thread>

void producer(std::stop_token st, ascopet::Queue& queue, ascopet::Duration duration, std::string_view name)
{
    using Clock = ascopet::Clock;
    std::this_thread::sleep_for(duration * 10);

    while (not st.stop_requested()) {
        auto now = Clock::now();
        std::this_thread::sleep_for(duration);
        std::cout << ">> " << name << '\n';
        queue.push(name, now);
    }

    std::cout << "end " << name << '\n';
}

int main()
{
    using namespace std::chrono_literals;

    auto queue = ascopet::Queue{ 1024 };

    auto thread1 = std::jthread(producer, std::ref(queue), 10ms, "1");
    auto thread2 = std::jthread(producer, std::ref(queue), 11ms, "2");
    auto thread3 = std::jthread(producer, std::ref(queue), 12ms, "3");
    auto thread4 = std::jthread(producer, std::ref(queue), 13ms, "4");
    auto thread5 = std::jthread(producer, std::ref(queue), 14ms, "5");

    auto start = ascopet::Clock::now();

    auto to_ms = [](ascopet::Duration d) { return std::chrono::duration_cast<std::chrono::milliseconds>(d); };

    for (auto i = 0u; i < 4; ++i) {
        std::this_thread::sleep_for(100ms);
        std::cout << "<< main\n";
        queue.consume([&](std::thread::id /*id*/, std::string_view name, auto&& record) {
            std::cout << "\t" << name << ":\t" << to_ms(record.m_duration) << '\n';
        });
    }
    std::cout << "end 6\n";
    std::cout << "end in: " << (ascopet::Clock::now() - start) << '\n';
}
