#include "tools/TextEdit.h"

namespace inkpages::detail
{
    TextEdit::TextEdit(UiThread& ui, std::function<TextHost*()> host, Kind kind, TextId id, Text before, Text after,
        uint64_t token)
        : ui(ui), host(std::move(host)), kind(kind), id(id), before(std::move(before)), after(std::move(after)),
          token(std::make_shared<uint64_t>(token))
    {
    }

    void TextEdit::RemapPages(PageMap const& map)
    {
        // A text of a removed page goes with its page's edits (UndoHistory drops them); the others follow.
        if (int page = map(before.page); page >= 0) before.page = page;
        if (int page = map(after.page); page >= 0) after.page = page;
    }

    void TextEdit::Run(bool undo)
    {
        ui.Post([host = host, kind = kind, id = id, before = before, after = after, token = token, undo] {
            TextHost* h = host();
            if (!h) return;
            switch (kind)
            {
            case Kind::Added: // undo takes it off, redo puts it back
                if (undo) *token = h->Remove(id);
                else h->Restore(*token);
                break;
            case Kind::Changed:
                h->Change(id, undo ? before : after);
                break;
            case Kind::Removed:
                if (undo) h->Restore(*token);
                else *token = h->Remove(id);
                break;
            }
        });
    }
}
