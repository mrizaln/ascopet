#include "ascopet/ascopet.hpp"

#include <algorithm>
#include <cmath>
#include <mutex>

std::pair<std::vector<ascopet::Duration>, std::vector<ascopet::Duration>> split_duration_interval(
    const circbuf::CircBuf<ascopet::Record>& records
)
{
    assert(records.size() >= 2);

    auto durations = std::vector<ascopet::Duration>{};
    auto intervals = std::vector<ascopet::Duration>{};

    durations.reserve(records.size());
    intervals.reserve(records.size() - 1);

    for (auto i = 0u; i < records.size(); ++i) {
        auto [dur, start] = records.at(i);
        durations.push_back(dur);
        if (i > 0) {
            auto dt = start - records.at(i - 1).m_start;
            intervals.push_back(dt);
        }
    }

    return { std::move(durations), std::move(intervals) };
}

ascopet::TimingStat calculate_stat(const circbuf::CircBuf<ascopet::Record>& records)
{
    using namespace ascopet;

    if (const auto size = records.size(); size == 0) {
        return {};
    } else if (size < 2) {
        return {
            .m_duration_mean   = records.front().m_duration,
            .m_duration_median = records.front().m_duration,
            .m_duration_stdev  = {},
            .m_duration_min    = records.front().m_duration,
            .m_duration_max    = records.front().m_duration,

            .m_interval_mean   = {},
            .m_interval_median = {},
            .m_interval_stdev  = {},
            .m_interval_min    = {},
            .m_interval_max    = {},

            .m_count = 1,
        };
    }

    auto [durations, intervals] = split_duration_interval(records);

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
        .m_duration_mean   = dur_mean,
        .m_duration_median = durations[durations.size() / 2],
        .m_duration_stdev  = dur_stdev,
        .m_duration_min    = dur_min,
        .m_duration_max    = dur_max,

        .m_interval_mean   = intvl_mean,
        .m_interval_median = intervals[intervals.size() / 2],
        .m_interval_stdev  = intvl_stdev,
        .m_interval_min    = intvl_min,
        .m_interval_max    = intvl_max,

        .m_count = durations.size(),
    };
}

namespace ascopet
{
    TimingList::TimingList(std::size_t capacity)
        : m_capacity{ capacity }
    {
        assert(capacity > 0);
    }

    void TimingList::push(std::string_view name, Record record)
    {
        auto it = m_records.find(name);
        if (it != m_records.end()) {
            it->second.push_back(record);
        } else {
            auto [it, _] = m_records.emplace(name, m_capacity);
            it->second.push_back(record);
        }
    }

    StrMap<TimingStat> TimingList::stat() const
    {
        auto reports = StrMap<TimingStat>{};
        for (const auto& [name, records] : m_records) {
            reports.emplace(name, calculate_stat(records));
        }
        return reports;
    }
}

namespace ascopet
{
    Inserter::Inserter(Ascopet* ascopet, std::string_view name)
        : m_ascopet{ ascopet }
        , m_name{ name }
        , m_start{ Clock::now() }
    {
    }

    Inserter::~Inserter()
    {
        if (m_ascopet) {
            m_ascopet->insert(m_name, m_start);
        }
    }
}

namespace ascopet
{
    Ascopet::Ascopet(std::size_t record_capacity, Duration interval, bool start)
        : m_queue{ record_capacity }
        , m_worker{ std::jthread([this](std::stop_token st) { worker(st); }) }
        , m_record_capacity{ record_capacity }
        , m_processing{ start }
        , m_process_interval{ interval }
    {
    }

    Ascopet::~Ascopet()
    {
        m_worker.request_stop();

        // need to do this to wake up the worker thread in case it's waiting
        m_processing.store(true, std::memory_order::release);
        m_processing.notify_one();

        m_worker.join();
        m_processing.store(false, std::memory_order::release);
    }

    Ascopet::Report Ascopet::report() const
    {
        auto report = Report{};
        auto lock   = std::shared_lock{ m_records_mutex };
        for (const auto& [id, records] : m_records) {
            report.emplace_back(id, records.stat());
        }
        return report;
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

    void Ascopet::worker(std::stop_token st)
    {
        m_processing.wait(false);

        while (not st.stop_requested()) {
            {
                auto lock = std::unique_lock{ m_records_mutex };
                m_queue.consume([this](std::thread::id id, std::string_view name, Record&& record) {
                    if (name == "") {
                        return;
                    }
                    auto it = m_records.find(id);
                    if (it != m_records.end()) {
                        it->second.push(name, record);
                    } else {
                        auto [it, _] = m_records.emplace(id, m_record_capacity);
                        it->second.push(name, record);
                    }
                });
            }

            std::this_thread::sleep_for(m_process_interval);
            m_processing.wait(false);
        }
    }

    void Ascopet::insert(std::string_view name, Timepoint start)
    {
        if (is_tracing()) {
            m_queue.push(name, start);
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

    Ascopet* init(bool immediately_start, std::size_t record_capacity, Duration interval)
    {
        if (not Ascopet::s_instance) {
            Ascopet::s_instance = std::make_unique<Ascopet>(record_capacity, interval, immediately_start);
        }
        return Ascopet::s_instance.get();
    }

    Inserter trace(std::source_location location)
    {
        auto name = location.function_name();
        return { Ascopet::s_instance.get(), name };
    }

    Inserter trace(std::string_view name)
    {
        return { Ascopet::s_instance.get(), name };
    }
}
