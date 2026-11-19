#pragma once

// Windows' ruler and protractor (InkPresenterRuler / InkPresenterProtractor) on the presenter: strokes drawn along
// their edge come out straight or as arcs - Windows does that. Ink thread.

#include <winrt/Windows.UI.Input.Inking.h>

#include "inkpages/model/Paper.h"

namespace inkpages::detail
{
    class Stencils
    {
    public:
        explicit Stencils(winrt::Windows::UI::Input::Inking::InkPresenter const& presenter) : presenter(presenter) {}

        /// `at`: where it appears (document coordinates; measured: without it the ruler appeared at the document-sized
        /// presenter's top right, out of view).
        void ShowRuler(bool shown, PointF at);
        void ShowProtractor(bool shown, PointF at);

    private:
        winrt::Windows::UI::Input::Inking::InkPresenter presenter;
        winrt::Windows::UI::Input::Inking::InkPresenterRuler ruler{ nullptr };
        winrt::Windows::UI::Input::Inking::InkPresenterProtractor protractor{ nullptr };
    };
}
