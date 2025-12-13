#pragma once

#include "ascopet/common.hpp"
#include "ascopet/ringbuf.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <shared_mutex>
#include <source_location>
#include <stop_token>
#include <thread>

// ascopet -> a-scope-t: asynchronous scope timer
namespace ascopet
{
    class LocalBuf;

    using Duration = std::chrono::duration<long, std::nano>;

    struct TimingStat
    {
        struct Stat
        {
            Duration mean;
            Duration median;
            Duration stdev;
            Duration min;
            Duration max;
        };

        Stat        duration;
        Stat        interval;
        std::size_t count;
    };

    class TimingList
    {
    public:
        TimingList(std::size_t capacity);

        void push_back(const NamedRecord& record);
        void clear(bool remove_entries);
        void resize(std::size_t new_capacity);

        StrMap<TimingStat>      stat(std::uint64_t freq) const;
        StrMap<RingBuf<Record>> records() const;

    private:
        std::size_t             m_capacity;
        StrMap<RingBuf<Record>> m_records;
    };

    class [[nodiscard]] Tracer
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
        std::uint64_t    m_start;
    };

    using Report    = ThreadMap<StrMap<TimingStat>>;
    using RawReport = ThreadMap<StrMap<RingBuf<Record>>>;

    struct InitParam
    {
        bool        immediately_start = false;
        Duration    poll_interval     = std::chrono::milliseconds{ 100 };
        std::size_t record_capacity   = 1024;
        std::size_t buffer_capacity   = 1024;
    };

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

        std::uint64_t tsc_freq() const;

    private:
        Ascopet(InitParam&& param);

        void add_localbuf(std::thread::id id, LocalBuf& buffer);
        void remove_localbuf(std::thread::id id);

        void worker(std::stop_token st);

        static std::unique_ptr<Ascopet> s_instance;

        mutable std::shared_mutex m_data_mutex;
        mutable std::mutex        m_cond_mutex;
        std::condition_variable   m_cv;

        ThreadMap<TimingList> m_records;

        std::atomic<bool>    m_processing;
        std::jthread         m_worker;
        ThreadMap<LocalBuf*> m_buffers;

        std::size_t m_record_capacity;
        std::size_t m_buffer_capacity;

        Duration      m_process_interval;
        std::uint64_t m_tsc_freq;
    };

    Ascopet* instance();
    Ascopet* init(InitParam&& param = {});

    Tracer trace(std::source_location location = std::source_location::current());
    Tracer trace(std::string_view name);
}
