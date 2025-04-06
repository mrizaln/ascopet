#pragma once

#include "ascopet/common.hpp"

#include <atomic>
#include <concepts>
#include <thread>
#include <utility>

namespace ascopet
{
    /**
     * @class Queue
     * @brief Unbuffered lock-free queue for storing records.
     */
    class Queue
    {
    public:
        struct Node
        {
            std::atomic<Node*> m_next = nullptr;
            std::string        m_name;
            std::thread::id    m_thread_id;
            Record             m_record;
        };

        using Ord = std::memory_order;

        Queue(std::size_t preallocate)
        {
            auto* freestore = new Node{};
            auto* node      = freestore;

            while (preallocate-- > 0) {
                auto* next   = new Node{};
                node->m_next = next;
                node         = next;
            }
            m_freestore = freestore;

            auto* dummy = new_node();
            m_head      = dummy;
            m_tail.store(dummy, Ord::relaxed);
        }

        ~Queue()
        {
            while (m_head != nullptr) {
                auto* next = m_head->m_next.load(Ord::relaxed);
                delete_node(m_head);
                m_head = next;
            }

            auto* freestore = m_freestore.load(Ord::relaxed);
            while (freestore) {
                auto* next = freestore->m_next.load(Ord::relaxed);
                delete freestore;
                freestore = next;
            }
        }

        Queue(Queue&&)            = delete;
        Queue& operator=(Queue&&) = delete;

        Queue(const Queue&)            = delete;
        Queue& operator=(const Queue&) = delete;

        void push(std::string_view name, Timepoint start)
        {
            auto* node = new_node();

            node->m_thread_id = std::this_thread::get_id();
            node->m_name      = name;
            node->m_record    = { .m_duration = Clock::now() - start, .m_start = start };

            auto* prev = m_tail.exchange(node, Ord::acq_rel);
            prev->m_next.store(node, Ord::release);
        }

        void consume(std::invocable<std::thread::id, std::string_view, Record&&> auto&& fn)
        {
            auto* node = m_head;
            auto* next = node->m_next.load(Ord::acquire);

            while (next != nullptr) {
                fn(node->m_thread_id, node->m_name, std::move(node->m_record));
                delete_node(node);
                node = next;
                next = node->m_next.load(Ord::acquire);
            }

            m_head = node;
        }

    private:
        Node* new_node()
        {
            auto* node = m_freestore.load(Ord::acquire);
            while (node != nullptr) {
                auto* next = node->m_next.load(Ord::acquire);
                if (m_freestore.compare_exchange_weak(node, next, Ord::acq_rel)) {
                    node->m_next = nullptr;
                    return node;
                }
            }
            return new Node{};
        }

        void delete_node(Node* node)
        {
            node->m_next   = nullptr;
            auto* old_head = m_freestore.load(Ord::relaxed);
            do {
                node->m_next = old_head;
            } while (not m_freestore.compare_exchange_weak(old_head, node, Ord::acq_rel));
        }

        Node*              m_head      = nullptr;
        std::atomic<Node*> m_tail      = nullptr;
        std::atomic<Node*> m_freestore = nullptr;
    };
}
