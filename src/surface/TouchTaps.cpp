#include "surface/TouchTaps.h"

#include <format>
#include <winrt/base.h>

#include "inkpages/Log.h"

namespace inkpages::detail
{
    TouchTaps::~TouchTaps()
    {
        if (!context) return;
        HRESULT hr = DestroyInteractionContext(context);
        if (FAILED(hr)) LogHr("touch: DestroyInteractionContext", hr);
    }

    void CALLBACK TouchTaps::OnInteraction(void* user, INTERACTION_CONTEXT_OUTPUT const* result) noexcept
    {
        if (result->interactionId == INTERACTION_ID_SECONDARY_TAP && !(result->interactionFlags & INTERACTION_FLAG_CANCEL))
        {
            auto self = static_cast<TouchTaps*>(user);
            self->secondaryTap = POINT{ static_cast<LONG>(result->x), static_cast<LONG>(result->y) };
        }
    }

    void TouchTaps::WatchHold(UINT32 pointer)
    {
        if (pointer != holdContact) return;
        try
        {
            if (!context)
            {
                winrt::check_hresult(CreateInteractionContext(&context));
                INTERACTION_CONTEXT_CONFIGURATION config{ INTERACTION_ID_SECONDARY_TAP, INTERACTION_CONFIGURATION_FLAG_SECONDARY_TAP };
                winrt::check_hresult(SetInteractionConfigurationInteractionContext(context, 1, &config));
                winrt::check_hresult(SetPropertyInteractionContext(context, INTERACTION_CONTEXT_PROPERTY_FILTER_POINTERS, FALSE));
                winrt::check_hresult(RegisterOutputCallbackInteractionContext(context, OnInteraction, this));
            }
            POINTER_INFO info{};
            winrt::check_bool(GetPointerInfo(pointer, &info));
            winrt::check_hresult(ProcessPointerFramesInteractionContext(context, 1, 1, &info));
        }
        catch (...)
        {
            LogException("touch: recognizing press and hold");
            ResetHold();
        }
    }

    void TouchTaps::ResetHold()
    {
        holdContact = 0;
        if (context)
        {
            HRESULT hr = StopInteractionContext(context);
            if (FAILED(hr)) LogHr("touch: StopInteractionContext", hr);
        }
        secondaryTap.reset();
    }

    void TouchTaps::Down(UINT32 pointer, POINT client)
    {
        if (gesture.Active()) ResetHold();
        else if (held)
        {
            holdContact = pointer;
            WatchHold(pointer);
        }
        gesture.Press(pointer, toDocument(client), GetTickCount64(), zoom());
        Log(std::format("touch: contact {} down", pointer));
    }

    void TouchTaps::Update(UINT32 pointer, POINT client)
    {
        gesture.Move(pointer, toDocument(client));
        WatchHold(pointer);
    }

    void TouchTaps::Up(UINT32 pointer, POINT client)
    {
        if (!gesture.Active()) return;
        auto tap = gesture.Release(pointer, toDocument(client), GetTickCount64());
        WatchHold(pointer);
        auto hold = secondaryTap;
        ResetHold();
        Log(std::format("touch: contact {} up{}", pointer, tap ? " (a tap)" : ""));
        if (hold && held) held(*hold);
        else if (tap && tapped) tapped(*tap);
    }

    void TouchTaps::Cancel(char const* why)
    {
        if (!gesture.Active()) return;
        ResetHold();
        gesture = {}; // its contacts' messages may not come any more: forget them
        Log(std::format("touch: no tap ({})", why));
    }
}
