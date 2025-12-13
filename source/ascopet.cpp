#if not defined(ASCOPET_DISABLE_RDTSC)
#include "rdtsc.hpp"
#endif

#include "ascopet/ascopet.hpp"
#include "ascopet/localbuf.hpp"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <span>
#include <vector>

using Fallback = std::chrono::steady_clock;

namespace
{
    ascopet::Duration to_duration(std::uint64_t start, std::uint64_t end, std::uint64_t freq)
    {
        return ascopet::Duration{ (end - start) * ascopet::Duration::period::den / freq };
    }

    std::pair<std::vector<ascopet::Duration>, std::vector<ascopet::Duration>> split_duration_interval(
        const ascopet::RingBuf<ascopet::Record>& records,
        std::uint64_t                            freq
    )
    {
        assert(records.size() >= 2);

        auto durations = std::vector<ascopet::Duration>{};
        auto intervals = std::vector<ascopet::Duration>{};

        durations.reserve(records.size());
        intervals.reserve(records.size() - 1);

        for (auto i = 0u; i < records.size(); ++i) {
            auto [start, end] = records[i];
            durations.push_back(to_duration(start, end, freq));
            if (i > 0) {
                auto dt = to_duration(records[i - 1].start, start, freq);
                intervals.push_back(dt);
            }
        }

        return { std::move(durations), std::move(intervals) };
    }

    ascopet::TimingStat calculate_stat(const ascopet::RingBuf<ascopet::Record>& records, std::uint64_t freq)
    {
        using namespace ascopet;

        if (const auto size = records.size(); size == 0) {
            return {};
        } else if (size < 2) {
            auto dur = to_duration(records[0].start, records[0].end, freq);
            return {
                .duration = {
                    .mean   = dur,
                    .median = dur,
                    .stdev  = {},
                    .min    = dur,
                    .max    = dur,
                },
                .interval = {
                    .mean   = {},
                    .median = {},
                    .stdev  = {},
                    .min    = {},
                    .max    = {},
                },
                .count = 1,
            };
        }

        auto [durations, intervals] = split_duration_interval(records, freq);

        const auto mean_stdev_min_max = [](std::span<const Duration> durations) -> std::array<Duration, 4> {
            auto min = Duration{ std::numeric_limits<Duration::rep>::max() };
            auto max = Duration{ std::numeric_limits<Duration::rep>::min() };
            auto sum = Duration{ 0 };

            for (auto dur : durations) {
                sum += dur;
                if (dur < min) {
                    min = dur;
                } else if (dur > max) {
                    max = dur;
                }
            }
            auto avg = sum / static_cast<Duration::rep>(durations.size());

            auto diff_sum = 0.0;
            for (auto dur : durations) {
                auto diff  = static_cast<double>(dur.count() - avg.count());
                diff_sum  += diff * diff;
            }
            auto stdev = std::sqrt(diff_sum / static_cast<double>(durations.size()));

            return { avg, Duration{ static_cast<Duration::rep>(stdev) }, min, max };
        };

        auto [dur_mean, dur_stdev, dur_min, dur_max]         = mean_stdev_min_max(durations);
        auto [intvl_mean, intvl_stdev, intvl_min, intvl_max] = mean_stdev_min_max(intervals);

        auto dur_mid = durations.begin() + durations.size() / 2;
        std::nth_element(durations.begin(), dur_mid, durations.end());

        auto intvl_mid = intervals.begin() + intervals.size() / 2;
        std::nth_element(intervals.begin(), intvl_mid, intervals.end());

        return {
            .duration = {
                .mean   = dur_mean,
                .median = durations[durations.size() / 2],
                .stdev  = dur_stdev,
                .min    = dur_min,
                .max    = dur_max,
            },
            .interval = {
                .mean   = intvl_mean,
                .median = intervals[intervals.size() / 2],
                .stdev  = intvl_stdev,
                .min    = intvl_min,
                .max    = intvl_max,
            },
            .count = records.actual_count(),
        };
    }
}

namespace ascopet
{
    TimingList::TimingList(std::size_t capacity)
        : m_capacity{ capacity }
    {
        assert(capacity > 0);
    }

    void TimingList::push_back(const NamedRecord& record)
    {
        auto it = m_records.find(record.name);
        if (it != m_records.end()) {
            it->second.push_back({ record.start, record.end });
        } else {
            auto [it, _] = m_records.emplace(record.name, m_capacity);
            it->second.push_back({ record.start, record.end });
        }
    }

    void TimingList::clear(bool remove_entries)
    {
        if (remove_entries) {
            m_records.clear();
        } else {
            for (auto& [name, records] : m_records) {
                records.clear();
            }
        }
    }

    void TimingList::resize(std::size_t new_capacity)
    {
        if (new_capacity == m_capacity) {
            return;
        }
        for (auto& [_, records] : m_records) {
            records.resize(new_capacity);
        }
    }

    StrMap<TimingStat> TimingList::stat(std::uint64_t freq) const
    {
        auto reports = StrMap<TimingStat>{};
        for (const auto& [name, records] : m_records) {
            reports.emplace(name, calculate_stat(records, freq));
        }
        return reports;
    }

    StrMap<RingBuf<Record>> TimingList::records() const
    {
        return m_records;
    }
}

namespace ascopet
{
    Tracer::Tracer(LocalBuf* buffer, std::string_view name)
        : m_buffer{ buffer }
        , m_name{ name }
#if not defined(ASCOPET_DISABLE_RDTSC)
        , m_start{ __rdtsc() }
#else
        , m_start{ static_cast<std::uint64_t>(Fallback::now().time_since_epoch().count()) }
#endif
    {
    }

    Tracer::~Tracer()
    {
        if (m_buffer) {
            m_buffer->add_record({
                .name  = m_name,
                .start = m_start,
#if not defined(ASCOPET_DISABLE_RDTSC)
                .end = __rdtsc(),
#else
                .end = static_cast<std::uint64_t>(Fallback::now().time_since_epoch().count()),
#endif
            });
        }
    }
}

namespace ascopet
{
    Ascopet::Ascopet(InitParam&& param)
        : m_processing{ param.immediately_start }
        , m_worker{ std::jthread([this](std::stop_token st) { worker(st); }) }
        , m_record_capacity{ param.record_capacity }
        , m_buffer_capacity{ param.buffer_capacity }
        , m_process_interval{ param.poll_interval }
#if not defined(ASCOPET_DISABLE_RDTSC)
        , m_tsc_freq{ get_rdtsc_freq() }
#else
        , m_tsc_freq{ Fallback::period::den }
#endif
    {
    }

    Ascopet::~Ascopet()
    {
        m_worker.request_stop();

        // NOTE: Even if the shared variable is atomic, it must be modified under the mutex in order to
        // correctly publish the modification to the waiting thread
        {
            auto lock = std::lock_guard{ m_cond_mutex };

            // need to do this to wake up the worker thread in case it's waiting
            m_processing.store(true, std::memory_order::release);
            m_processing.notify_one();
        }

        m_cv.notify_all();

        m_worker.join();
        m_processing.store(false, std::memory_order::release);
    }

    ascopet::Report Ascopet::report() const
    {
        auto report = ThreadMap<StrMap<TimingStat>>{};
        auto lock   = std::shared_lock{ m_data_mutex };
        for (const auto& [id, records] : m_records) {
            report.emplace(id, records.stat(m_tsc_freq));
        }
        return report;
    }

    ascopet::Report Ascopet::report_consume(bool remove_entries)
    {
        auto report = ThreadMap<StrMap<TimingStat>>{};
        auto lock   = std::shared_lock{ m_data_mutex };
        for (auto& [id, records] : m_records) {
            report.emplace(id, records.stat(m_tsc_freq));
            records.clear(remove_entries);
        }
        if (remove_entries) {
            m_records.clear();
        }
        return report;
    }

    ascopet::RawReport Ascopet::raw_report() const
    {
        auto lock    = std::shared_lock{ m_data_mutex };
        auto records = ThreadMap<StrMap<RingBuf<Record>>>{};
        for (const auto& [id, timing_list] : m_records) {
            records.emplace(id, timing_list.records());
        }
        return records;
    }

    void Ascopet::clear(bool remove_entries)
    {
        auto lock = std::unique_lock{ m_data_mutex };
        for (auto& [id, records] : m_records) {
            records.clear(remove_entries);
        }
        if (remove_entries) {
            m_records.clear();
        }
    }

    bool Ascopet::is_tracing() const
    {
        return m_processing.load(std::memory_order::acquire);
    }

    void Ascopet::start_tracing()
    {
        m_processing.store(true, std::memory_order::release);
        m_processing.notify_one();
    }

    void Ascopet::pause_tracing()
    {
        m_processing.store(false, std::memory_order::release);
        m_processing.notify_one();
    }

    std::size_t Ascopet::record_capacity() const
    {
        auto lock = std::shared_lock{ m_data_mutex };
        return m_record_capacity;
    }

    std::size_t Ascopet::localbuf_capacity() const
    {
        auto lock = std::shared_lock{ m_data_mutex };
        return m_buffer_capacity;
    }

    void Ascopet::resize_record_capacity(std::size_t capacity)
    {
        auto lock         = std::unique_lock{ m_data_mutex };
        m_record_capacity = capacity;
        for (auto& [id, records] : m_records) {
            records.resize(capacity);
        }
    }

    ascopet::Duration Ascopet::process_interval() const
    {
        auto lock = std::shared_lock{ m_data_mutex };
        return m_process_interval;
    }

    void Ascopet::set_process_interval(Duration interval)
    {
        auto lock          = std::unique_lock{ m_data_mutex };
        m_process_interval = interval;
    }

    std::uint64_t Ascopet::tsc_freq() const
    {
        return m_tsc_freq;
    }

    void Ascopet::add_localbuf(std::thread::id id, LocalBuf& buffer)
    {
        auto lock = std::unique_lock{ m_data_mutex };
        m_buffers.emplace(id, &buffer);
    }

    void Ascopet::remove_localbuf(std::thread::id id)
    {
        auto lock = std::unique_lock{ m_data_mutex };
        m_buffers.erase(id);
    }

    void Ascopet::worker(std::stop_token st)
    {
        m_processing.wait(false);

        using Clock = std::chrono::steady_clock;

        while (not st.stop_requested()) {
            auto elapsed = [this] {
                auto start = Clock::now();
                auto lock  = std::unique_lock{ m_data_mutex };

                for (auto [id, buffer] : m_buffers) {
                    auto& records = buffer->swap();

                    auto it = m_records.find(id);
                    if (it == m_records.end()) {
                        auto [new_it, _] = m_records.emplace(id, m_record_capacity);
                        it               = new_it;
                    }

                    for (auto i = 0u; i < records.size(); ++i) {
                        auto& record = records[i];
                        it->second.push_back(record);
                    }

                    records.clear();
                }

                return Clock::now() - start;
            }();

            {
                auto lock = std::unique_lock{ m_cond_mutex };
                m_cv.wait_for(lock, m_process_interval - elapsed);
            }

            m_processing.wait(false);
        }
    }
}

namespace ascopet
{
    // NOTE: I'm forced to do the old way of initializing here since clang complains if I use static inline...
    std::unique_ptr<Ascopet> Ascopet::s_instance = nullptr;

    Ascopet* instance()
    {
        return Ascopet::s_instance.get();
    }

    Ascopet* init(InitParam&& param)
    {
        if (not Ascopet::s_instance) {
            Ascopet::s_instance.reset(new Ascopet{ std::move(param) });
        }
        return Ascopet::s_instance.get();
    }

    Tracer trace(std::source_location location)
    {
        auto name = location.function_name();
        return trace(name);
    }

    Tracer trace(std::string_view name)
    {
        if (auto ptr = instance(); ptr != nullptr and ptr->is_tracing()) {
            static thread_local auto buffer = LocalBuf{ ptr };
            return { &buffer, name };
        }
        return { nullptr, name };
    }
}
