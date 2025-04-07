#include "ascopet/ascopet.hpp"

#include <algorithm>
#include <cmath>
#include <mutex>

std::pair<std::vector<ascopet::Duration>, std::vector<ascopet::Duration>> split_duration_interval(
    const ascopet::RecordBuffer& records
)
{
    assert(records.size() >= 2);

    auto durations = std::vector<ascopet::Duration>{};
    auto intervals = std::vector<ascopet::Duration>{};

    durations.reserve(records.size());
    intervals.reserve(records.size() - 1);

    for (auto i = 0u; i < records.size(); ++i) {
        auto [dur, start] = records[i];
        durations.push_back(dur);
        if (i > 0) {
            auto dt = start - records[i - 1].m_start;
            intervals.push_back(dt);
        }
    }

    return { std::move(durations), std::move(intervals) };
}

ascopet::TimingStat calculate_stat(const ascopet::RecordBuffer& records)
{
    using namespace ascopet;

    if (const auto size = records.size(); size == 0) {
        return {};
    } else if (size < 2) {
        return {
            .m_duration = {
                .m_mean   = records[0].m_duration,
                .m_median = records[0].m_duration,
                .m_stdev  = {},
                .m_min    = records[0].m_duration,
                .m_max    = records[0].m_duration,
            },
            .m_interval = {
                .m_mean   = {},
                .m_median = {},
                .m_stdev  = {},
                .m_min    = {},
                .m_max    = {},
            },
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
        .m_duration = {
            .m_mean   = dur_mean,
            .m_median = durations[durations.size() / 2],
            .m_stdev  = dur_stdev,
            .m_min    = dur_min,
            .m_max    = dur_max,
        },
        .m_interval = {
            .m_mean   = intvl_mean,
            .m_median = intervals[intervals.size() / 2],
            .m_stdev  = intvl_stdev,
            .m_min    = intvl_min,
            .m_max    = intvl_max,
        },
        .m_count = records.actual_count(),
    };
}

namespace ascopet
{
    TimingList::TimingList(std::size_t capacity)
        : m_capacity{ capacity }
    {
        assert(capacity > 0);
    }

    void TimingList::push(std::string_view name, Record&& record)
    {
        auto it = m_records.find(name);
        if (it != m_records.end()) {
            it->second.push_back(std::move(record));
        } else {
            auto [it, _] = m_records.emplace(name, m_capacity);
            it->second.push_back(std::move(record));
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
        : m_queue{ record_capacity * std::thread::hardware_concurrency() }
        , m_worker{ std::jthread([this](std::stop_token st) { worker(st); }) }
        , m_processing{ start }
        , m_record_capacity{ record_capacity }
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
        auto lock   = std::shared_lock{ m_mutex };
        for (const auto& [id, records] : m_records) {
            report.emplace(id, records.stat());
        }
        return report;
    }

    Ascopet::Report Ascopet::report_consume(bool remove_entries)
    {
        auto report = Report{};
        auto lock   = std::shared_lock{ m_mutex };
        for (auto& [id, records] : m_records) {
            report.emplace(id, records.stat());
            records.clear(remove_entries);
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

    std::size_t Ascopet::record_capacity() const
    {
        auto lock = std::shared_lock{ m_mutex };
        return m_record_capacity;
    }

    void Ascopet::resize_record_capacity(std::size_t capacity)
    {
        auto lock         = std::unique_lock{ m_mutex };
        m_record_capacity = capacity;
        for (auto& [id, records] : m_records) {
            records.resize(capacity);
        }
    }

    ascopet::Duration Ascopet::process_interval() const
    {
        auto lock = std::shared_lock{ m_mutex };
        return m_process_interval;
    }

    void Ascopet::set_process_interval(Duration interval)
    {
        m_process_interval = interval;
    }

    void Ascopet::worker(std::stop_token st)
    {
        m_processing.wait(false);

        while (not st.stop_requested()) {
            auto time = [this] {
                auto lock = std::unique_lock{ m_mutex };
                m_queue.consume([this](std::thread::id id, std::string_view name, Record&& record) {
                    if (name == "") {
                        return;
                    }
                    auto it = m_records.find(id);
                    if (it != m_records.end()) {
                        it->second.push(name, std::move(record));
                    } else {
                        auto [it, _] = m_records.emplace(id, m_record_capacity);
                        it->second.push(name, std::move(record));
                    }
                });
                return m_process_interval;
            }();

            std::this_thread::sleep_for(time);
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
