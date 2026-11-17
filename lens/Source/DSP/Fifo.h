#pragma once

#include <atomic>
#include <vector>

// Lock-free single-producer / single-consumer FIFOs. The producer never
// waits: if there is no room, the data is dropped.
namespace lens::dsp
{
template <typename T>
class SpscQueue
{
public:
    explicit SpscQueue (int capacityPowerOfTwo = 1024) { resize (capacityPowerOfTwo); }

    // Not thread-safe: call before either side starts.
    void resize (int capacityPowerOfTwo)
    {
        int c = 1;
        while (c < capacityPowerOfTwo)
            c <<= 1;
        buffer.assign ((size_t) c, T {});
        mask = c - 1;
        readPos.store (0);
        writePos.store (0);
    }

    // Producer. Returns how many items were written.
    int push (const T* items, int n) noexcept
    {
        const int w = writePos.load (std::memory_order_relaxed);
        const int r = readPos.load (std::memory_order_acquire);
        const int free = (int) buffer.size() - (w - r);
        const int count = n < free ? n : free;
        for (int i = 0; i < count; ++i)
            buffer[(size_t) ((w + i) & mask)] = items[i];
        writePos.store (w + count, std::memory_order_release);
        droppedCount.fetch_add (n - count, std::memory_order_relaxed);
        return count;
    }

    bool push (const T& item) noexcept { return push (&item, 1) == 1; }

    // Consumer. Returns how many items were read.
    int pop (T* items, int n) noexcept
    {
        const int r = readPos.load (std::memory_order_relaxed);
        const int w = writePos.load (std::memory_order_acquire);
        const int avail = w - r;
        const int count = n < avail ? n : avail;
        for (int i = 0; i < count; ++i)
            items[i] = buffer[(size_t) ((r + i) & mask)];
        readPos.store (r + count, std::memory_order_release);
        return count;
    }

    bool pop (T& item) noexcept { return pop (&item, 1) == 1; }

    int available() const noexcept
    {
        return writePos.load (std::memory_order_acquire) - readPos.load (std::memory_order_acquire);
    }
    int capacity() const noexcept { return (int) buffer.size(); }
    long long dropped() const noexcept { return droppedCount.load(); }

private:
    std::vector<T> buffer;
    int mask = 0;
    std::atomic<int> readPos { 0 }, writePos { 0 };
    std::atomic<long long> droppedCount { 0 };
};

// Fixed delay line, any delay up to the prepared maximum.
class DelayLine
{
public:
    void prepare (int maxDelay)
    {
        int c = 1;
        while (c < maxDelay + 1)
            c <<= 1;
        buffer.assign ((size_t) c, 0.0f);
        mask = c - 1;
        pos = 0;
    }
    void clear() noexcept { std::fill (buffer.begin(), buffer.end(), 0.0f); }
    int maxDelay() const noexcept { return mask; }

    float process (float x, int delay) noexcept
    {
        buffer[(size_t) pos] = x;
        const float y = buffer[(size_t) ((pos - delay) & mask)];
        pos = (pos + 1) & mask;
        return y;
    }

private:
    std::vector<float> buffer { 0.0f };
    int mask = 0;
    int pos = 0;
};

// Same, double precision.
class DelayLineD
{
public:
    void prepare (int maxDelay)
    {
        int c = 1;
        while (c < maxDelay + 1)
            c <<= 1;
        buffer.assign ((size_t) c, 0.0);
        mask = c - 1;
        pos = 0;
    }
    void clear() noexcept { std::fill (buffer.begin(), buffer.end(), 0.0); }

    double process (double x, int delay) noexcept
    {
        buffer[(size_t) pos] = x;
        const double y = buffer[(size_t) ((pos - delay) & mask)];
        pos = (pos + 1) & mask;
        return y;
    }

private:
    std::vector<double> buffer { 0.0 };
    int mask = 0;
    int pos = 0;
};
} // namespace lens::dsp
