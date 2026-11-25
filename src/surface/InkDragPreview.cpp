#include "surface/InkDragPreview.h"

#include <algorithm>
#include <chrono>
#include <format>
#include <optional>

#include <winrt/Windows.Foundation.Collections.h>

#include "ink/InkHost.h"
#include "ink/InkLayer.h"
#include "inkpages/Log.h"
#include "render/InkDragVisual.h"
#include "ink/UiThread.h"

namespace inkpages::detail
{
    void InkDragPreview::Move(std::set<uint32_t> const& selected, std::vector<PictureId> const& picked, Affine const& m)
    {
        latest = m;
        if (state == State::Idle)
        {
            pictures = picked;
            pending = !picked.empty();
            Request(selected);
        }
        if (state != State::Live) Show();
    }

    void InkDragPreview::Request(std::set<uint32_t> selected)
    {
        state = State::Pending;
        auto token = ++generation;
        auto requested = std::chrono::steady_clock::now();
        // On the UI thread: the textures. A selection without strokes needs no snapshot of the ink.
        auto deliver = [this, token, requested](std::optional<InkDragSnapshot> snapshot) {
            c.ui->Post([this, snapshot = std::move(snapshot), token, requested]() mutable {
                if (token != generation || state != State::Pending) return; // ended, or out of date meanwhile
                float scale = c.pixelsPerUnit();
                if (snapshot || !pictures.empty()) c.visual->Prepare(snapshot ? std::move(*snapshot) : InkDragSnapshot{}, ObjectsOf(scale), scale);
                Log(std::format("selection: drag preview {} after {:.1f} ms", c.visual->Ready() ? "ready" : "unavailable",
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - requested).count()));
                if (c.visual->Ready()) Show();
                else
                {
                    state = State::Live;
                    pending = false;
                    c.moveLive(latest);
                }
            });
        };
        if (selected.empty())
        {
            deliver(std::nullopt);
            return;
        }
        c.host->Post("snapshot drag ink", [this, selected = std::move(selected), deliver] {
            std::optional<InkDragSnapshot> snapshot;
            if (InkLayer* layer = c.ink())
            {
                auto& ink = layer->State();
                InkDragSnapshot taken;
                for (auto const& stroke : ink.container.GetStrokes())
                {
                    // InkStroke is agile (Windows.UI.winmd: MarshalingBehavior Agile): the clones are drawn on the UI thread.
                    if (selected.contains(stroke.Id())) taken.moving.push_back(stroke.Clone());
                    else taken.stationary[ink.PageOf(stroke)].push_back(stroke.Clone());
                }
                if (taken.moving.size() == selected.size()) snapshot = std::move(taken);
            }
            deliver(std::move(snapshot));
        });
    }

    std::optional<InkDragObjects> InkDragPreview::ObjectsOf(float pixelsPerUnit)
    {
        PictureHost* host = c.pictures ? c.pictures() : nullptr;
        if (pictures.empty() || !host) return std::nullopt;
        RectF bounds = host->Bounds(pictures.front());
        for (PictureId id : pictures)
        {
            RectF r = host->Bounds(id);
            bounds = { std::min(bounds.left, r.left), std::min(bounds.top, r.top), std::max(bounds.right, r.right), std::max(bounds.bottom, r.bottom) };
        }
        return InkDragObjects{ bounds, [host, ids = pictures, pixelsPerUnit](ID2D1DeviceContext* dc) {
            return host->DrawLifted(dc, ids, pixelsPerUnit);
        } };
    }

    void InkDragPreview::Show()
    {
        bool switching = state != State::Shown && c.visual->Move({ latest.dx, latest.dy });
        if (switching) state = State::Shown;
        else if (state == State::Shown) c.visual->Move({ latest.dx, latest.dy });
        c.showFrame(latest);
        // DirectComposition changes take effect at Commit (IDCompositionDevice::Commit): the switch from the live to the
        // composited ink must not wait for the frame to change.
        if (switching)
        {
            // Hidden on the pages in the same commit that shows the texture (Lift redraws the pages and commits).
            if (PictureHost* host = c.pictures ? c.pictures() : nullptr; host && !pictures.empty())
            {
                host->Lift(pictures, true);
                lifted = pictures;
                Log(std::format("selection: {} pictures lifted into the drag layer", pictures.size()));
            }
            pending = false;
            c.commit();
        }
    }

    void InkDragPreview::Unlift()
    {
        if (lifted.empty()) return;
        if (PictureHost* host = c.pictures ? c.pictures() : nullptr) host->Lift(lifted, false);
        lifted.clear();
    }

    void InkDragPreview::Invalidate()
    {
        ++generation;
        bool shown = c.visual->Active();
        c.visual->Reset();
        if (state == State::Pending || state == State::Shown)
        {
            Unlift();
            pending = false;
            state = State::Live;
            c.moveLive(latest);
        }
        if (shown) c.commit();
    }

    void InkDragPreview::End()
    {
        ++generation;
        state = State::Idle;
        pending = false;
    }
}
