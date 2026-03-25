#pragma once

#include <cstdint>
#include <srl.hpp>

namespace AppState
{
enum class Stage : uint8_t
{
    Boot = 0,
    CoreInit,
    BackgroundInit,
    CarLoad,
    TrackInit,
    LoopStart,
    LoopFrameBegin,
    LoopBackground,
    LoopTrack,
    LoopCar,
    LoopSync,
    Fault
};

inline const char* StageName(Stage stage)
{
    switch (stage)
    {
    case Stage::Boot: return "boot";
    case Stage::CoreInit: return "core";
    case Stage::BackgroundInit: return "bg_init";
    case Stage::CarLoad: return "car_load";
    case Stage::TrackInit: return "track_init";
    case Stage::LoopStart: return "loop_start";
    case Stage::LoopFrameBegin: return "frame_begin";
    case Stage::LoopBackground: return "bg";
    case Stage::LoopTrack: return "track";
    case Stage::LoopCar: return "car";
    case Stage::LoopSync: return "sync";
    case Stage::Fault: return "fault";
    default: return "unknown";
    }
}

inline volatile Stage g_stage = Stage::Boot;
inline volatile uint32_t g_stageFrame = 0;

inline void Set(Stage stage, uint32_t frame = 0)
{
    g_stage = stage;
    g_stageFrame = frame;
}

inline void PresentOverlay(uint32_t row = 2)
{
    constexpr bool kEnableAppStateOverlay = false;
    if (!kEnableAppStateOverlay) return;
    SRL::Debug::Print(2, row, "APP stage:%s f:%lu",
                      StageName(static_cast<Stage>(g_stage)),
                      static_cast<unsigned long>(g_stageFrame));
}
} // namespace AppState
