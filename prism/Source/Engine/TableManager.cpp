#include "TableManager.h"
#include "../DSP/FactoryTables.h"
#include <chrono>

namespace prism
{
TableManager::TableManager()
{
    worker = std::thread ([this] { run(); });
}

TableManager::~TableManager()
{
    {
        std::lock_guard<std::mutex> g (lock);
        quit = true;
    }
    cv.notify_all();
    if (worker.joinable()) worker.join();
    collectGarbage();
    for (int o = 0; o < 2; ++o)
    {
        delete pending[o].exchange (nullptr);
        delete current[o];
        delete stash[o];
    }
}

void TableManager::request (int osc, const TableSource& src)
{
    {
        std::lock_guard<std::mutex> g (lock);
        if (src == sources[osc] && requested[osc].load() > 0) return;
        sources[osc] = src;
        jobId[osc] = requested[osc].fetch_add (1) + 1;
        jobPending[osc] = true;
    }
    cv.notify_all();
}

TableSource TableManager::currentSource (int osc) const
{
    std::lock_guard<std::mutex> g (lock);
    return sources[osc];
}

std::shared_ptr<const std::vector<std::vector<float>>> TableManager::displayFrames (int osc) const
{
    std::lock_guard<std::mutex> g (lock);
    return display[osc];
}

bool TableManager::waitUntilGenerated (int osc, int timeoutMs) const
{
    for (int i = 0; i < timeoutMs; ++i)
    {
        if (published[osc].load() == requested[osc].load()) return true;
        std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }
    return published[osc].load() == requested[osc].load();
}

void TableManager::run()
{
    for (;;)
    {
        int osc = -1, id = 0;
        TableSource src;
        {
            std::unique_lock<std::mutex> g (lock);
            cv.wait (g, [this] { return quit || jobPending[0] || jobPending[1]; });
            if (quit) return;
            osc = jobPending[0] ? 0 : 1;
            src = sources[osc];
            id = jobId[osc];
            jobPending[osc] = false;
        }
        collectGarbage();

        std::unique_ptr<dsp::Wavetable> t;
        if (! src.factory.empty())
        {
            const int idx = dsp::factoryTableIndex (src.factory);
            t = dsp::Wavetable::fromFrames (dsp::generateFactoryFrames (idx < 0 ? 0 : idx), idx < 0 ? dsp::factoryTableNames()[0] : src.factory);
        }
        else
            t = dsp::Wavetable::fromFrames (src.frames, src.name.empty() ? std::string ("User") : src.name);

        auto disp = std::make_shared<const std::vector<std::vector<float>>> (t->levelZeroFrames());
        {
            std::lock_guard<std::mutex> g (lock);
            if (jobPending[osc] && jobId[osc] != id) continue; // superseded while generating
            display[osc] = disp;
        }
        auto* box = new Box();
        box->table = std::move (t);
        box->id = id;
        delete pending[osc].exchange (box); // an unconsumed older table was never seen by audio
        published[osc].store (id);
    }
}

const dsp::Wavetable* TableManager::acquire (int osc) noexcept
{
    auto pushRetire = [this] (Box* b) {
        const int w = retireW.load (std::memory_order_relaxed);
        const int next = (w + 1) % RetireCap;
        if (next == retireR.load (std::memory_order_acquire)) return false;
        retire[w].store (b, std::memory_order_relaxed);
        retireW.store (next, std::memory_order_release);
        return true;
    };
    if (stash[osc] != nullptr && pushRetire (stash[osc])) stash[osc] = nullptr;

    if (Box* p = pending[osc].exchange (nullptr))
    {
        if (current[osc] != nullptr)
        {
            if (! pushRetire (current[osc]))
            {
                if (stash[osc] == nullptr) stash[osc] = current[osc];
                // else: retire queue and stash both full (collector stalled) - the old
                // table is intentionally leaked rather than freed on the audio thread.
            }
        }
        current[osc] = p;
        applied[osc].store (p->id);
    }
    return current[osc] != nullptr ? current[osc]->table.get() : nullptr;
}

void TableManager::collectGarbage()
{
    std::lock_guard<std::mutex> g (collectLock);
    for (;;)
    {
        const int r = retireR.load (std::memory_order_relaxed);
        if (r == retireW.load (std::memory_order_acquire)) break;
        Box* b = retire[r].load (std::memory_order_relaxed);
        retireR.store ((r + 1) % RetireCap, std::memory_order_release);
        delete b;
    }
}
} // namespace prism
