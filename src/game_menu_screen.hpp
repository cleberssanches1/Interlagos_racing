#pragma once

#include <srl.hpp>

#include "game_menu_catalog.hpp"

#ifndef AUTO_LAP_PHYSICS_DRIVER
#define AUTO_LAP_PHYSICS_DRIVER 1
#endif

namespace GameMenu
{
inline void DrawOptionLine(uint8_t row, bool selected, const char* name, bool available)
{
    SRL::Debug::Print(3, row, "%c %s%s",
                      selected ? '>' : ' ',
                      name,
                      available ? "" : "  [EM DESENV.]");
}

inline void Draw(const State& state)
{
    for (uint8_t row = 2u; row <= 26u; ++row) SRL::Debug::PrintClearLine(row);
    SRL::Debug::Print(3, 3, "INTERLAGOS RACING");
    SRL::Debug::Print(3, 5, "SELECAO DE JOGO");

    if (state.page == MenuScreen::Confirm)
    {
        const Selection selection = Selected(state);
        SRL::Debug::Print(3, 8, "CONFIRMAR");
        SRL::Debug::Print(3, 11, "MODO : %s", selection.mode->name);
        SRL::Debug::Print(3, 13, "PISTA: %s", selection.track->name);
        SRL::Debug::Print(3, 15, "CARRO: %s", selection.car->name);
        if (selection.mode->id == Mode::TimeAttack)
            SRL::Debug::Print(3, 18, "HUD DE TEMPOS: ATIVO");
        else if (selection.mode->id == Mode::AutoLap)
            SRL::Debug::Print(3, 18, AUTO_LAP_PHYSICS_DRIVER ?
                             "PILOTO: FISICA/TCOL" : "ROTA GUIADA: PROTOTIPO");
        SRL::Debug::Print(3, 20, "A/START: INICIAR");
    }
    else
    {
        const char* title = (state.page == MenuScreen::Mode) ? "MODO" :
                            (state.page == MenuScreen::Track) ? "PISTA" : "CARRO";
        const size_t count = (state.page == MenuScreen::Mode) ? ModeCount() :
                             (state.page == MenuScreen::Track) ? TrackCount() : CarCount();
        const size_t selected = (state.page == MenuScreen::Mode) ? state.modeIndex :
                                (state.page == MenuScreen::Track) ? state.trackIndex : state.carIndex;
        SRL::Debug::Print(3, 8, "%s  %u/%u", title,
                          static_cast<unsigned>(selected + 1u),
                          static_cast<unsigned>(count));
        const size_t first = (selected >= 5u) ? (selected - 5u) : 0u;
        const size_t last = ((first + 6u) < count) ? (first + 6u) : count;
        for (size_t index = first; index < last; ++index)
        {
            const char* name = (state.page == MenuScreen::Mode) ? kModes[index].name :
                               (state.page == MenuScreen::Track) ? kTracks[index].name : kCars[index].name;
            const bool available = (state.page == MenuScreen::Mode) ? kModes[index].available :
                                   (state.page == MenuScreen::Track) ? kTracks[index].available :
                                                                   kCars[index].available;
            DrawOptionLine(static_cast<uint8_t>(11u + ((index - first) * 2u)),
                           index == selected, name, available);
        }
        SRL::Debug::Print(3, 23, "DIRECIONAL: ESCOLHER  A: AVANCAR");
    }
    SRL::Debug::Print(3, 25, "B: VOLTAR");
}

inline Selection Run()
{
    State state{};
    SRL::Input::Digital pad(0);
    SRL::VDP2::SetBackColor(SRL::Types::HighColor::FromRGB555(1, 3, 7));
    SRL::VDP2::NBG3::SetPriority(SRL::VDP2::Priority::Layer7);
    SRL::Debug::PrintClearScreen();
    Draw(state);
    while (true)
    {
        SRL::Core::Synchronize();
        Input input = Input::None;
        if (pad.WasPressed(SRL::Input::Digital::Button::Up) ||
            pad.WasPressed(SRL::Input::Digital::Button::Left))
        {
            input = Input::Previous;
        }
        else if (pad.WasPressed(SRL::Input::Digital::Button::Down) ||
                 pad.WasPressed(SRL::Input::Digital::Button::Right))
        {
            input = Input::Next;
        }
        else if (pad.WasPressed(SRL::Input::Digital::Button::B))
        {
            input = Input::Back;
        }
        else if (pad.WasPressed(SRL::Input::Digital::Button::A) ||
                 (state.page == MenuScreen::Confirm &&
                  pad.WasPressed(SRL::Input::Digital::Button::START)))
        {
            input = Input::Accept;
        }

        const Action action = Apply(state, input);
        if (action == Action::Launch)
        {
            const Selection selection = Selected(state);
            SRL::Debug::PrintClearScreen();
            SRL::Core::Synchronize();
            return selection;
        }
        if (action == Action::Redraw) Draw(state);
    }
}
} // namespace GameMenu
