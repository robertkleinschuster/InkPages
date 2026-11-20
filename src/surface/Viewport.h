#pragma once

// Pan and zoom with DirectManipulation, as Microsoft's TouchInputDirectManipulation sample (Windows-classic-samples):
// a viewport on the window whose primary content DirectManipulation inserts between two DirectComposition visuals
// (system DCompManipulationCompositor - transforms applied off the UI thread, inertia included).
// Input: touch contacts (WM_POINTERDOWN) and precision touchpad contacts (DM_POINTERHITTEST) via SetContact; mouse
// wheel via ProcessInput. Pen and mouse buttons never reach it.

#include <windows.h>
#include <dcomp.h>
#include <directmanipulation.h>

#include <functional>

#include <winrt/base.h>

#include "inkpages/model/ViewMath.h"

namespace inkpages::detail
{
    class Viewport
    {
    public:
        Viewport(HWND window, IDCompositionDevice* device, IDCompositionVisual* parent, IDCompositionVisual* child,
            float minZoom, float maxZoom);
        ~Viewport();
        Viewport(Viewport const&) = delete;
        Viewport& operator=(Viewport const&) = delete;

        void SetViewportRect(RECT pixels);
        void SetContentSize(SIZE pixels);

        /// WM_POINTERDOWN / DM_POINTERHITTEST: touch and touchpad contacts are given to DirectManipulation.
        bool OnPointer(WPARAM wp);
        /// WM_MOUSEWHEEL / WM_MOUSEHWHEEL / WM_POINTERWHEEL: handed to DirectManipulation (ProcessInput).
        bool OnWheel(MSG const& message);

        /// WM_KEYDOWN: handed to DirectManipulation (ProcessInput with the keyboard focus contact); false when it did not
        /// take the key.
        bool OnKey(MSG const& message);

        /// Moves the view so that the content rectangle (content pixels) fills it.
        void ZoomToRect(RectF content, bool animate);

        View Current() const { return current; }
        bool Ready() const { return status == DIRECTMANIPULATION_READY || status == DIRECTMANIPULATION_ENABLED; }
        bool Moving() const { return status == DIRECTMANIPULATION_RUNNING || status == DIRECTMANIPULATION_INERTIA; }

        /// UI thread: every content update (while moving too), and every status change (settled = READY).
        std::function<void(View)> onChanged;
        std::function<void(bool settled)> onStatus;

    private:
        struct Handler;
        HWND window;
        winrt::com_ptr<IDirectManipulationManager> manager;
        winrt::com_ptr<IDirectManipulationCompositor> compositor;
        winrt::com_ptr<IDirectManipulationViewport> viewport;
        winrt::com_ptr<IDirectManipulationContent> content;
        DWORD cookie = 0;
        View current;
        DIRECTMANIPULATION_STATUS status = DIRECTMANIPULATION_BUILDING;
        unsigned updates = 0;
    };
}
