#pragma once

#include "ascopet/ascopet.hpp"

#include <atomic>
#include <memory>

namespace ascopet
{
    class Ascopet;

    class LocalBuf
    {
    public:
        LocalBuf() = delete;

        LocalBuf(LocalBuf&&)            = delete;
        LocalBuf& operator=(LocalBuf&&) = delete;

        LocalBuf(const LocalBuf&)            = delete;
        LocalBuf& operator=(const LocalBuf&) = delete;

        LocalBuf(Ascopet* ascopet) noexcept
            : m_ascopet{ ascopet }
            , m_buffers{ new RingBuf<NamedRecord>[2]{
                  { m_ascopet->record_capacity() },
                  { m_ascopet->record_capacity() },
              } }
        {
            m_ascopet->add_localbuf(std::this_thread::get_id(), *this);
        }

        ~LocalBuf() { m_ascopet->remove_localbuf(std::this_thread::get_id()); }

        RingBuf<NamedRecord>& swap_buffers() noexcept
        {
            auto front = m_front.fetch_xor(1, Ord::acq_rel) ^ 1;    // emulate xor_fetch
            return m_buffers[front];
        }

        bool add_record(NamedRecord&& record) noexcept
        {
            auto back = m_front.load() ^ 1;    // access the back buffer
            m_buffers[back].push_back(std::move(record));
            return true;
        }

    private:
        using Ord = std::memory_order;

        Ascopet* m_ascopet = nullptr;

        std::unique_ptr<RingBuf<NamedRecord>[]> m_buffers;
        std::atomic<std::uint32_t>              m_front = 0;
        std::atomic<bool>                       m_dirty = false;
    };
}
