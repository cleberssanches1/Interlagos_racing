#pragma once

#include "interfaces.hpp"

#include <memory>
#include <vector>

class FocusSelector
{
public:
    void Reset()
    {
        targets_.clear();
        currentIndex_ = 0;
    }

    void AddTarget(std::shared_ptr<Game::ICameraTarget> target)
    {
        if (!target) return;
        targets_.push_back(target);
    }

    void EnsureValid()
    {
        if (targets_.empty()) { currentIndex_ = 0; return; }
        if (currentIndex_ >= targets_.size()) currentIndex_ = 0;
        for (size_t offset = 0; offset < targets_.size(); ++offset)
        {
            size_t idx = (currentIndex_ + offset) % targets_.size();
            if (targets_[idx]->Valid())
            {
                currentIndex_ = idx;
                return;
            }
        }
        currentIndex_ = 0;
    }

    bool NextValid()
    {
        if (targets_.empty()) return false;
        EnsureValid();
        if (!targets_[currentIndex_]->Valid()) return false;

        size_t count = targets_.size();
        for (size_t offset = 1; offset <= count; ++offset)
        {
            size_t idx = (currentIndex_ + offset) % count;
            if (targets_[idx]->Valid())
            {
                currentIndex_ = idx;
                return true;
            }
        }
        return false;
    }

    Vector3D CurrentPosition() const
    {
        auto* target = CurrentTarget();
        return target ? target->TargetPosition() : Vector3D(Fxp::Convert(0), Fxp::Convert(0), Fxp::Convert(0));
    }

    const char* CurrentName() const
    {
        auto* target = CurrentTarget();
        return target ? target->Name() : "none";
    }

    Game::ICameraTarget* CurrentTarget() const
    {
        if (targets_.empty()) return nullptr;
        return targets_[currentIndex_].get();
    }

    bool HasTargets() const { return !targets_.empty(); }
    size_t TargetCount() const { return targets_.size(); }

private:
    std::vector<std::shared_ptr<Game::ICameraTarget>> targets_;
    size_t currentIndex_{0};
};
