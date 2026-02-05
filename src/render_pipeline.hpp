#pragma once

#include <vector>

#include "mesh_renderer.hpp"

class RenderPipeline
{
public:
    struct Task
    {
        MeshRenderer* renderer{nullptr};
        SRL::Math::Types::Vector3D position;
        SRL::Math::Types::Angle yaw;
        bool logStats{false};
    };

    void Reset()
    {
        tasks_.clear();
    }

    void Enqueue(MeshRenderer& renderer,
                 const SRL::Math::Types::Vector3D& position,
                 const SRL::Math::Types::Angle& yaw,
                 bool logStats = false)
    {
        tasks_.push_back(Task{&renderer, position, yaw, logStats});
    }

    void Flush()
    {
        for (auto& task : tasks_)
        {
            if (!task.renderer) continue;
            task.renderer->Render(task.position, task.yaw, task.logStats);
        }
    }

    const std::vector<Task>& Tasks() const { return tasks_; }

private:
    std::vector<Task> tasks_;
};
