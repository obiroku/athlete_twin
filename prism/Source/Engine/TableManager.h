#pragma once
// Background wavetable generation with lock-free hand-off to the audio thread.
// Tables are swapped via atomic pointers; retired tables go to a garbage FIFO
// that a non-audio thread (the message thread timer, or tests) frees.
#include "../DSP/Wavetable.h"
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace prism
{
struct TableSource
{
    std::string factory;                          // factory table name, or empty
    std::string name;                             // display name for embedded tables
    std::vector<std::vector<float>> frames;       // embedded frames (already hygienic or raw)
    bool operator== (const TableSource& o) const { return factory == o.factory && name == o.name && frames == o.frames; }
};

class TableManager
{
public:
    TableManager();
    ~TableManager();

    // ---- message thread
    void request (int osc, const TableSource& src);
    TableSource currentSource (int osc) const;
    // level-0 frames of the most recently generated table for display / export
    std::shared_ptr<const std::vector<std::vector<float>>> displayFrames (int osc) const;
    void collectGarbage();              // frees retired tables (any non-audio thread)
    bool isReadyForAudio (int osc) const noexcept { return applied[osc].load() == requested[osc].load(); }
    // Blocks until the worker has published the latest request (tests / offline)
    bool waitUntilGenerated (int osc, int timeoutMs) const;

    // ---- audio thread
    // Picks up any freshly generated table; returns the table to use (may be null -> sine fallback)
    const dsp::Wavetable* acquire (int osc) noexcept;
    bool isPublished (int osc) const noexcept { return published[osc].load() == requested[osc].load(); }

private:
    struct Box
    {
        std::unique_ptr<dsp::Wavetable> table;
        int id = 0;
    };

    std::atomic<Box*> pending[2] { nullptr, nullptr };
    Box* current[2] { nullptr, nullptr };
    std::atomic<int> requested[2] { 0, 0 }, published[2] { 0, 0 }, applied[2] { 0, 0 };

    // retire FIFO (audio -> collector)
    static constexpr int RetireCap = 64;
    std::atomic<Box*> retire[RetireCap] {};
    std::atomic<int> retireW { 0 }, retireR { 0 };
    Box* stash[2] { nullptr, nullptr };
    std::mutex collectLock;

    // worker
    mutable std::mutex lock;
    std::condition_variable cv;
    TableSource sources[2];
    int jobId[2] { 0, 0 };
    bool jobPending[2] { false, false };
    std::shared_ptr<const std::vector<std::vector<float>>> display[2];
    bool quit = false;
    std::thread worker;
    void run();
};
} // namespace prism
