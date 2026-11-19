#pragma once

// Windows' ruler and protractor (InkPresenterRuler / InkPresenterProtractor) on the presenter: strokes drawn along
// their edge come out straight or as arcs - Windows does that. Ink thread.

#include <winrt/Windows.UI.Input.Inking.h>

namespace inkpages::detail
{
    class Stencils
    {
    public:
        explicit Stencils(winrt::Windows::UI::Input::Inking::InkPresenter const& presenter) : presenter(presenter) {}

        void ShowRuler(bool shown);
        void ShowProtractor(bool shown);

    private:
        winrt::Windows::UI::Input::Inking::InkPresenter presenter;
        winrt::Windows::UI::Input::Inking::InkPresenterRuler ruler{ nullptr };
        winrt::Windows::UI::Input::Inking::InkPresenterProtractor protractor{ nullptr };
    };
}
