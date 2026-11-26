// shared_cockpit - a background thread that can be asked to stop
//
// std::thread + std::atomic only (Apple's libc++ long marked jthread experimental).
//   - The thread function gets a StopToken and checks token.StopRequested_cai().
//   - RequestStop_cai() sets the flag; the destructor and assigning a new thread
//     request stop and wait for the old thread first.
//   - A default StopToken never reports a stop (for calls on the main thread).
// Taken from vrconfig_helper.

#pragma once

#include <atomic>
#include <memory>
#include <thread>
#include <utility>

namespace sc {

class StopToken {
public:
    StopToken() = default;
    explicit StopToken(std::shared_ptr<const std::atomic<bool>> flag) : mFlag(std::move(flag)) {}

    // True once the owner asked the thread to stop.
    bool StopRequested_cai() const noexcept { return mFlag && mFlag->load(std::memory_order_relaxed); }

private:
    std::shared_ptr<const std::atomic<bool>> mFlag;
};

class StopThread {
public:
    StopThread() = default;

    template <typename Function>
    explicit StopThread(Function&& function)
        : mFlag(std::make_shared<std::atomic<bool>>(false))
    {
        auto lmbda_run = [work = std::forward<Function>(function), token = StopToken(mFlag)]() mutable { work(token); };
        mThread = std::thread(std::move(lmbda_run));
    }

    ~StopThread() { StopAndJoin_cai(); }

    StopThread(const StopThread&)            = delete;
    StopThread& operator=(const StopThread&) = delete;
    StopThread(StopThread&&) noexcept        = default;

    StopThread& operator=(StopThread&& other) noexcept
    {
        if (this != &other) {
            StopAndJoin_cai();
            mThread = std::move(other.mThread);
            mFlag   = std::move(other.mFlag);
        }
        return *this;
    }

    // True while a thread is attached (running or finished but not joined).
    bool Joinable_cai() const noexcept { return mThread.joinable(); }
    // Waits for the thread to end.
    void Join_cai() { mThread.join(); }
    // Asks the thread to stop (it checks its StopToken).
    void RequestStop_cai() noexcept
    {
        if (mFlag)
            mFlag->store(true, std::memory_order_relaxed);
    }

private:
    // Asks the thread to stop and waits for it.
    void StopAndJoin_cai()
    {
        RequestStop_cai();
        if (mThread.joinable())
            mThread.join();
    }

    std::thread                        mThread;
    std::shared_ptr<std::atomic<bool>> mFlag;
};

} // namespace sc
