#include "tools/PictureEdit.h"

#include "inkpages/Log.h"

namespace inkpages::detail
{
    PictureEdit::PictureEdit(UiThread& ui, std::function<PictureHost*()> host, Kind kind, std::vector<PictureId> pictures,
        std::set<int> pages, Affine m, uint64_t token)
        : ui(ui), host(std::move(host)), kind(kind), pictures(std::move(pictures)), pages(std::move(pages)), m(m),
          token(std::make_shared<uint64_t>(token))
    {
    }

    void PictureEdit::Undo() { Run(true); }
    void PictureEdit::Redo() { Run(false); }

    void PictureEdit::Run(bool undo)
    {
        ui.Post([host = host, kind = kind, pictures = pictures, m = m, token = token, undo] {
            PictureHost* h = host();
            if (!h) return;
            switch (kind)
            {
            case Kind::Transformed:
                if (undo)
                {
                    if (auto inverse = m.Inverse()) h->Transform(pictures, *inverse);
                    else LogError("pictures: a transform without inverse cannot be undone");
                }
                else h->Transform(pictures, m);
                break;
            case Kind::Removed: // undo puts them back, redo takes them again
                if (undo) h->Restore(*token);
                else *token = h->Remove(pictures);
                break;
            case Kind::Added:
                if (undo) *token = h->Remove(pictures);
                else h->Restore(*token);
                break;
            }
        });
    }
}
