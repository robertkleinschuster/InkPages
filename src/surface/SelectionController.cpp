#include "surface/SelectionController.h"

#include <windowsx.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>

#include <winrt/Windows.Foundation.Collections.h>

#include "ink/InkLayer.h"
#include "inkpages/Log.h"
#include "render/InkDragVisual.h"
#include "tools/PictureEdit.h"
#include "tools/ToolInput.h"

namespace inkpages::detail
{
    namespace
    {
        constexpr UINT_PTR ScrollTimer = 3;
        constexpr UINT ScrollIntervalMs = 16;     // one frame: the scroll moves with the screen
        constexpr float EdgeDip = 32;              // auto-scroll band at the view's edges
        constexpr float DuplicateOffset = 16;      // a duplicate lands beside its original (page units)
    }

    PointF SelectionController::ToDocument(POINT client) const
    {
        return ViewportToDocument({ static_cast<float>(client.x), static_cast<float>(client.y) }, c.viewport->Current(), c.scale());
    }

    std::set<int> SelectionController::PicturePages(std::vector<PictureId> const& ids) const
    {
        std::set<int> pages;
        if (PictureHost* host = c.pictures())
        {
            for (PictureId id : ids)
            {
                RectF b = host->Bounds(id);
                if (auto page = c.layout().NearestPage({ (b.left + b.right) / 2, (b.top + b.bottom) / 2 })) pages.insert(*page);
            }
        }
        return pages;
    }

    std::shared_ptr<Edit> SelectionController::PictureStep(int kind, std::vector<PictureId> ids, Affine m, uint64_t token,
        std::set<int> pages)
    {
        if (ids.empty() || !c.pictures()) return nullptr;
        if (pages.empty()) pages = PicturePages(ids);
        return std::make_shared<PictureEdit>(*c.ui, c.pictures, static_cast<PictureEdit::Kind>(kind), std::move(ids), std::move(pages), m, token);
    }

    void SelectionController::Selected(StrokeSelection const& made, std::vector<PointF> const& polygon)
    {
        std::vector<PictureId> picked;
        PictureHost* host = c.pictures();
        if (host && !polygon.empty())
        {
            RectF b{ polygon[0].x, polygon[0].y, polygon[0].x, polygon[0].y };
            for (PointF p : polygon) b = { std::min(b.left, p.x), std::min(b.top, p.y), std::max(b.right, p.x), std::max(b.bottom, p.y) };
            if (made.Empty() && b.Width() + b.Height() < Tap::MaxDistance(c.viewport->Current().zoom))
            {
                // A tap (with the selection tool or the barrel) on no stroke selects the picture under the pen.
                if (auto picture = host->PictureAt({ (b.left + b.right) / 2, (b.top + b.bottom) / 2 })) picked.push_back(*picture);
            }
            else if (polygon.size() >= 3)
            {
                for (int page : c.layout().PagesIn(b))
                {
                    auto in = host->PicturesIn(page, polygon);
                    picked.insert(picked.end(), in.begin(), in.end());
                }
            }
        }
        Set(made, std::move(picked));
    }

    bool SelectionController::SelectPictureAt(PointF at)
    {
        PictureHost* host = c.pictures();
        auto picture = host ? host->PictureAt(at) : std::nullopt;
        if (!picture) return false;
        Set({}, { *picture });
        return true;
    }

    void SelectionController::Set(StrokeSelection const& made, std::vector<PictureId> picked)
    {
        InvalidateInkPreview();
        strokes = made;
        pictures = std::move(picked);
        frame.reset();
        bool any = !strokes.Empty();
        RectF b = strokes.bounds;
        if (PictureHost* host = c.pictures())
        {
            for (PictureId id : pictures)
            {
                RectF r = host->Bounds(id);
                b = any ? RectF{ std::min(b.left, r.left), std::min(b.top, r.top), std::max(b.right, r.right), std::max(b.bottom, r.bottom) } : r;
                any = true;
            }
        }
        if (any) frame = FrameAround(b);
        Publish();
    }

    void SelectionController::RequestInkPreview()
    {
        drag->ink = Drag::Ink::Pending;
        auto selected = strokes.ids;
        auto generation = ++previewGeneration;
        auto requested = std::chrono::steady_clock::now();
        c.host->Post("snapshot drag ink", [this, selected = std::move(selected), generation, requested] {
            std::optional<InkDragSnapshot> snapshot;
            if (InkLayer* layer = c.ink())
            {
                auto& state = layer->State();
                InkDragSnapshot taken;
                for (auto const& stroke : state.container.GetStrokes())
                {
                    if (selected.contains(stroke.Id())) taken.moving.push_back(stroke.Clone());
                    else taken.stationary[state.PageOf(stroke)].push_back(stroke.Clone());
                }
                if (taken.moving.size() == selected.size()) snapshot = std::move(taken);
            }
            c.ui->Post([this, snapshot = std::move(snapshot), generation, requested]() mutable {
                if (generation != previewGeneration || !drag || drag->ink != Drag::Ink::Pending) return;
                if (snapshot) c.dragInk->Prepare(std::move(*snapshot), c.scale() * c.viewport->Current().zoom);
                Log(std::format("selection: drag ink preview {} after {:.1f} ms", c.dragInk->Ready() ? "ready" : "unavailable",
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - requested).count()));
                if (c.dragInk->Ready()) ShowInkPreview();
                else MoveInkLive();
            });
        });
    }

    // The composited ink follows the drag's latest offset together with the frame.
    void SelectionController::ShowInkPreview()
    {
        Affine m = drag->m;
        if (c.dragInk->Move({ m.dx, m.dy })) drag->ink = Drag::Ink::Shown;
        SelectionFrame shown = *frame;
        shown.orientation = m;
        c.previewFrame(shown);
    }

    // No composited ink (the snapshot failed or its pages changed): the live ink is moved as for any other transform.
    void SelectionController::MoveInkLive()
    {
        drag->ink = Drag::Ink::Live;
        PostPreview(drag->m);
    }

    void SelectionController::InvalidateInkPreview()
    {
        ++previewGeneration;
        bool active = c.dragInk->Active();
        c.dragInk->Reset();
        if (drag && drag->handle == Handle::Move && drag->ink != Drag::Ink::Unused && drag->ink != Drag::Ink::Live) MoveInkLive();
        if (active) c.commit();
    }

    // The frame and its handles keep their size on the screen: page units = DIPs / zoom.
    std::optional<SelectionFrame> SelectionController::FrameAround(RectF bounds) const
    {
        float zoom = c.viewport->Current().zoom;
        return SelectionFrame{ bounds, 4 / zoom, 10 / zoom };
    }

    void SelectionController::Zoomed()
    {
        float zoom = c.viewport->Current().zoom;
        if (!frame || drag || zoom == frameZoom) return;
        frame = FrameAround(frame->bounds);
        Publish(false);
    }

    LPCWSTR SelectionController::CursorAt(PointF document) const
    {
        if (!frame) return nullptr;
        switch (frame->At(document))
        {
        case Handle::Move: return IDC_SIZEALL;
        case Handle::Rotate:
        case Handle::Delete:
        case Handle::Duplicate: return IDC_HAND;
        case Handle::TopLeft:
        case Handle::BottomRight: return IDC_SIZENWSE;
        case Handle::TopRight:
        case Handle::BottomLeft: return IDC_SIZENESW;
        default: return nullptr;
        }
    }

    void SelectionController::Publish(bool log)
    {
        auto f = frame;
        frameZoom = c.viewport->Current().zoom;
        c.host->Post("selection frame", [tools = c.tools, f] {
            if (ToolInput* t = tools()) t->SetFrame(f);
        });
        if (!log) return;
        Log(std::format("selection: {} strokes, {} pictures", strokes.ids.size(), pictures.size()));
        if (c.changed) c.changed();
    }

    void SelectionController::Cleared()
    {
        CancelDrag();
        InvalidateInkPreview();
        strokes = {};
        pictures.clear();
        frame.reset();
        drag.reset();
        Publish();
    }

    void SelectionController::Clear()
    {
        if (!HasSelection()) return;
        c.host->Post("selection clear", [tools = c.tools] {
            if (ToolInput* t = tools()) t->Selection().Clear();
        });
        Cleared();
    }

    bool SelectionController::OnPointer(UINT message, WPARAM wp, LPARAM lp)
    {
        POINT client{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        bool pointer = message == WM_POINTERDOWN || message == WM_POINTERUPDATE || message == WM_POINTERUP;
        if (pointer && !ScreenToClient(c.window, &client))
        {
            LogHr("ScreenToClient (selection)", HRESULT_FROM_WIN32(GetLastError()));
            return false;
        }
        bool down = message == WM_POINTERDOWN || message == WM_LBUTTONDOWN || message == WM_LBUTTONDBLCLK;
        bool up = message == WM_POINTERUP || message == WM_LBUTTONUP;
        bool contact = pointer ? IS_POINTER_INCONTACT_WPARAM(wp) : (wp & MK_LBUTTON) != 0;
        if (down)
        {
            if (c.dragInk->Active() && !drag) return true;
            if (Grabs(ToDocument(client)))
            {
                Press(client);
                if (pointer && drag->handle == Handle::Move)
                {
                    POINTER_PEN_INFO info{};
                    if (!GetPointerPenInfo(GET_POINTERID_WPARAM(wp), &info))
                        LogHr("GetPointerPenInfo (selection)", HRESULT_FROM_WIN32(GetLastError()));
                    else if (info.penFlags & PEN_FLAG_BARREL)
                    {
                        drag->barrel = true;
                    }
                }
                if (!pointer) SetCapture(c.window);
                return true;
            }
            Clear(); // a press beside the selection (the window's pen or the mouse)
            return false;
        }
        if (drag)
        {
            if (pointer && IS_POINTER_CANCELED_WPARAM(wp))
            {
                CancelDrag();
                return true;
            }
            if (up)
            {
                try { Release(client); }
                catch (...)
                {
                    if (!pointer) ReleaseCapture();
                    throw;
                }
                if (!pointer) ReleaseCapture();
            }
            else if (contact) MoveTo(client);
            return true;
        }
        // The window's pen hovering away from the selection (and the edited text box) goes back to the ink.
        PointF at = ToDocument(client);
        bool kept = (frame && frame->At(at) != Handle::None) || (c.keepsPen && c.keepsPen(at));
        if (pointer && !contact && !kept)
        {
            c.host->Post("give back pen", [tools = c.tools] {
                if (ToolInput* t = tools()) t->GiveBackPen();
            });
        }
        return false;
    }

    bool SelectionController::OnKey(WPARAM key)
    {
        if (!HasSelection() || GetKeyState(VK_CONTROL) < 0 || GetKeyState(VK_SHIFT) < 0) return false;
        float unit = 1.f / (c.scale() * c.viewport->Current().zoom);
        PointF offset;
        switch (key)
        {
        case VK_LEFT: offset = { -unit, 0 }; break;
        case VK_RIGHT: offset = { unit, 0 }; break;
        case VK_UP: offset = { 0, -unit }; break;
        case VK_DOWN: offset = { 0, unit }; break;
        default: return false;
        }
        if (!drag && !c.dragInk->Active()) TransformSelection(Affine::Translation(offset.x, offset.y));
        return true;
    }

    void SelectionController::Press(POINT client)
    {
        PointF at = ToDocument(client);
        drag = Drag{ frame->At(at), at, client };
        Log(std::format("selection: drag begins ({})", static_cast<int>(drag->handle)));
    }

    void SelectionController::MoveTo(POINT client)
    {
        PointF at = ToDocument(client);
        drag->client = client;
        drag->farthest = std::max(drag->farthest, std::abs(at.x - drag->start.x) + std::abs(at.y - drag->start.y));
        if (IsSelectionCommand(drag->handle) ||
            (drag->barrel && drag->farthest < Tap::MaxDistance(c.viewport->Current().zoom))) return;
        drag->m = frame->Drag(drag->handle, drag->start, at);
        Affine m = drag->m;
        if (drag->handle == Handle::Move && pictures.empty() && drag->ink != Drag::Ink::Live)
        {
            if (drag->ink == Drag::Ink::Unused) RequestInkPreview();
            ShowInkPreview();
        }
        else PostPreview(m);
        if (PictureHost* host = c.pictures(); host && !pictures.empty()) host->Preview(pictures, m);
        // Near the view's edge the pages scroll (per frame) while the pen holds still too.
        RECT rc;
        GetClientRect(c.window, &rc);
        float band = EdgeDip * c.scale();
        bool edge = client.y < band || client.y > rc.bottom - band || client.x < band || client.x > rc.right - band;
        if (edge) SetTimer(c.window, ScrollTimer, ScrollIntervalMs, nullptr);
        else KillTimer(c.window, ScrollTimer);
    }

    void SelectionController::PostPreview(Affine m)
    {
        if (!drag->preview) drag->preview = std::make_shared<Preview>();
        auto preview = drag->preview;
        {
            std::scoped_lock lock(preview->mutex);
            bool queued = preview->pending.has_value();
            preview->pending = m;
            if (queued) return;
        }
        HRESULT hr = c.host->Post("selection preview", [tools = c.tools, preview] {
            std::optional<Affine> latest;
            {
                std::scoped_lock lock(preview->mutex);
                latest.swap(preview->pending);
            }
            if (latest)
            {
                if (ToolInput* t = tools()) t->PreviewSelection(latest);
            }
        });
        if (FAILED(hr)) DiscardPendingPreview();
    }

    void SelectionController::DiscardPendingPreview()
    {
        if (!drag->preview) return;
        std::scoped_lock lock(drag->preview->mutex);
        drag->preview->pending.reset();
    }

    void SelectionController::OnScrollTimer()
    {
        if (!drag)
        {
            KillTimer(c.window, ScrollTimer);
            return;
        }
        RECT rc;
        GetClientRect(c.window, &rc);
        float band = EdgeDip * c.scale();
        auto speed = [band](float into) { return std::clamp(into / band, 0.f, 1.f) * band / 2; }; // pixels per frame
        float dx = 0, dy = 0;
        if (drag->client.y < band) dy = -speed(band - drag->client.y);
        else if (drag->client.y > rc.bottom - band) dy = speed(drag->client.y - (rc.bottom - band));
        if (drag->client.x < band) dx = -speed(band - drag->client.x);
        else if (drag->client.x > rc.right - band) dx = speed(drag->client.x - (rc.right - band));
        View v = c.viewport->Current();
        SizeF size{ static_cast<float>(rc.right), static_cast<float>(rc.bottom) };
        float left = (-v.dx + dx) / v.zoom, top = (-v.dy + dy) / v.zoom;
        SizeF doc = c.layout().DocumentSize();
        SizeF content{ doc.width * c.scale(), doc.height * c.scale() };
        c.viewport->ZoomToRect(KeepOnContent({ left, top, left + size.width / v.zoom, top + size.height / v.zoom }, content), false);
        MoveTo(drag->client);
    }

    void SelectionController::BeginTouchDrag(POINT client)
    {
        Log("selection: touch drag begins");
        Press(client);
    }

    void SelectionController::UpdateTouchDrag(POINT client)
    {
        if (drag) MoveTo(client);
    }

    void SelectionController::EndTouchDrag(bool cancelled)
    {
        Log(cancelled ? "selection: touch drag cancelled" : "selection: touch drag ends");
        if (!drag) return;
        if (!cancelled)
        {
            Release(drag->client);
            return;
        }
        CancelDrag();
    }

    void SelectionController::CancelDrag()
    {
        if (!drag) return;
        KillTimer(c.window, ScrollTimer);
        DiscardPendingPreview();
        drag.reset();
        ResetPreview();
    }

    void SelectionController::ResetPreview()
    {
        if (c.dragInk->Active())
        {
            c.dragInk->Reset();
            if (frame) c.previewFrame(*frame);
            c.commit();
            Publish(false);
        }
        c.host->Post("selection preview end", [tools = c.tools] {
            if (ToolInput* t = tools()) t->PreviewSelection({});
        });
        if (PictureHost* host = c.pictures(); host && !pictures.empty()) host->Preview(pictures, {});
    }

    void SelectionController::Release(POINT client)
    {
        KillTimer(c.window, ScrollTimer);
        DiscardPendingPreview();
        Drag d = *drag;
        drag.reset();
        PointF at = ToDocument(client);
        float distance = std::max(d.farthest, std::abs(at.x - d.start.x) + std::abs(at.y - d.start.y));
        bool tap = distance < Tap::MaxDistance(c.viewport->Current().zoom);
        if (IsSelectionCommand(d.handle) || (d.barrel && tap))
        {
            if (tap && frame->At(at) == d.handle)
            {
                if (d.handle == Handle::Delete) Delete();
                else Duplicate();
            }
            return;
        }
        if (d.barrel || client.x != d.client.x || client.y != d.client.y)
            d.m = frame->Drag(d.handle, d.start, at);
        if (d.m.IsIdentity())
        {
            ResetPreview();
            return;
        }
        if (PictureHost* host = c.pictures(); host && !pictures.empty())
            ResetPreview();
        if (d.handle == Handle::Move && !d.preview && c.dragInk->Move({ d.m.dx, d.m.dy }))
        {
            SelectionFrame shown = *frame;
            shown.orientation = d.m;
            c.previewFrame(shown);
        }
        TransformSelection(d.m);
    }

    void SelectionController::TransformSelection(Affine m)
    {
        if (PictureHost* host = c.pictures(); host && !pictures.empty()) host->Transform(pictures, m);
        auto pictureEdit = PictureStep(static_cast<int>(PictureEdit::Kind::Transformed), pictures, m, 0, {});
        PushWith([m](ToolInput& t) { return t.Selection().Commit(m); }, pictureEdit, true);
    }

    void SelectionController::PushWith(std::function<std::shared_ptr<Edit>(ToolInput&)> strokesStep,
        std::shared_ptr<Edit> pictureEdit, bool keep)
    {
        auto kept = pictures;
        bool shown = c.dragInk->Active();
        HRESULT hr = c.host->Post("selection edit", [this, strokesStep, pictureEdit, keep, kept, shown] {
            try
            {
                ToolInput* t = c.tools();
                InkLayer* layer = c.ink();
                if (!t || !layer)
                {
                    if (shown) c.ui->Post([this] { ResetPreview(); });
                    return;
                }
                auto edit = std::make_shared<CompositeEdit>();
                edit->Add(strokesStep(*t));
                edit->Add(pictureEdit);
                layer->Edits().Push(edit);
                StrokeSelection now = t->Selection().Current();
                c.ui->Post([this, now, keep, kept] {
                    if (keep) Set(now, kept);
                    else Cleared();
                });
            }
            catch (...)
            {
                if (shown) c.ui->Post([this] { ResetPreview(); });
                throw;
            }
        });
        if (FAILED(hr) && shown) ResetPreview();
    }

    void SelectionController::Delete()
    {
        if (!HasSelection()) return;
        std::shared_ptr<Edit> pictureEdit;
        if (PictureHost* host = c.pictures(); host && !pictures.empty())
        {
            auto pages = PicturePages(pictures); // before they leave their pages
            uint64_t token = host->Remove(pictures);
            pictureEdit = PictureStep(static_cast<int>(PictureEdit::Kind::Removed), pictures, {}, token, pages);
        }
        pictures.clear();
        PushWith([](ToolInput& t) { return t.Selection().Delete(); }, pictureEdit, false);
    }

    void SelectionController::Duplicate()
    {
        if (!HasSelection()) return;
        Affine m = Affine::Translation(DuplicateOffset, DuplicateOffset);
        if (PictureHost* host = c.pictures(); host && !pictures.empty())
        {
            pictures = host->Duplicate(pictures, m);
        }
        auto pictureEdit = PictureStep(static_cast<int>(PictureEdit::Kind::Added), pictures, {}, 0, {});
        PushWith([m](ToolInput& t) { return t.Selection().Duplicate(m); }, pictureEdit, true);
    }

    bool SelectionController::Copy()
    {
        if (!HasSelection() || !frame) return false;
        if (c.surface && c.surface->copyToClipboard)
        {
            // Waits for the strokes' copies: the app gets the clip before anything else happens to the selection (Cut
            // deletes it right after; the app reads its pictures while they are still on their pages).
            RectF bounds = frame->bounds;
            auto copies = std::make_shared<std::vector<InkStroke>>(); // held by the work (Run's timeout: it may run later)
            c.host->Run("selection copy", [copies, bounds, tools = c.tools] {
                if (ToolInput* t = tools()) *copies = t->Selection().Copies({ bounds.left, bounds.top });
            });
            return c.surface->copyToClipboard(Clip{ *copies, pictures, bounds });
        }
        c.host->Post("selection copy", [tools = c.tools] {
            if (ToolInput* t = tools()) t->Selection().CopyToClipboard();
        });
        return true;
    }

    void SelectionController::Cut()
    {
        if (!Copy())
        {
            Log("selection: cut - nothing copied, the selection stays");
            return;
        }
        Delete();
    }

    void SelectionController::Paste()
    {
        RECT rc;
        GetClientRect(c.window, &rc);
        PointF at = ToDocument({ rc.right / 2, rc.bottom / 2 });
        if (c.surface && c.surface->deferPaste && c.surface->deferPaste(at)) return;
        PasteAt(at);
    }

    void SelectionController::PasteAt(PointF at)
    {
        Clear();
        if (c.surface && c.surface->pasteFromClipboard)
        {
            if (auto clip = c.surface->pasteFromClipboard(at))
            {
                pictures = clip->pictures;
                auto pictureEdit = PictureStep(static_cast<int>(PictureEdit::Kind::Added), pictures, {}, 0, {});
                auto relative = clip->strokes;
                PushWith([relative, at](ToolInput& t) { return t.Selection().Add(relative, at); }, pictureEdit, true);
                return;
            }
            // Nothing of the app's on the clipboard: Windows ink (ink copied in other apps) is pasted as before.
        }
        PushWith([at](ToolInput& t) { return t.Selection().PasteFromClipboard(at); }, nullptr, true);
    }

    void SelectionController::SelectAll(int page)
    {
        c.host->Post("select all", [this, page, tools = c.tools, ui = c.ui] {
            ToolInput* t = tools();
            if (!t) return;
            StrokeSelection all = t->Selection().SelectAll(page);
            ui->Post([this, all, page] {
                PictureHost* host = c.pictures();
                Set(all, host ? host->PicturesOn(page) : std::vector<PictureId>{});
            });
        });
    }
}
