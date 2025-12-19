#pragma once

#include "camera_controller.hpp"

namespace CameraRig
{
    inline void RecalcPosition(Camera::State& cam);

    struct Snapshot
    {
        Camera::State state{};
        bool hasValue{ false };
    };

    inline void Save(const Camera::State& current, Snapshot& snap)
    {
        snap.state = current;
        snap.hasValue = true;
    }

    inline void Restore(Camera::State& current, const Snapshot& snap)
    {
        if (!snap.hasValue) return;
        current = snap.state;
    }

    struct OrbitState
    {
        bool wasHeld{ false };
        Snapshot snap{};
    };

    inline void HandleOrbitAroundCar(Camera::State& cam,
                                     int32_t carYawStepDeg,
                                     bool held, bool leftHeld, bool rightHeld,
                                     int32_t& carYawDeg,
                                     OrbitState& state,
                                     bool invertCamera = true)
    {
        if (held)
        {
            if (!state.wasHeld)
            {
                Save(cam, state.snap);
            }

            int delta = 0;
            if (leftHeld) delta -= carYawStepDeg;
            if (rightHeld) delta += carYawStepDeg;

            if (delta != 0)
            {
                carYawDeg = (carYawDeg + delta + 360) % 360;
                int32_t camDelta = invertCamera ? -delta : delta;
                cam.yawDeg = (cam.yawDeg + camDelta + 360) % 360;
                Camera::RefreshAngles(cam);
                CameraRig::RecalcPosition(cam);
            }
        }
        else if (state.wasHeld && state.snap.hasValue)
        {
            Restore(cam, state.snap);
        }

        state.wasHeld = held;
    }

    inline void SetBehind(Camera::State& cam, int32_t carYawDeg)
    {
        cam.yawDeg = (carYawDeg + 180) % 360;
        cam.viewYawDeg = 0;
        cam.viewPitchDeg = 0;
        Camera::RefreshAngles(cam);
    }

    inline void ApplyDelta(Camera::State& cam, int32_t deltaYaw)
    {
        cam.yawDeg = (cam.yawDeg + deltaYaw + 360) % 360;
        Camera::RefreshAngles(cam);
    }

    inline void RecalcPosition(Camera::State& cam)
    {
        cam.location = Camera::OrbitPosition(cam.yaw, cam.pitch, cam.radius) + cam.strafe;
    }
} // namespace CameraRig
