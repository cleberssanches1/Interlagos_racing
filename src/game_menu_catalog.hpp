#pragma once

#include <cstddef>
#include <cstdint>

namespace GameMenu
{
enum class Mode : uint8_t
{
    FreeDrive = 0,
    TimeAttack,
    AutoLap,
    Race
};

enum class MenuScreen : uint8_t
{
    Mode = 0,
    Track,
    Car,
    Confirm
};

enum class Input : uint8_t
{
    None = 0,
    Previous,
    Next,
    Accept,
    Back
};

enum class Action : uint8_t
{
    None = 0,
    Redraw,
    Launch
};

struct ModeEntry
{
    Mode id;
    const char* name;
    bool available;
};

struct TrackEntry
{
    uint8_t id;
    const char* name;
    const char* assetKey;
    uint16_t startLineSegmentId;
    bool available;
};

struct CarEntry
{
    uint8_t id;
    const char* name;
    const char* const* paths;
    size_t pathCount;
    bool available;
};

inline constexpr const char* kCar1Paths[] = {
    "CD/DATA/CAR1.NYA;1", "CD/DATA/CAR1.NYA",
    "DATA/CAR1.NYA;1", "DATA/CAR1.NYA",
    "CAR1.NYA;1", "CAR1.NYA",
    "car1.nya;1", "car1.nya"
};

// Add new choices here. Their asset loader and race rules must be ready before
// changing available to true; a visible but unavailable entry cannot launch.
inline constexpr ModeEntry kModes[] = {
    {Mode::FreeDrive, "TREINO LIVRE", true},
    {Mode::TimeAttack, "TIME ATTACK", true},
    {Mode::AutoLap, "AUTO LAP", true},
    {Mode::Race, "CORRIDA", false}
};
inline constexpr TrackEntry kTracks[] = {
    {1u, "INTERLAGOS", "interlagos", 13u, true}
};
inline constexpr CarEntry kCars[] = {
    {1u, "CARRO 1", kCar1Paths, sizeof(kCar1Paths) / sizeof(kCar1Paths[0]), true}
};

constexpr size_t ModeCount() { return sizeof(kModes) / sizeof(kModes[0]); }
constexpr size_t TrackCount() { return sizeof(kTracks) / sizeof(kTracks[0]); }
constexpr size_t CarCount() { return sizeof(kCars) / sizeof(kCars[0]); }
constexpr bool StartsAutoLap(Mode mode) { return mode == Mode::AutoLap; }

struct Selection
{
    const ModeEntry* mode = &kModes[0];
    const TrackEntry* track = &kTracks[0];
    const CarEntry* car = &kCars[0];
};

struct State
{
    MenuScreen page = MenuScreen::Mode;
    // During Time Attack development, make its timing HUD the initial choice.
    // Free Drive remains one directional step away in the menu.
    size_t modeIndex = 1u;
    size_t trackIndex = 0u;
    size_t carIndex = 0u;
};

inline Selection Selected(const State& state)
{
    return {&kModes[state.modeIndex], &kTracks[state.trackIndex], &kCars[state.carIndex]};
}

inline bool SelectionAvailable(const State& state)
{
    return kModes[state.modeIndex].available &&
           kTracks[state.trackIndex].available &&
           kCars[state.carIndex].available;
}

template <typename Entry>
inline size_t NextAvailable(const Entry* entries, size_t count, size_t current, int direction)
{
    if (!entries || count == 0u || current >= count) return current;
    for (size_t tried = 0u; tried < count; ++tried)
    {
        current = (direction < 0) ? (current + count - 1u) % count : (current + 1u) % count;
        if (entries[current].available) return current;
    }
    return current;
}

inline size_t StepAvailable(size_t current, int direction, MenuScreen page)
{
    return (page == MenuScreen::Mode) ? NextAvailable(kModes, ModeCount(), current, direction) :
           (page == MenuScreen::Track) ? NextAvailable(kTracks, TrackCount(), current, direction) :
                                   NextAvailable(kCars, CarCount(), current, direction);
}

inline Action Apply(State& state, Input input)
{
    switch (input)
    {
    case Input::Previous:
    case Input::Next:
    {
        if (state.page == MenuScreen::Confirm) return Action::None;
        const int direction = (input == Input::Previous) ? -1 : 1;
        size_t* index = (state.page == MenuScreen::Mode) ? &state.modeIndex :
                        (state.page == MenuScreen::Track) ? &state.trackIndex : &state.carIndex;
        const size_t next = StepAvailable(*index, direction, state.page);
        if (next == *index) return Action::None;
        *index = next;
        return Action::Redraw;
    }
    case Input::Accept:
        if (state.page == MenuScreen::Confirm)
        {
            return SelectionAvailable(state) ? Action::Launch : Action::None;
        }
        state.page = static_cast<MenuScreen>(static_cast<uint8_t>(state.page) + 1u);
        return Action::Redraw;
    case Input::Back:
        if (state.page == MenuScreen::Mode) return Action::None;
        state.page = static_cast<MenuScreen>(static_cast<uint8_t>(state.page) - 1u);
        return Action::Redraw;
    default:
        return Action::None;
    }
}
} // namespace GameMenu
