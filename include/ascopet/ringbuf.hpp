#pragma once

#include <cassert>
#include <concepts>
#include <memory>
#include <type_traits>

namespace ascopet
{
    template <typename T>
        requires std::default_initializable<T>                //
             and std::is_trivially_move_constructible_v<T>    //
             and std::is_trivially_destructible_v<T>
    class RingBuf
    {
    public:
        static constexpr std::size_t npos = std::numeric_limits<std::size_t>::max();

        RingBuf(std::size_t capacity)
            : m_head{ 0 }
            , m_tail{ 0 }
            , m_capacity{ capacity }
            , m_buffer{ std::make_unique<T[]>(capacity) }
        {
            assert(capacity > 0);
        }

        RingBuf(RingBuf&&)            = default;
        RingBuf& operator=(RingBuf&&) = default;

        RingBuf(const RingBuf& other)
            : m_head{ 0 }
            , m_tail{ other.m_capacity == other.size() ? npos : other.size() }
            , m_capacity{ other.m_capacity }
            , m_buffer{ std::make_unique<T[]>(m_capacity) }
        {
            assert(m_capacity > 0);
            for (std::size_t i = 0; i < other.size(); ++i) {
                m_buffer[i] = other[i];    // linearize as it copies
            }
        }

        RingBuf& operator=(const RingBuf& other) = delete;

        void push_back(T&& record)
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

        T& operator[](std::size_t pos)
        {
            assert(pos < size());
            auto realpos = (m_head + pos) % capacity();
            return m_buffer[realpos];
        }

        const T& operator[](std::size_t pos) const
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

            auto new_buffer = std::make_unique<T[]>(new_capacity);
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

        std::size_t          m_head = 0;
        std::size_t          m_tail = npos;
        std::size_t          m_capacity;
        std::unique_ptr<T[]> m_buffer;

        std::size_t m_count = 0;
    };
}
