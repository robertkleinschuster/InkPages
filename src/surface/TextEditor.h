#pragma once

// The Text tool's editing (UI thread): one text at a time in a windowless RichEdit on the page. A pen tap with the Text
// tool edits the text there or starts a new one; a tap beside the box, Escape, another tool or any command that changes
// the document ends it. The pen hovering over the box goes to the window (ToolInput), so its taps and drags reach the
// RichEdit as the mouse messages Windows makes from them; the keyboard follows the focus, which the surface's window
// takes while editing (and gives back afterwards). At the end the app's TextHost gets the result and the history one
// TextEdit. The box is laid out at the settled zoom: RichEdit's font is the text's size at that zoom (RichEditText).

#include <windows.h>

#include <functional>
#include <memory>
#include <optional>

#include "ink/InkHost.h"
#include "ink/UiThread.h"
#include "inkpages/Texts.h"
#include "inkpages/model/PageLayout.h"
#include "inkpages/model/ViewMath.h"
#include "text/RichEditBox.h"
#include "text/TextBoxVisual.h"

namespace inkpages::detail
{
    class InkLayer;
    class ToolInput;

    struct TextEditorContext
    {
        HWND window = nullptr;
        Graphics* graphics = nullptr;
        IDCompositionVisual* layer = nullptr;        // the composition's text layer
        InkHost* host = nullptr;
        UiThread* ui = nullptr;
        std::function<InkLayer*()> ink;              // ink thread
        std::function<ToolInput*()> tools;           // ink thread
        std::function<TextHost*()> texts;            // the app's (null: no texts)
        std::function<View()> view;
        std::function<float()> scale;                // pixels per DIP
        std::function<float()> renderScale;          // pixels per page unit at the settled zoom
        std::function<PageLayout const&()> layout;
        std::function<void()> commit;                // the composition
    };

    class TextEditor
    {
    public:
        explicit TextEditor(TextEditorContext context) : c(std::move(context)) {}
        ~TextEditor();
        TextEditor(TextEditor const&) = delete;
        TextEditor& operator=(TextEditor const&) = delete;

        bool Editing() const { return box != nullptr; }
        /// The Text tool's pen tap (document point): ends editing when a text is edited; otherwise edits the text
        /// there, or starts a new one in `style`.
        void Tap(PointF at, TextStyle const& style);
        /// Ends editing: the result goes to the app and the history.
        void End();
        /// The toolbar's text style changed: the edited text takes it.
        void SetStyle(TextStyle const& style);
        /// The edited box (with its frame) covers a document point.
        bool Contains(PointF document) const { return box && docBox.Contains(document); }
        /// The window's messages while editing (keyboard, IME, the mouse and the pen's mouse messages in the box,
        /// timers, cursor, focus); true when the editor took it (`result` for the window procedure).
        bool OnMessage(UINT message, WPARAM wp, LPARAM lp, LRESULT& result);
        /// The view moved or zoomed: the box's place in the window follows, at a new settled zoom it is laid out anew.
        void ViewChanged();

        std::function<void(bool editing)> editingChanged;

    private:
        void Begin(std::optional<TextId> id, Text text, PointF caretAt);
        void Layout();
        void ScheduleDraw();
        void Draw();
        RECT ClientOfText() const;
        PointF DocumentAt() const;
        bool InBox(POINT client) const;
        void Push(std::shared_ptr<class TextEdit> edit);

        TextEditorContext c;
        std::unique_ptr<RichEditBox> box;
        std::unique_ptr<TextBoxVisual> visual;
        std::optional<TextId> id;
        Text original, now;
        HWND focusBefore = nullptr;
        SIZE textPx{};
        RectF docBox;         // the box with its frame (document)
        float laidOutAt = 0;  // the render scale of the layout
        bool caretOn = true;
        bool drawPending = false;
    };
}
