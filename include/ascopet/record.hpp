#pragma once

#include "ascopet/common.hpp"

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

        RecordBuffer(RecordBuffer&&)            = default;
        RecordBuffer& operator=(RecordBuffer&&) = default;

        RecordBuffer(const RecordBuffer& other)
            : m_head{ 0 }
            , m_tail{ other.m_capacity == other.size() ? npos : other.size() }
            , m_capacity{ other.m_capacity }
            , m_buffer{ std::make_unique<Record[]>(m_capacity) }
        {
            assert(m_capacity > 0);
            for (std::size_t i = 0; i < other.size(); ++i) {
                m_buffer[i] = other[i];    // linearize as it copies
            }
        }

        RecordBuffer& operator=(const RecordBuffer& other) = delete;

        void push_back(Record&& record)
        {
            m_count++;

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

        void resize(std::size_t new_capacity)
        {
            if (new_capacity == m_capacity) {
                return;
            }

            auto new_buffer = std::make_unique<Record[]>(new_capacity);
            auto offset     = new_capacity < size() ? size() - new_capacity : 0;
            auto count      = std::min(new_capacity, size());

            for (auto i = 0u; i < count; ++i) {
                new_buffer[i] = std::move(m_buffer[(m_head + offset + i) % m_capacity]);
            }

            m_buffer   = std::move(new_buffer);
            m_head     = 0;
            m_tail     = count == new_capacity ? npos : count;
            m_capacity = new_capacity;
        }

        std::size_t size() const
        {
            return m_tail == npos ? capacity() : (m_tail + capacity() - m_head) % capacity();
        }

        std::size_t capacity() const { return m_capacity; }

        std::size_t actual_count() const { return m_count; }

        void clear()
        {
            m_head  = 0;
            m_tail  = 0;
            m_count = 0;
        }

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

        std::size_t m_count = 0;
    };
}
