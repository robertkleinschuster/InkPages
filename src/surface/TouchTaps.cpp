#include "surface/TouchTaps.h"

#include <format>

#include "inkpages/Log.h"

namespace inkpages::detail
{
    void TouchTaps::Down(UINT32 pointer, POINT client)
    {
        gesture.Press(pointer, toDocument(client), GetTickCount64());
        Log(std::format("touch: contact {} down", pointer));
    }

    void TouchTaps::Update(UINT32 pointer, POINT client)
    {
        gesture.Move(pointer, toDocument(client));
    }

    void TouchTaps::Up(UINT32 pointer, POINT client)
    {
        if (!gesture.Active()) return;
        auto tap = gesture.Release(pointer, toDocument(client), GetTickCount64());
        Log(std::format("touch: contact {} up{}", pointer, tap ? " (a tap)" : ""));
        if (tap && tapped) tapped(*tap);
    }

    void TouchTaps::Cancel(char const* why)
    {
        if (!gesture.Active()) return;
        gesture = {}; // its contacts' messages may not come any more: forget them
        Log(std::format("touch: no tap ({})", why));
    }
}
