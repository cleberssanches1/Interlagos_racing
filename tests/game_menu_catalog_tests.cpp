#include <cassert>

#include "game_menu_catalog.hpp"

int main()
{
    using namespace GameMenu;
    static_assert(ModeCount() == 3u, "Expected the current mode catalog");
    static_assert(TrackCount() == 1u, "Expected one current track");
    static_assert(CarCount() == 1u, "Expected one current car");

    State state{};
    assert(SelectionAvailable(state));
    assert(Selected(state).mode->id == Mode::TimeAttack);
    assert(Apply(state, Input::Next) == Action::Redraw);
    assert(state.modeIndex == 0u); // Race is skipped; Free Drive remains available.
    assert(Apply(state, Input::Next) == Action::Redraw);
    assert(state.modeIndex == 1u);
    assert(Apply(state, Input::Accept) == Action::Redraw);
    assert(state.page == MenuScreen::Track);
    assert(Apply(state, Input::Next) == Action::None); // one available track
    assert(Apply(state, Input::Accept) == Action::Redraw);
    assert(state.page == MenuScreen::Car);
    assert(Apply(state, Input::Accept) == Action::Redraw);
    assert(state.page == MenuScreen::Confirm);
    assert(Apply(state, Input::Back) == Action::Redraw);
    assert(state.page == MenuScreen::Car);
    assert(Apply(state, Input::Accept) == Action::Redraw);
    assert(Apply(state, Input::Accept) == Action::Launch);
    const Selection selected = Selected(state);
    assert(selected.mode->id == Mode::TimeAttack);
    assert(selected.track->startLineSegmentId == 13u);
    assert(selected.car->paths != nullptr && selected.car->pathCount > 0u);

    const ModeEntry futureModes[] = {
        {Mode::FreeDrive, "free", true},
        {Mode::TimeAttack, "time attack", false},
        {Mode::Race, "race", true}
    };
    assert(NextAvailable(futureModes, 3u, 0u, +1) == 2u);
    assert(NextAvailable(futureModes, 3u, 0u, -1) == 2u);
    assert(NextAvailable(futureModes, 3u, 2u, +1) == 0u);

    const TrackEntry futureTracks[] = {
        {1u, "Interlagos", "interlagos", 13u, true},
        {2u, "Another track", "another", 1u, true}
    };
    const CarEntry futureCars[] = {
        {1u, "Car 1", kCar1Paths, 8u, true},
        {2u, "Car 2", kCar1Paths, 8u, true}
    };
    assert(NextAvailable(futureTracks, 2u, 0u, +1) == 1u);
    assert(NextAvailable(futureTracks, 2u, 1u, +1) == 0u);
    assert(NextAvailable(futureCars, 2u, 0u, -1) == 1u);
    return 0;
}
