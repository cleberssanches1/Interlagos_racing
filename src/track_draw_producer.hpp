#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <srl_slave.hpp>

#include "track_draw_list.hpp"

// Runtime counters to inspect producer behavior across frames.
struct TrackDrawProducerStats
{
    uint32_t jobsSubmitted = 0;
    uint32_t jobsCompleted = 0;
    uint32_t reusedPreviousList = 0;
    uint32_t synchronousBuilds = 0;
    uint32_t timeoutFallbacks = 0;
    uint32_t lastLatencyFrames = 0;
    uint32_t maxLatencyFrames = 0;
    uint32_t consecutiveTimeouts = 0;
    uint32_t slaveReenabledCount = 0;
    uint32_t safeModeTriggers = 0;
    uint32_t safeModeFrames = 0;
    bool jobInFlight = false;
    bool slaveDisabledByTimeout = false;
    bool safeModeActive = false;
};

template <typename Handle, size_t Capacity>
class ITrackDrawProducer
{
public:
    virtual ~ITrackDrawProducer() = default;
    // Pass frame context before building to compute producer latency.
    virtual void BeginFrame(uint32_t frameId) = 0;
    // Build the next draw list from the ordered handles.
    virtual void Build(const std::vector<Handle>& orderedHandles, size_t limit) = 0;
    // Consume the latest stable draw list.
    virtual const TrackDrawList<Handle, Capacity>& Consume() const = 0;
    // Expose producer counters for frame telemetry.
    virtual TrackDrawProducerStats Stats() const = 0;
};

template <typename Handle, size_t Capacity>
class DoubleBufferedTrackDrawProducer final : public ITrackDrawProducer<Handle, Capacity>
{
public:
    // Capture current frame context for telemetry consistency.
    void BeginFrame(uint32_t frameId) override
    {
        currentFrameId_ = frameId;
    }

    // Build and publish the list in the same frame.
    void Build(const std::vector<Handle>& orderedHandles, size_t limit) override
    {
        lists_.BuildWriteList(orderedHandles, limit);
        lists_.Publish();
        ++stats_.synchronousBuilds;
        ++stats_.jobsSubmitted;
        ++stats_.jobsCompleted;
        stats_.jobInFlight = false;
        stats_.lastLatencyFrames = 0;
    }

    // Return the current read list.
    const TrackDrawList<Handle, Capacity>& Consume() const override
    {
        return lists_.ReadList();
    }

    // Return immutable producer stats.
    TrackDrawProducerStats Stats() const override
    {
        return stats_;
    }

private:
    TrackDrawListAB<Handle, Capacity> lists_{};
    TrackDrawProducerStats stats_{};
    uint32_t currentFrameId_ = 0;
};

template <typename Handle, size_t Capacity>
class SlaveTrackDrawProducer final : public ITrackDrawProducer<Handle, Capacity>
{
public:
    SlaveTrackDrawProducer() = default;

    // Capture frame context used for timeout and latency accounting.
    void BeginFrame(uint32_t frameId) override
    {
        currentFrameId_ = frameId;
        this->UpdateSafeModeState();
        this->TryRecoverSlave();
    }

    // Build list using slave task when available and keep a safe fallback.
    void Build(const std::vector<Handle>& orderedHandles, size_t limit) override
    {
        FinalizeIfReady();

        if (jobInFlight_)
        {
            const uint32_t inFlightFrames = currentFrameId_ - submittedFrameId_;
            if (inFlightFrames > maxFramesInFlight_)
            {
                ++stats_.timeoutFallbacks;
                ++stats_.consecutiveTimeouts;
                stats_.slaveDisabledByTimeout = true;
                disableSlaveWhenIdle_ = true;
                disabledAtFrameId_ = currentFrameId_;
                EnterSafeMode(safeModeCooldownFrames_);
            }
            // Keep last stable list if Slave SH2 is still preparing.
            ++stats_.reusedPreviousList;
            stats_.jobInFlight = true;
            if (safeModeFramesRemaining_ > 0)
            {
                ++stats_.safeModeFrames;
            }
            return;
        }

        BuildJobData jobData{};
        jobData.count = static_cast<uint16_t>(std::min({ limit, orderedHandles.size(), size_t(Capacity) }));
        for (size_t i = 0; i < jobData.count; ++i)
        {
            jobData.items[i] = orderedHandles[i];
        }

        pendingData_ = jobData;
        pendingTarget_ = CacheThroughPtr(&lists_[writeIdx_]);

        // Force synchronous mode while safe mode cooldown is active.
        if (useSlave_ && safeModeFramesRemaining_ == 0)
        {
            // Ensure configured pointers are visible before scheduling Slave SH2.
            CompilerFence();
            task_.Configure(CacheThroughPtr(&pendingData_), pendingTarget_);
            SRL::Slave::ExecuteOnSlave(task_);
            jobInFlight_ = true;
            submittedFrameId_ = currentFrameId_;
            ++stats_.jobsSubmitted;
            stats_.jobInFlight = true;
            return;
        }

        // Synchronous fallback for environments without Slave SH2 scheduling.
        ApplyJob(pendingData_, *pendingTarget_);
        SwapBuffers();
        ++stats_.jobsSubmitted;
        ++stats_.jobsCompleted;
        ++stats_.synchronousBuilds;
        stats_.lastLatencyFrames = 0;
        stats_.consecutiveTimeouts = 0;
        stats_.jobInFlight = false;
        if (safeModeFramesRemaining_ > 0)
        {
            ++stats_.safeModeFrames;
        }
    }

    // Return the latest completed list using cache through alias.
    const TrackDrawList<Handle, Capacity>& Consume() const override
    {
        return *CacheThroughPtr(const_cast<TrackDrawList<Handle, Capacity>*>(&lists_[readIdx_]));
    }

    // Return immutable producer stats.
    TrackDrawProducerStats Stats() const override
    {
        return stats_;
    }

    bool IsJobInFlight() const { return jobInFlight_; }
    void SetUseSlave(bool enabled) { useSlave_ = enabled; }
    void SetMaxFramesInFlight(uint32_t frames) { maxFramesInFlight_ = frames == 0 ? 1 : frames; }
    void SetRecoveryFrames(uint32_t frames) { recoveryFrames_ = frames == 0 ? 1 : frames; }
    void SetSafeModeStallThreshold(uint32_t frames) { safeModeStallThreshold_ = frames == 0 ? 1 : frames; }
    void SetSafeModeCooldownFrames(uint32_t frames) { safeModeCooldownFrames_ = frames == 0 ? 1 : frames; }

private:
    struct BuildJobData
    {
        Handle items[Capacity]{};
        uint16_t count = 0;
    };

    class BuildTask final : public SRL::Types::ITask
    {
    public:
        // Configure input and output pointers before scheduling.
        void Configure(const BuildJobData* data, TrackDrawList<Handle, Capacity>* target)
        {
            data_ = data;
            target_ = target;
        }

    private:
        // Execute list build on Slave SH2.
        void Do() override
        {
            if (!data_ || !target_) return;
            target_->Clear();
            for (uint16_t i = 0; i < data_->count; ++i)
            {
                target_->Push(data_->items[i]);
            }
            CompilerFence();
        }

        const BuildJobData* data_ = nullptr;
        TrackDrawList<Handle, Capacity>* target_ = nullptr;
    };

    static void ApplyJob(const BuildJobData& data, TrackDrawList<Handle, Capacity>& target)
    {
        target.Clear();
        for (uint16_t i = 0; i < data.count; ++i)
        {
            target.Push(data.items[i]);
        }
        CompilerFence();
    }

    void FinalizeIfReady()
    {
        if (!jobInFlight_) return;
        // Important: do not call virtual methods through cache through aliases.
        // Virtual dispatch must use a regular pointer to keep vtable reads stable.
        if (!task_.IsDone()) return;
        jobInFlight_ = false;
        ++stats_.jobsCompleted;
        stats_.lastLatencyFrames = currentFrameId_ - submittedFrameId_;
        if (stats_.lastLatencyFrames > stats_.maxLatencyFrames)
        {
            stats_.maxLatencyFrames = stats_.lastLatencyFrames;
        }
        stats_.consecutiveTimeouts = 0;
        if (disableSlaveWhenIdle_)
        {
            useSlave_ = false;
        }
        disableSlaveWhenIdle_ = false;
        stats_.jobInFlight = false;
        SwapBuffers();
    }

    // Track repeated reuse patterns and enter safe mode before hard timeout.
    void UpdateSafeModeState()
    {
        const uint32_t reusedDelta = stats_.reusedPreviousList - lastReusedPreviousList_;
        lastReusedPreviousList_ = stats_.reusedPreviousList;

        if (jobInFlight_ && reusedDelta > 0)
        {
            ++stallFrames_;
        }
        else
        {
            stallFrames_ = 0;
        }

        if (stallFrames_ >= safeModeStallThreshold_)
        {
            EnterSafeMode(safeModeCooldownFrames_);
            stallFrames_ = 0;
        }

        if (safeModeFramesRemaining_ > 0)
        {
            --safeModeFramesRemaining_;
            stats_.safeModeActive = true;
        }
        else
        {
            stats_.safeModeActive = false;
        }
    }

    // Enter synchronous fallback window to drain instability.
    void EnterSafeMode(uint32_t frames)
    {
        safeModeFramesRemaining_ = frames == 0 ? 1 : frames;
        ++stats_.safeModeTriggers;
        stats_.safeModeActive = true;
        if (jobInFlight_)
        {
            disableSlaveWhenIdle_ = true;
            disabledAtFrameId_ = currentFrameId_;
        }
        else
        {
            useSlave_ = false;
            stats_.slaveDisabledByTimeout = true;
            disabledAtFrameId_ = currentFrameId_;
        }
    }

    // Try to re-enable Slave SH2 after a cooldown window.
    void TryRecoverSlave()
    {
        if (useSlave_) return;
        if (!stats_.slaveDisabledByTimeout) return;
        if (jobInFlight_) return;
        if (safeModeFramesRemaining_ > 0) return;
        const uint32_t elapsed = currentFrameId_ - disabledAtFrameId_;
        if (elapsed < recoveryFrames_) return;
        useSlave_ = true;
        stats_.slaveDisabledByTimeout = false;
        stats_.consecutiveTimeouts = 0;
        ++stats_.slaveReenabledCount;
    }

    // Swap read and write indices only after build is visible.
    void SwapBuffers()
    {
        CompilerFence();
        const uint8_t prevRead = readIdx_;
        readIdx_ = writeIdx_;
        writeIdx_ = prevRead;
        CompilerFence();
    }

    template <typename T>
    static T* CacheThroughPtr(T* ptr)
    {
        constexpr uintptr_t kCacheThroughMask = 0x20000000u;
        return reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(ptr) | kCacheThroughMask);
    }

    // Compiler memory fence for shared producer state.
    static void CompilerFence()
    {
        asm volatile("" ::: "memory");
    }

    TrackDrawList<Handle, Capacity> lists_[2]{};
    uint8_t readIdx_ = 0;
    uint8_t writeIdx_ = 1;
    bool useSlave_ = true;
    bool jobInFlight_ = false;
    bool disableSlaveWhenIdle_ = false;
    TrackDrawProducerStats stats_{};
    uint32_t currentFrameId_ = 0;
    uint32_t submittedFrameId_ = 0;
    uint32_t disabledAtFrameId_ = 0;
    uint32_t maxFramesInFlight_ = 2;
    uint32_t recoveryFrames_ = 120;
    uint32_t safeModeStallThreshold_ = 4;
    uint32_t safeModeCooldownFrames_ = 90;
    uint32_t safeModeFramesRemaining_ = 0;
    uint32_t stallFrames_ = 0;
    uint32_t lastReusedPreviousList_ = 0;
    BuildJobData pendingData_{};
    TrackDrawList<Handle, Capacity>* pendingTarget_ = nullptr;
    BuildTask task_{};
};
