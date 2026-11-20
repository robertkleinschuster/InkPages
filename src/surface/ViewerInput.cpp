#include "surface/ViewerInput.h"

#include <format>
#include <optional>

#include "inkpages/Log.h"
#include "surface/Viewport.h"

namespace inkpages::detail
{
    namespace
    {
        constexpr float LineDip = 40;    // an arrow key's step (screen DIPs, any zoom)
        constexpr float OverlapDip = 48; // what stays in view of the last screen on Bild auf/ab

        std::optional<ScrollKey> KeyOf(WPARAM key)
        {
            bool shift = GetKeyState(VK_SHIFT) < 0;
            switch (key)
            {
            case VK_UP: return ScrollKey::LineUp;
            case VK_DOWN: return ScrollKey::LineDown;
            case VK_LEFT: return ScrollKey::LineLeft;
            case VK_RIGHT: return ScrollKey::LineRight;
            case VK_PRIOR: return ScrollKey::PageUp;
            case VK_NEXT: return ScrollKey::PageDown;
            case VK_SPACE: return shift ? ScrollKey::PageUp : ScrollKey::PageDown;
            case VK_HOME: return ScrollKey::Start;
            case VK_END: return ScrollKey::End;
            default: return std::nullopt;
            }
        }
    }

    bool ViewerInput::OnKey(WPARAM key, LPARAM flags)
    {
        auto scroll = KeyOf(key);
        if (!scroll) return false;
        MSG message{ c.window, WM_KEYDOWN, key, flags };
        if (key != VK_SPACE && c.viewport->OnKey(message)) return true;
        float scale = c.scale();
        RectF rect = KeyScrollRect(*scroll, c.clientSize(), c.viewport->Current(), scale, c.layout().DocumentSize().height,
            LineDip * scale, OverlapDip * scale);
        Log(std::format("viewer: key 0x{:02X} scrolls", key));
        c.zoomToRect(rect);
        return true;
    }

    void ViewerInput::PanDown(POINT client, bool middleButton)
    {
        panning = true;
        middlePan = middleButton;
        panMoved = false;
        panFrom = client;
        panView = c.viewport->Current();
        SetCapture(c.window);
        SetCursor(LoadCursorW(nullptr, IDC_SIZEALL));
        Log(std::format("viewer: {}-button pan from {},{}", middleButton ? "middle" : "left", client.x, client.y));
    }

    bool ViewerInput::PanMove(POINT client)
    {
        if (!panning) return false;
        // From the press: the view moves opposite to the mouse, so the content follows it.
        c.zoomToRect(PanRect(c.clientSize(), panView, static_cast<float>(panFrom.x - client.x),
            static_cast<float>(panFrom.y - client.y)));
        if (!panMoved && (panFrom.x != client.x || panFrom.y != client.y))
        {
            panMoved = true;
            if (c.panned) c.panned();
        }
        return true;
    }

    bool ViewerInput::PanUp()
    {
        if (!panning || !middlePan) return false;
        panning = false;
        if (GetCapture() == c.window) ReleaseCapture();
        Log("viewer: pan ended");
        return true;
    }

    void ViewerInput::CaptureLost()
    {
        if (panning) Log("viewer: pan ended (capture lost)");
        panning = false;
    }

    void ViewerInput::Hover(POINT client)
    {
        if (!tracking)
        {
            TRACKMOUSEEVENT track{ sizeof(track), TME_LEAVE, c.window, 0 };
            if (TrackMouseEvent(&track)) tracking = true;
            else LogHr("TrackMouseEvent", HRESULT_FROM_WIN32(GetLastError()));
        }
        PointF at = c.toDocument(client);
        PageLayout const& layout = c.layout();
        auto page = layout.PageAt(at);
        if (page)
        {
            RectF r = layout.Page(*page);
            Report(*page, { at.x - r.left, at.y - r.top });
        }
        else
        {
            Report(-1, {});
        }
    }

    void ViewerInput::NotHovering()
    {
        if (hoverCursor) Report(-1, {});
    }

    void ViewerInput::Left()
    {
        tracking = false;
        Report(-1, {}, false); // the mouse is over another window now: its cursor
    }

    void ViewerInput::Report(int page, PointF point, bool apply)
    {
        LPCWSTR cursor = c.cursorAt ? c.cursorAt(page, point) : nullptr;
        if (cursor == hoverCursor) return;
        hoverCursor = cursor;
        if (apply && !panning) SetCursor(LoadCursorW(nullptr, cursor ? cursor : IDC_ARROW));
    }

    bool ViewerInput::OnSetCursor()
    {
        LPCWSTR cursor = panning ? IDC_SIZEALL : hoverCursor;
        if (!cursor) return false;
        SetCursor(LoadCursorW(nullptr, cursor));
        return true;
    }
}
