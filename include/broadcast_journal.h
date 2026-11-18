#pragma once
#include <atomic>
#include <array>
#include <vector>
#include <cstddef>
#include <cassert>

// The Zero-Copy Broadcast Architecture (LMAX Disruptor Pattern)
// Templatized on capacity; payload type is determined by the journal's vector element.
// Consumers only READ the published index → MESI cache line stays in Shared (S) state.
//
// Progress reporting: each consumer owns a dedicated cursor (single writer per
// cursor → plain release store, no CAS). The producer computes the minimum
// across cursors only when the ring looks full, and caches the result.
template <size_t Capacity, size_t MaxConsumers = 16>
class BroadcastJournal {
private:
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2");
    static_assert(MaxConsumers > 0, "Need at least one consumer slot");
    static constexpr size_t Mask = Capacity - 1;

    struct alignas(64) Slot {
        alignas(8) char data[64];
    };

    std::vector<Slot> journal_;

    // Single source of truth: the monotonically increasing sequence number.
    // alignas(64) isolates on its own cache line to prevent false sharing.
    alignas(64) std::atomic<size_t> published_index_{0};

    // One cursor per consumer, each on its own cache line. Only consumer i
    // writes cursors_[i] — single writer, so no CAS is ever needed.
    struct alignas(64) Cursor {
        std::atomic<size_t> seq{0};
    };
    std::array<Cursor, MaxConsumers> cursors_;

    alignas(64) std::atomic<size_t> num_consumers_{0};

    // Producer-private cache of the slowest consumer position. Rescanning all
    // cursors on every publish would cost N atomic loads; instead the producer
    // only rescans when the cached value makes the ring look full.
    alignas(64) size_t cached_min_seq_ = 0;

public:
    static constexpr size_t capacity = Capacity;

    BroadcastJournal() : journal_(Capacity) {}

    // Call once before publishing begins. Consumer i (0 <= i < n) reports
    // progress through its own cursor.
    void set_consumer_count(size_t n) {
        assert(n <= MaxConsumers);
        num_consumers_.store(n, std::memory_order_release);
    }

    // PRODUCER: true if writing `sequence` cannot lap the slowest consumer.
    inline bool can_publish(size_t sequence) {
        if (sequence - cached_min_seq_ < Capacity) return true;
        cached_min_seq_ = slowest_consumer_seq(sequence);
        return sequence - cached_min_seq_ < Capacity;
    }

    // PRODUCER: Write payload then release-fence the index.
    // Returns false if publishing would lap the slowest consumer (backpressure).
    template <typename T>
    inline bool try_publish(size_t sequence, const T& item) {
        static_assert(sizeof(T) <= sizeof(Slot), "Payload exceeds slot size");

        if (!can_publish(sequence)) {
            return false; // would overwrite unread slot
        }

        __builtin_memcpy(&journal_[sequence & Mask], &item, sizeof(T));
        published_index_.store(sequence + 1, std::memory_order_release);
        return true;
    }

    // PRODUCER: Publish unconditionally (use only when consumers are guaranteed to keep up)
    template <typename T>
    inline void publish(size_t sequence, const T& item) {
        static_assert(sizeof(T) <= sizeof(Slot), "Payload exceeds slot size");
        __builtin_memcpy(&journal_[sequence & Mask], &item, sizeof(T));
        published_index_.store(sequence + 1, std::memory_order_release);
    }

    // PRODUCER: Write payload WITHOUT updating the published index.
    // Use with publish_fence() for batched publishing.
    template <typename T>
    inline void publish_payload(size_t sequence, const T& item) {
        static_assert(sizeof(T) <= sizeof(Slot), "Payload exceeds slot size");
        __builtin_memcpy(&journal_[sequence & Mask], &item, sizeof(T));
    }

    // PRODUCER: Make all payloads up to `sequence` visible to consumers.
    // Call once after writing a batch of payloads. One invalidation per batch
    // instead of one per message — reduces MESI fan-out by batch_size×.
    inline void publish_fence(size_t sequence) {
        published_index_.store(sequence, std::memory_order_release);
    }

    // CONSUMER: Acquire-load the published index (read-only, MESI Shared state)
    inline size_t get_published_index() const {
        return published_index_.load(std::memory_order_acquire);
    }

    // CONSUMER: Report progress on this consumer's own cursor. Single writer
    // per cursor — a plain release store suffices, no CAS.
    inline void report_consumer_progress(size_t consumer_id, size_t consumer_seq) {
        assert(consumer_id < MaxConsumers);
        cursors_[consumer_id].seq.store(consumer_seq, std::memory_order_release);
    }

    // CONSUMER: O(1) zero-copy payload retrieval
    template <typename T>
    inline const T& get_payload(size_t sequence) const {
        return *reinterpret_cast<const T*>(&journal_[sequence & Mask]);
    }

    // Minimum position across all registered consumers; `fallback` when none
    // are registered (no gating constraint).
    inline size_t slowest_consumer_seq(size_t fallback) const {
        size_t n = num_consumers_.load(std::memory_order_acquire);
        if (n == 0) return fallback;
        size_t min_seq = ~size_t{0};
        for (size_t i = 0; i < n; i++) {
            size_t s = cursors_[i].seq.load(std::memory_order_acquire);
            if (s < min_seq) min_seq = s;
        }
        return min_seq;
    }

    // Check how far ahead producer is of slowest consumer
    inline size_t backlog() const {
        size_t pub = published_index_.load(std::memory_order_acquire);
        return pub - slowest_consumer_seq(pub);
    }
};
