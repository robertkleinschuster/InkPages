#pragma once

#include <functional>
#include <map>
#include <optional>
#include <vector>

#include <dcomp.h>
#include <winrt/Windows.UI.Input.Inking.h>

#include "inkpages/model/Geometry.h"
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

    /// The app's pictures and texts of the selection: drawn once into a texture of their own that moves with the drag.
    struct InkDragObjects
    {
        RectF bounds; // document
        std::function<bool(ID2D1DeviceContext*)> draw; // draws them in document units; false: cannot
    };

    // A temporary replacement for what a selection moves, during translation: the selected ink and objects are textures
    // that DirectComposition offsets; when ink moves, the live ink visual is hidden and the stationary ink is textures
    // too (the presenter cannot hide single strokes). Order, bottom to top: objects, stationary ink, moving ink. The
    // ink container stays authoritative and receives the edit only when the pointer is released.
    class InkDragVisual
    {
    public:
        InkDragVisual(Graphics& graphics, IDCompositionVisual* parent, IDCompositionVisual* live);
        void Prepare(InkDragSnapshot snapshot, std::optional<InkDragObjects> objects, float scale);
        /// Moves, scales or rotates what is lifted by `m` (the drag's transform, document units).
        bool Move(Affine const& m);
        void Reset();
        bool Active() const { return active; }
        bool Ready() const { return ready; }

    private:
        Graphics& graphics;
        winrt::com_ptr<IDCompositionVisual3> live, preview;
        winrt::com_ptr<IDCompositionVisual> objectsGroup; // below the live ink, always
        winrt::com_ptr<IDCompositionVisual> moving, movingObjects;
        bool ready = false, active = false, hidesLive = false;
    };
}
