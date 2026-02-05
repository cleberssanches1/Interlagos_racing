#pragma once

#include "mesh_renderer.hpp"

class IRenderInstance
{
public:
    virtual ~IRenderInstance() = default;
    virtual MeshRenderer* Renderer() = 0;
    virtual SRL::Math::Types::Vector3D Position() const = 0;
    virtual SRL::Math::Types::Angle Yaw() const = 0;
    virtual const char* Name() const = 0;
};
