#include "surface/TextEditor.h"

#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <format>

#include "ink/InkLayer.h"
#include "inkpages/Log.h"
#include "surface/SurfaceAutomation.h"
#include "tools/TextEdit.h"
#include "tools/ToolInput.h"

namespace inkpages::detail
{
    namespace
    {
        constexpr UINT_PTR CaretTimer = 4;
        constexpr UINT_PTR RichEditTimers = 0x1000;

        bool Blank(std::wstring const& text)
        {
            return std::all_of(text.begin(), text.end(), [](wchar_t ch) { return std::iswspace(ch) != 0; });
        }

        bool IsKeyboard(UINT m)
        {
            switch (m)
            {
            case WM_KEYDOWN: case WM_KEYUP: case WM_CHAR: case WM_DEADCHAR: case WM_SYSCHAR: case WM_UNICHAR:
            case WM_IME_STARTCOMPOSITION: case WM_IME_COMPOSITION: case WM_IME_ENDCOMPOSITION: case WM_IME_NOTIFY:
            case WM_IME_CHAR: case WM_IME_REQUEST: case WM_IME_SETCONTEXT: case WM_INPUTLANGCHANGE:
                return true;
            default:
                return false;
            }
        }
    }

    TextEditor::~TextEditor()
    {
        try
        {
            End();
        }
        catch (...)
        {
            LogException("text: end on shutdown");
        }
    }

    PointF TextEditor::DocumentAt() const
    {
        RectF page = c.layout().Page(now.page);
        return { page.left + now.at.x, page.top + now.at.y };
    }

    RECT TextEditor::ClientOfText() const
    {
        PointF px = DocumentToViewport(DocumentAt(), c.view(), c.scale());
        LONG x = std::lround(px.x), y = std::lround(px.y);
        return { x, y, x + textPx.cx, y + textPx.cy };
    }

    bool TextEditor::InBox(POINT client) const
    {
        RECT r = ClientOfText();
        int m = TextBoxVisual::Margin(c.scale());
        InflateRect(&r, m, m);
        return PtInRect(&r, client) != FALSE;
    }

    std::optional<RectF> TextEditor::CaretInClient() const
    {
        if (!box) return std::nullopt;
        auto caret = box->CaretNow();
        if (!caret.shown || caret.size.cy <= 0) return std::nullopt;
        return RectF{ static_cast<float>(caret.at.x), static_cast<float>(caret.at.y),
            static_cast<float>(caret.at.x + caret.size.cx), static_cast<float>(caret.at.y + caret.size.cy) };
    }

    void TextEditor::Tap(PointF at, TextStyle const& style)
    {
        Log(std::format("text: tap at {:.1f},{:.1f} (editing {}, text host {})", at.x, at.y, Editing(), c.texts() != nullptr));
        if (Editing())
        {
            Log("text: a tap beside the box ends editing");
            End();
            return;
        }
        TextHost* texts = c.texts();
        if (!texts)
        {
            Log("text: no text host - the app has not set one (SetTextHost)");
            return;
        }
        if (auto existing = texts->TextAt(at))
        {
            Begin(existing, texts->Get(*existing), at);
            return;
        }
        auto page = c.layout().PageAt(at);
        if (!page)
        {
            Log("text: the tap is beside the pages");
            return;
        }
        RectF r = c.layout().Page(*page);
        Text text{ *page, { at.x - r.left, at.y - r.top - style.size * 0.6f }, L"", style }; // the line centered on the tap
        Begin(std::nullopt, text, at);
    }

    void TextEditor::Begin(std::optional<TextId> editing, Text text, PointF caretAt)
    {
        id = editing;
        original = now = std::move(text);
        box = std::make_unique<RichEditBox>(c.window, RichEditTimers);
        box->invalidated = [this] { ScheduleDraw(); };
        box->SetText(now.content);
        visual = std::make_unique<TextBoxVisual>(*c.graphics, c.layer);
        if (id) c.texts()->SetEditing(id);
        laidOutAt = 0;
        Layout();
        focusBefore = SetFocus(c.window);
        box->Focus(true);
        if (c.automation) // the edited box as UI Automation's focused text control (touch keyboard, Narrator)
        {
            c.automation->TextBoxBegins(*box, [this] {
                RECT r = ClientOfText();
                MapWindowPoints(c.window, nullptr, reinterpret_cast<POINT*>(&r), 2);
                return r;
            });
        }
        // The caret where the pen tapped (a new text: at its start).
        PointF px = DocumentToViewport(caretAt, c.view(), c.scale());
        POINTL p{ std::lround(px.x), std::lround(px.y) };
        LRESULT at = 0;
        box->Send(EM_CHARFROMPOS, 0, reinterpret_cast<LPARAM>(&p), &at);
        box->Send(EM_SETSEL, static_cast<WPARAM>(at), at); // RichEdit: the character index
        caretOn = true;
        UINT blink = GetCaretBlinkTime();
        if (blink != INFINITE && blink > 0) SetTimer(c.window, CaretTimer, blink, nullptr);
        Log(std::format("text: editing {} on page {}", id ? std::format("text {}", *id) : std::string("a new text"), now.page));
        if (editingChanged) editingChanged(true);
    }

    void TextEditor::Layout()
    {
        float render = c.renderScale() > 0 ? c.renderScale() : c.scale();
        float zoom = render / c.scale();
        if (render != laidOutAt)
        {
            box->SetFormat(now.style.font, RichEditText::Twips(now.style.size, zoom), now.style.color);
            laidOutAt = render;
        }
        SIZE natural = box->NaturalSize();
        // Room for the next character: the box is a little wider and never narrower than one em.
        LONG em = std::lround(now.style.size * render);
        textPx = { std::max(natural.cx, 0L) + em, std::max(natural.cy, em) };
        box->SetClientRect(ClientOfText());
        RectF doc{ DocumentAt().x, DocumentAt().y, DocumentAt().x + textPx.cx / render, DocumentAt().y + textPx.cy / render };
        float margin = TextBoxVisual::Margin(c.scale()) / render;
        docBox = doc.Inflated(margin, margin);
        c.host->Post("text box", [tools = c.tools, b = docBox] {
            if (ToolInput* t = tools()) t->SetTextBox(b);
        });
        ScheduleDraw();
    }

    void TextEditor::ScheduleDraw()
    {
        if (drawPending || !box) return;
        drawPending = c.ui->Post([this] {
            drawPending = false;
            try
            {
                Draw();
            }
            catch (...)
            {
                LogException("text: draw");
            }
        });
    }

    void TextEditor::Draw()
    {
        if (!box) return;
        float render = laidOutAt > 0 ? laidOutAt : c.scale();
        visual->Draw(*box, { DocumentAt(), textPx, render, c.scale() }, now.style.color, caretOn && GetFocus() == c.window);
        c.commit();
    }

    void TextEditor::ViewChanged()
    {
        if (!box) return;
        float render = c.renderScale() > 0 ? c.renderScale() : c.scale();
        if (render != laidOutAt) Layout();
        else box->SetClientRect(ClientOfText());
    }

    void TextEditor::PlaceCaret(PointF at)
    {
        if (!box) return;
        PointF px = DocumentToViewport(at, c.view(), c.scale());
        POINTL p{ std::lround(px.x), std::lround(px.y) };
        LRESULT index = 0;
        box->Send(EM_CHARFROMPOS, 0, reinterpret_cast<LPARAM>(&p), &index);
        box->Send(EM_SETSEL, static_cast<WPARAM>(index), index);
        Log(std::format("text: a finger tap puts the caret at {}", index));
        if (caretMoved) caretMoved();
        caretOn = true;
        ScheduleDraw();
    }

    void TextEditor::SetStyle(TextStyle const& style)
    {
        if (!box || style == now.style) return;
        now.style = style;
        laidOutAt = 0; // the format again
        Layout();
    }

    bool TextEditor::OnMessage(UINT message, WPARAM wp, LPARAM lp, LRESULT& result)
    {
        if (!box) return false;
        if (message == WM_TIMER)
        {
            if (box->OwnsTimer(wp))
            {
                box->OnTimer(wp);
                return true;
            }
            if (wp != CaretTimer) return false;
            caretOn = !caretOn;
            ScheduleDraw();
            return true;
        }
        if (message == WM_KEYDOWN && wp == VK_ESCAPE)
        {
            End();
            return true;
        }
        if (IsKeyboard(message))
        {
            bool taken = box->Send(message, wp, lp, &result);
            if (message == WM_CHAR || message == WM_KEYDOWN || message == WM_IME_COMPOSITION || message == WM_IME_CHAR)
            {
                caretOn = true; // typing shows the caret
                now.content = box->Text();
                Layout();
                if (caretMoved) caretMoved();
            }
            return taken;
        }
        switch (message)
        {
        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK:
        case WM_MOUSEMOVE:
        case WM_LBUTTONUP:
        {
            POINT p{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            if (!InBox(p) && !box->HasCapture())
            {
                if (message == WM_LBUTTONDOWN || message == WM_LBUTTONDBLCLK)
                {
                    Log("text: a press beside the box ends editing");
                    End();
                }
                return false;
            }
            box->Send(message, wp, lp, &result);
            caretOn = true;
            ScheduleDraw();
            return true;
        }
        case WM_SETCURSOR:
        {
            POINT p;
            GetCursorPos(&p);
            ScreenToClient(c.window, &p);
            if (LOWORD(lp) != HTCLIENT || !InBox(p)) return false;
            box->SetCursorAt(p);
            result = TRUE;
            return true;
        }
        case WM_SETFOCUS:
        case WM_KILLFOCUS:
            Log(std::format("text: the surface {} the keyboard focus (other window {})", message == WM_SETFOCUS ? "got" : "lost",
                reinterpret_cast<void*>(wp)));
            box->Focus(message == WM_SETFOCUS);
            ScheduleDraw();
            return false;
        default:
            return false;
        }
    }

    void TextEditor::End()
    {
        if (!box) return;
        now.content = box->Text();
        KillTimer(c.window, CaretTimer);
        if (c.automation) c.automation->TextBoxEnds();
        box->Focus(false);
        box.reset();
        visual.reset();
        c.commit();
        c.host->Post("text box", [tools = c.tools] {
            if (ToolInput* t = tools()) t->SetTextBox(std::nullopt);
        });
        if (GetFocus() == c.window && focusBefore && IsWindow(focusBefore)) SetFocus(focusBefore);
        focusBefore = nullptr;

        TextHost* texts = c.texts();
        std::optional<TextId> edited = id;
        id.reset();
        if (texts)
        {
            bool empty = Blank(now.content);
            if (!edited)
            {
                if (!empty)
                {
                    TextId added = texts->Add(now);
                    Push(std::make_shared<TextEdit>(*c.ui, c.texts, TextEdit::Kind::Added, added, now, now));
                    Log(std::format("text: added text {} on page {}", added, now.page));
                }
                else Log("text: the new text stayed empty - nothing is created");
            }
            else if (empty)
            {
                uint64_t token = texts->Remove(*edited);
                Push(std::make_shared<TextEdit>(*c.ui, c.texts, TextEdit::Kind::Removed, *edited, original, original, token));
                Log(std::format("text: text {} emptied - removed", *edited));
            }
            else if (!(now == original))
            {
                texts->Change(*edited, now);
                Push(std::make_shared<TextEdit>(*c.ui, c.texts, TextEdit::Kind::Changed, *edited, original, now));
                Log(std::format("text: text {} changed", *edited));
            }
            else Log(std::format("text: text {} unchanged", *edited));
            if (edited) texts->SetEditing(std::nullopt);
        }
        if (editingChanged) editingChanged(false);
    }

    void TextEditor::Push(std::shared_ptr<TextEdit> edit)
    {
        c.host->Post("text edit", [ink = c.ink, edit] {
            if (InkLayer* layer = ink()) layer->Edits().Push(edit);
        });
    }
}
