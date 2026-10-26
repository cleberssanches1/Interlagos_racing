#include <cassert>

#include "game_menu_catalog.hpp"

int main()
{
    using namespace GameMenu;
    static_assert(ModeCount() == 4u, "Expected the current mode catalog");
    static_assert(TrackCount() == 1u, "Expected one current track");
    static_assert(CarCount() == 1u, "Expected one current car");

    State state{};
    assert(SelectionAvailable(state));
    assert(Selected(state).mode->id == Mode::TimeAttack);
    assert(!StartsAutoLap(Mode::FreeDrive));
    assert(!StartsAutoLap(Mode::TimeAttack));
    assert(StartsAutoLap(Mode::AutoLap));
    assert(Apply(state, Input::Next) == Action::Redraw);
    assert(Selected(state).mode->id == Mode::AutoLap);
    assert(Apply(state, Input::Next) == Action::Redraw);
    assert(Selected(state).mode->id == Mode::FreeDrive); // Race is skipped.
    assert(Apply(state, Input::Previous) == Action::Redraw);
    assert(Selected(state).mode->id == Mode::AutoLap);
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
    assert(selected.mode->id == Mode::AutoLap);
    assert(StartsAutoLap(selected.mode->id));
    assert(selected.track->startLineSegmentId == 13u);
    assert(selected.car->paths != nullptr && selected.car->pathCount > 0u);

    const ModeEntry futureModes[] = {
        {Mode::FreeDrive, "free", true},
        {Mode::TimeAttack, "time attack", false},
        {Mode::AutoLap, "auto lap", false},
        {Mode::Race, "race", true}
    };
    assert(NextAvailable(futureModes, 4u, 0u, +1) == 3u);
    assert(NextAvailable(futureModes, 4u, 0u, -1) == 3u);
    assert(NextAvailable(futureModes, 4u, 3u, +1) == 0u);

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
