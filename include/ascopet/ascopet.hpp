#pragma once

#include "ascopet/common.hpp"
#include "ascopet/ringbuf.hpp"

#include <atomic>
#include <chrono>
#include <shared_mutex>
#include <source_location>
#include <stop_token>
#include <thread>

// ascopet -> a-scope-t: asynchronous scope timer
namespace ascopet
{
    class LocalBuf;
    struct InitParam;

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

        void push_back(const NamedRecord& record);
        void clear(bool remove_entries);
        void resize(std::size_t new_capacity);

        StrMap<TimingStat>      stat() const;
        StrMap<RingBuf<Record>> records() const;

    private:
        std::size_t             m_capacity;
        StrMap<RingBuf<Record>> m_records;
    };

    class Tracer
    {
    public:
        ~Tracer();
        Tracer(LocalBuf* buffer, std::string_view name);

        Tracer(Tracer&&)            = delete;
        Tracer& operator=(Tracer&&) = delete;

        Tracer(const Tracer&)            = delete;
        Tracer& operator=(const Tracer&) = delete;

    private:
        LocalBuf*        m_buffer;
        std::string_view m_name;
        Timepoint        m_start;
    };

    using Report    = ThreadMap<StrMap<TimingStat>>;
    using RawReport = ThreadMap<StrMap<RingBuf<Record>>>;

    class Ascopet
    {
    public:
        friend LocalBuf;

        friend Ascopet* instance();
        friend Ascopet* init(InitParam&& param);

        ~Ascopet();

        Report report() const;
        Report report_consume(bool remove_entries);

        RawReport raw_report() const;

        void clear(bool remove_entries = false);

        bool is_tracing() const;
        void start_tracing();
        void pause_tracing();

        std::size_t record_capacity() const;
        std::size_t localbuf_capacity() const;

        void resize_record_capacity(std::size_t capacity);

        Duration process_interval() const;
        void     set_process_interval(Duration interval);

    private:
        Ascopet(InitParam&& param);

        void add_localbuf(std::thread::id id, LocalBuf& buffer);
        void remove_localbuf(std::thread::id id);

        void worker(std::stop_token st);

        static std::unique_ptr<Ascopet> s_instance;

        mutable std::shared_mutex m_mutex;
        ThreadMap<TimingList>     m_records;

        std::atomic<bool>    m_processing;
        std::jthread         m_worker;
        ThreadMap<LocalBuf*> m_buffers;

        std::size_t m_record_capacity;
        std::size_t m_buffer_capacity;
        Duration    m_process_interval;
    };

    struct InitParam
    {
        bool        m_immediately_start = false;
        Duration    m_interval          = std::chrono::milliseconds{ 100 };
        std::size_t m_record_capacity   = 1024;
        std::size_t m_buffer_capacity   = 1024;
    };

    Ascopet* instance();
    Ascopet* init(InitParam&& param);

    Tracer trace(std::source_location location = std::source_location::current());
    Tracer trace(std::string_view name);
}
