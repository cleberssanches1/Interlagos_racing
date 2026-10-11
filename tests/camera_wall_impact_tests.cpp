#include <cstdint>
#include <iostream>

#include "camera_wall_impact.hpp"

namespace
{
int failures = 0;
#define CHECK(expr) do { if (!(expr)) { \
    std::cerr << __LINE__ << ": failed: " #expr "\n"; ++failures; } } while (0)

constexpr int32_t U(int32_t units) { return units * 65536; }

void TestInertiaOnlyOnImpactAndRecovers()
{
    CameraWallImpact::State state{};
    CameraWallImpact::Observe(state, true, 5, 0, U(100), 0);
    CHECK(state.responseFrames == 12);
    CHECK(CameraWallImpact::PositionBlendRaw(state, 60000) < 20000);
    CameraWallImpact::Advance(state);
    CameraWallImpact::Observe(state, true, 5, 0, U(100), 0);
    CHECK(state.responseFrames == 11); // sustained contact does not freeze camera
    for (int i = 0; i < 11; ++i) CameraWallImpact::Advance(state);
    CHECK(CameraWallImpact::PositionBlendRaw(state, 60000) == 60000);
    CameraWallImpact::Observe(state, false, 0, 0, U(100), 0);
    CameraWallImpact::Observe(state, true, 5, 0, U(100), 0);
    CHECK(state.responseFrames == 12);
}

void TestWallSlideAndUnchangedSafeCamera()
{
    CameraWallImpact::State state{};
    CameraWallImpact::Observe(state, true, 5, 0, U(100), 0);
    int32_t x = U(60);
    int32_t z = U(-240);
    CameraWallImpact::KeepCameraOnTrackSide(state, U(100), 0, x, z);
    CHECK(x == U(94)); // wall plane at X=92, two-unit inside margin
    CHECK(z == U(-240)); // tangent travel is retained
    x = U(150);
    z = U(-240);
    CameraWallImpact::KeepCameraOnTrackSide(state, U(100), 0, x, z);
    CHECK(x == U(150));
    CHECK(z == U(-240));
}

void TestOppositeAndDiagonalNormals()
{
    CameraWallImpact::State state{};
    CameraWallImpact::Observe(state, true, -6, 0, U(100), 0);
    int32_t x = U(150);
    int32_t z = 0;
    CameraWallImpact::KeepCameraOnTrackSide(state, U(100), 0, x, z);
    CHECK(x == U(106));
    CameraWallImpact::Reset(state);
    CameraWallImpact::Observe(state, true, 5, 5, U(100), U(100));
    x = U(60);
    z = U(60);
    CameraWallImpact::KeepCameraOnTrackSide(state, U(100), U(100), x, z);
    CHECK(x == U(93));
    CHECK(z == U(93));
}

void TestMissingPushDoesNotInventWall()
{
    CameraWallImpact::State state{};
    CameraWallImpact::Observe(state, true, 0, 0, U(100), 0);
    CHECK(state.responseFrames == 12);
    CHECK(state.guardFrames == 0);
    int32_t x = U(60);
    int32_t z = 0;
    CameraWallImpact::KeepCameraOnTrackSide(state, U(100), 0, x, z);
    CHECK(x == U(60));
}

void TestGuardExpiresAndResetClears()
{
    CameraWallImpact::State state{};
    CameraWallImpact::Observe(state, true, 5, 0, U(100), 0);
    CameraWallImpact::Observe(state, false, 0, 0, U(100), 0);
    for (int i = 0; i < 30; ++i)
    {
        int32_t x = U(60);
        int32_t z = 0;
        CameraWallImpact::KeepCameraOnTrackSide(state, U(100), 0, x, z);
        CHECK(x == U(94)); // never releases while camera would cross wall
    }
    for (int i = 0; i < 18; ++i)
    {
        int32_t x = U(150);
        int32_t z = 0;
        CameraWallImpact::KeepCameraOnTrackSide(state, U(100), 0, x, z);
    }
    int32_t x = U(60);
    int32_t z = 0;
    CameraWallImpact::KeepCameraOnTrackSide(state, U(100), 0, x, z);
    CHECK(x == U(60));
    CameraWallImpact::Reset(state);
    CHECK(!state.previousHit && state.responseFrames == 0 && state.guardFrames == 0);
}
} // namespace

int main()
{
    TestInertiaOnlyOnImpactAndRecovers();
    TestWallSlideAndUnchangedSafeCamera();
    TestOppositeAndDiagonalNormals();
    TestMissingPushDoesNotInventWall();
    TestGuardExpiresAndResetClears();
    if (failures != 0) return 1;
    std::cout << "camera wall impact tests OK\n";
    return 0;
}
