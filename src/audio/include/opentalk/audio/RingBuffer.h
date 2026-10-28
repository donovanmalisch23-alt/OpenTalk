// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QtGlobal>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <vector>

namespace opentalk::audio {

/// Lock-free single-producer, single-consumer ring of float samples.
///
/// This is the only thing the real-time audio callbacks touch: no locks, no allocation
/// and no system calls happen on the audio thread. One thread may write and one other
/// thread may read at the same time.
class RingBuffer {
public:
    explicit RingBuffer(std::size_t capacity)
        : m_buffer(capacity + 1)
    {
    }

    std::size_t capacity() const { return m_buffer.size() - 1; }

    std::size_t available() const
    {
        const std::size_t w = m_write.load(std::memory_order_acquire);
        const std::size_t r = m_read.load(std::memory_order_acquire);
        return w >= r ? w - r : m_buffer.size() - r + w;
    }

    std::size_t freeSpace() const { return capacity() - available(); }

    /// Writes up to `count` samples; returns how many fitted.
    std::size_t write(const float *data, std::size_t count)
    {
        const std::size_t size = m_buffer.size();
        const std::size_t w = m_write.load(std::memory_order_relaxed);
        const std::size_t r = m_read.load(std::memory_order_acquire);
        const std::size_t free = (r > w ? r - w : size - w + r) - 1;
        const std::size_t n = std::min(count, free);
        const std::size_t first = std::min(n, size - w);
        std::copy(data, data + first, m_buffer.begin() + static_cast<std::ptrdiff_t>(w));
        std::copy(data + first, data + n, m_buffer.begin());
        m_write.store((w + n) % size, std::memory_order_release);
        return n;
    }

    /// Reads up to `count` samples; returns how many were available.
    std::size_t read(float *out, std::size_t count)
    {
        const std::size_t size = m_buffer.size();
        const std::size_t r = m_read.load(std::memory_order_relaxed);
        const std::size_t w = m_write.load(std::memory_order_acquire);
        const std::size_t avail = w >= r ? w - r : size - r + w;
        const std::size_t n = std::min(count, avail);
        const std::size_t first = std::min(n, size - r);
        std::copy(m_buffer.begin() + static_cast<std::ptrdiff_t>(r),
                  m_buffer.begin() + static_cast<std::ptrdiff_t>(r + first), out);
        std::copy(m_buffer.begin(), m_buffer.begin() + static_cast<std::ptrdiff_t>(n - first), out + first);
        m_read.store((r + n) % size, std::memory_order_release);
        return n;
    }

    /// Drops everything. Only safe when no other thread is using the buffer.
    void clear()
    {
        m_read.store(0);
        m_write.store(0);
    }

private:
    std::vector<float> m_buffer;
    std::atomic<std::size_t> m_read{0};
    std::atomic<std::size_t> m_write{0};
};

} // namespace opentalk::audio
