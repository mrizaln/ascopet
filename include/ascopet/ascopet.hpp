#pragma once

#include <ascopet/common.hpp>
#include <ascopet/queue.hpp>
#include <ascopet/record.hpp>

#include <atomic>
#include <chrono>
#include <shared_mutex>
#include <source_location>
#include <stop_token>
#include <thread>

// ascopet -> a-scope-t: asynchronous scope timer
namespace ascopet
{
    struct TimingStat
    {
        struct Stat
        {
            Duration m_mean;
            Duration m_median;
            Duration m_stdev;
            Duration m_min;
            Duration m_max;
        };

        Stat        m_duration;
        Stat        m_interval;
        std::size_t m_count = 0;
    };

    class TimingList
    {
    public:
        TimingList(std::size_t capacity);

        void push(std::string_view name, Record&& record);
        void clear(bool remove_entries);

        StrMap<TimingStat> stat() const;

    private:
        std::size_t          m_capacity;
        StrMap<RecordBuffer> m_records;
    };

    class Ascopet;

    class Inserter
    {
    public:
        Inserter() = default;
        ~Inserter();

        Inserter(Ascopet* ascopet, std::string_view name);

        Inserter(Inserter&&)            = delete;
        Inserter& operator=(Inserter&&) = delete;

        Inserter(const Inserter&)            = delete;
        Inserter& operator=(const Inserter&) = delete;

    private:
        Ascopet*         m_ascopet = nullptr;
        std::string_view m_name;
        Timepoint        m_start;
    };

    class Ascopet
    {
    public:
        friend Inserter;

        friend Ascopet* instance();
        friend Ascopet* init(bool, std::size_t, Duration);

        friend Inserter trace(std::source_location);
        friend Inserter trace(std::string_view);

        using Report = std::vector<std::pair<std::thread::id, StrMap<TimingStat>>>;

        Ascopet(std::size_t record_capacity, Duration interval, bool start);
        ~Ascopet();

        Report report() const;
        Report report_consume(bool remove_entries);

        bool is_tracing() const;
        void start_tracing();
        void pause_tracing();

    private:
        static std::unique_ptr<Ascopet> s_instance;

        void worker(std::stop_token st);
        void insert(std::string_view name, Timepoint start);

        mutable std::shared_mutex m_records_mutex;
        ThreadMap<TimingList>     m_records;
        Queue                     m_queue;

        std::jthread m_worker;

        std::size_t       m_record_capacity;
        std::atomic<bool> m_processing = false;
        Duration          m_process_interval;
    };

    Ascopet* instance();

    Ascopet* init(
        bool        immediately_start = false,
        std::size_t record_capacity   = 1024,
        Duration    interval          = std::chrono::milliseconds{ 100 }
    );

    Inserter trace(std::source_location location = std::source_location::current());
    Inserter trace(std::string_view name);
}
