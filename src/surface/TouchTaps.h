#pragma once

// Finger taps for the app (UI thread): the finger's contacts belong to DirectManipulation (pan, pinch), which takes
// them only once they move; a contact that ends within Tap's rules, alone and before DirectManipulation took it, is a
// tap. It changes nothing in the panning - the contact is given to DirectManipulation as before, the window only
// watches the contact's messages.

#include <windows.h>
#include <interactioncontext.h>

#include <functional>
#include <optional>

#include "inkpages/model/TapGesture.h"

namespace inkpages::detail
{
    class TouchTaps
    {
    public:
        TouchTaps(std::function<PointF(POINT client)> toDocument, std::function<float()> zoom,
            std::function<void(PointF document)> tapped, std::function<void(POINT screen)> held)
            : toDocument(std::move(toDocument)), zoom(std::move(zoom)), tapped(std::move(tapped)), held(std::move(held)) {}
        ~TouchTaps();

        void Down(UINT32 pointer, POINT client);
        void Update(UINT32 pointer, POINT client);
        void Up(UINT32 pointer, POINT client);
        bool Active() const { return gesture.Active(); }
        /// DirectManipulation took the contacts (capture changed, the view moves): no tap; their later messages are ignored.
        void Cancel(char const* why);

    private:
        static void CALLBACK OnInteraction(void* user, INTERACTION_CONTEXT_OUTPUT const* result) noexcept;
        void WatchHold(UINT32 pointer);
        void ResetHold();
        std::function<PointF(POINT)> toDocument;
        std::function<float()> zoom;
        std::function<void(PointF)> tapped;
        std::function<void(POINT)> held;
        HINTERACTIONCONTEXT context = nullptr;
        UINT32 holdContact = 0;
        std::optional<POINT> secondaryTap;
        TapGesture gesture;
    };
}
