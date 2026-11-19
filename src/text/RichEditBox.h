#pragma once

// A windowless RichEdit (msftedit.dll: ITextServices2 with our ITextHost2) for one plain text: one font, size and color,
// newlines, no wrapping. The window it belongs to forwards its keyboard and mouse messages (client pixels); it draws
// with TxDrawD2D into a Direct2D context. Measured in the text probe (feature-probes/textedit): the render target's
// DPI must be the window's, the bounds given to TxDrawD2D are render-target pixels, while the client rectangle, the
// mouse and the caret are client pixels. A D2D host draws the caret itself (RichEdit reports where it is).

#include <windows.h>
#include <d2d1_1.h>
#include <richedit.h>
#include <textserv.h>

#include <functional>
#include <memory>
#include <string>

#include "inkpages/model/Text.h"

namespace inkpages::detail
{
    class RichEditBox
    {
    public:
        /// RichEdit's timers are window timers from `timerBase` on (the window hands WM_TIMER back: OnTimer).
        RichEditBox(HWND window, UINT_PTR timerBase);
        ~RichEditBox();
        RichEditBox(RichEditBox const&) = delete;
        RichEditBox& operator=(RichEditBox const&) = delete;

        void SetText(std::wstring const& text);   // \n for newlines
        std::wstring Text() const;
        /// The font (face, twips - RichEditText::Twips) and color of the whole text.
        void SetFormat(std::wstring const& face, long twips, Color color);
        /// Where the box is in the window (client pixels): RichEdit hit-tests and places its caret there.
        void SetClientRect(RECT client);
        RECT ClientRect() const { return client; }
        /// The size the text needs (pixels; at least one line).
        SIZE NaturalSize() const;
        /// Draws the text into `bounds` (render-target pixels; the context's DPI is the window's).
        void Draw(ID2D1DeviceContext* dc, RECTL bounds);

        void Focus(bool focused);
        /// A window message for RichEdit (keyboard, mouse in client pixels, IME); true when it took it.
        bool Send(UINT message, WPARAM wp, LPARAM lp, LRESULT* result = nullptr);
        /// WM_SETCURSOR over the box: RichEdit's cursor (an I-beam).
        void SetCursorAt(POINT client);
        bool OwnsTimer(UINT_PTR id) const { return id >= timerBase && id < timerBase + 0x100; }
        void OnTimer(UINT_PTR id);

        struct Caret
        {
            bool shown = false;
            POINT at{};   // client pixels
            SIZE size{};
        };
        Caret CaretNow() const;
        bool HasCapture() const;

        /// RichEdit wants to be drawn again (text, selection or caret changed).
        std::function<void()> invalidated;

    private:
        class Host;
        HWND window;
        UINT_PTR timerBase;
        RECT client{};
        std::unique_ptr<Host> host;
        ITextServices2* services = nullptr;
    };
}
