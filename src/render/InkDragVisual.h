#pragma once

#include <map>
#include <vector>

#include <dcomp.h>
#include <winrt/Windows.UI.Input.Inking.h>

#include "inkpages/model/PageLayout.h"

namespace inkpages::detail
{
    struct Graphics;

    struct InkDragSnapshot
    {
        using Stroke = winrt::Windows::UI::Input::Inking::InkStroke;
        std::vector<Stroke> moving;
        std::map<int, std::vector<Stroke>> stationary;
    };

    // A temporary replacement for the live ink visual during translation. The ink container
    // stays authoritative and receives the edit only when the pointer is released.
    class InkDragVisual
    {
    public:
        InkDragVisual(Graphics& graphics, IDCompositionVisual* parent, IDCompositionVisual* live);
        void Prepare(InkDragSnapshot snapshot, float scale);
        bool Move(PointF offset);
        void Reset();
        bool Active() const { return active; }

    private:
        Graphics& graphics;
        winrt::com_ptr<IDCompositionVisual3> live, preview;
        winrt::com_ptr<IDCompositionVisual> moving;
        bool ready = false, active = false;
    };
}
