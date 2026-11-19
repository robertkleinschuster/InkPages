#pragma once

// Finger taps for the app (UI thread): the finger's contacts belong to DirectManipulation (pan, pinch), which takes
// them only once they move; a contact that ends within Tap's rules, alone and before DirectManipulation took it, is a
// tap. It changes nothing in the panning - the contact is given to DirectManipulation as before, the window only
// watches the contact's messages.

#include <windows.h>

#include <functional>

#include "inkpages/model/TapGesture.h"

namespace inkpages::detail
{
    class TouchTaps
    {
    public:
        TouchTaps(std::function<PointF(POINT client)> toDocument, std::function<void(PointF document)> tapped)
            : toDocument(std::move(toDocument)), tapped(std::move(tapped)) {}

        void Down(UINT32 pointer, POINT client);
        void Update(UINT32 pointer, POINT client);
        void Up(UINT32 pointer, POINT client);
        /// DirectManipulation took the contacts (capture changed, the view moves): no tap; their later messages are ignored.
        void Cancel(char const* why);

    private:
        std::function<PointF(POINT)> toDocument;
        std::function<void(PointF)> tapped;
        TapGesture gesture;
    };
}
