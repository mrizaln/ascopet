#pragma once

#include <ascopet/common.hpp>
#include <cassert>
#include <memory>

namespace ascopet
{
    class RecordBuffer
    {
    public:
        static constexpr std::size_t npos = std::numeric_limits<std::size_t>::max();

        RecordBuffer(std::size_t capacity)
            : m_head{ 0 }
            , m_tail{ 0 }
            , m_capacity{ capacity }
            , m_buffer{ std::make_unique<Record[]>(capacity) }
        {
            assert(capacity > 0);
        }

        void push_back(Record&& record)
        {
            assert(capacity() > 0);
            if (m_tail != npos) {
                m_buffer[m_tail] = std::move(record);    // new entry -> construct
                if (increment(m_tail) == m_head) {
                    m_tail = npos;
                }
            } else {
                m_buffer[m_head] = std::move(record);    // already existing entry -> assign
                increment(m_head);
            }
        }

        Record& operator[](std::size_t pos)
        {
            assert(pos < size());
            auto realpos = (m_head + pos) % capacity();
            return m_buffer[realpos];
        }

        const Record& operator[](std::size_t pos) const
        {
            assert(pos < size());
            auto realpos = (m_head + pos) % capacity();
            return m_buffer[realpos];
        }

        std::size_t size() const
        {
            return m_tail == npos ? capacity() : (m_tail + capacity() - m_head) % capacity();
        }

        std::size_t capacity() const { return m_capacity; }

    private:
        std::size_t increment(std::size_t& index)
        {
            if (++index == m_capacity) {
                index = 0;
            }
            return index;
        }

        std::size_t               m_head = 0;
        std::size_t               m_tail = npos;
        std::size_t               m_capacity;
        std::unique_ptr<Record[]> m_buffer;
    };
}
