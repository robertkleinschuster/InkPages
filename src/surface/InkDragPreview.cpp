#include "surface/InkDragPreview.h"

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
    void InkDragPreview::Move(std::set<uint32_t> const& selected, Affine const& m)
    {
        latest = m;
        if (state == State::Idle) Request(selected);
        if (state != State::Live) Show();
    }

    void InkDragPreview::Request(std::set<uint32_t> selected)
    {
        state = State::Pending;
        auto token = ++generation;
        auto requested = std::chrono::steady_clock::now();
        c.host->Post("snapshot drag ink", [this, selected = std::move(selected), token, requested] {
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
            c.ui->Post([this, snapshot = std::move(snapshot), token, requested]() mutable {
                if (token != generation || state != State::Pending) return; // ended, or out of date meanwhile
                if (snapshot) c.visual->Prepare(std::move(*snapshot), c.pixelsPerUnit());
                Log(std::format("selection: drag ink preview {} after {:.1f} ms", c.visual->Ready() ? "ready" : "unavailable",
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - requested).count()));
                if (c.visual->Ready()) Show();
                else
                {
                    state = State::Live;
                    c.moveLive(latest);
                }
            });
        });
    }

    void InkDragPreview::Show()
    {
        bool switching = state != State::Shown && c.visual->Move({ latest.dx, latest.dy });
        if (switching) state = State::Shown;
        else if (state == State::Shown) c.visual->Move({ latest.dx, latest.dy });
        c.showFrame(latest);
        // DirectComposition changes take effect at Commit (IDCompositionDevice::Commit): the switch from the live to the
        // composited ink must not wait for the frame to change.
        if (switching) c.commit();
    }

    void InkDragPreview::Invalidate()
    {
        ++generation;
        bool shown = c.visual->Active();
        c.visual->Reset();
        if (state == State::Pending || state == State::Shown)
        {
            state = State::Live;
            c.moveLive(latest);
        }
        if (shown) c.commit();
    }

    void InkDragPreview::End()
    {
        ++generation;
        state = State::Idle;
    }
}
