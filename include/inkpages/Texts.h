#pragma once

// The app's texts (Xournal++'s <text>: plain text with newlines, one font, size and color, placed by its top left). The
// Text tool edits one at a time: a pen tap or mouse click on a page starts a new text there (however long the pen
// rests; a drag is no text), one on a text edits it, one beside the box ends editing (so does Escape, another tool, undo/redo, a page operation). While a text is edited the library
// shows it (a windowless RichEdit in the page's composition); the app leaves it out of its own drawing. At the end the
// library hands the result back (Add, Change, or Remove when it was emptied) and records it as one undo step; an empty
// new text is nothing.
// Selecting, moving, scaling, duplicating, copying and deleting texts is the selection's, through PictureHost: texts are
// among the app's objects there (ids from the same space; scaling a text scales its size - the app's choice). All calls
// on the UI thread.

#include <cstdint>
#include <optional>
#include <string>

#include "inkpages/Pictures.h"
#include "inkpages/model/Text.h"

namespace inkpages
{
    using TextId = PictureId;

    struct Text
    {
        int page = 0;
        PointF at;            // the top left, page coordinates
        std::wstring content; // \n between lines
        TextStyle style;
        friend bool operator==(Text const&, Text const&) = default;
    };

    class TextHost
    {
    public:
        virtual ~TextHost() = default;

        /// The topmost text under a document point (the Text tool's tap edits it), if any.
        virtual std::optional<TextId> TextAt(PointF document) = 0;
        virtual Text Get(TextId text) = 0;
        /// The text being edited (none: editing ended): the app does not draw it meanwhile (InvalidatePage).
        virtual void SetEditing(std::optional<TextId> text) = 0;

        /// Editing ended with a new text (returns its id), a changed text, or an emptied one (taken off; the token puts
        /// it back for undo). Undo and redo call these too (Change with the text before/after; Remove/Restore).
        virtual TextId Add(Text const& text) = 0;
        virtual void Change(TextId id, Text const& text) = 0;
        virtual uint64_t Remove(TextId id) = 0;
        virtual void Restore(uint64_t token) = 0;
    };
}
