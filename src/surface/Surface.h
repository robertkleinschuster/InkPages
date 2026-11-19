#pragma once

// The writing surface's implementation: the child window and the modules it wires together - Composition (visual
// tree, sheets, page layers, overlay), Viewport (DirectManipulation), InkLayer + PageStore + ToolInput (ink thread),
// SelectionController (UI thread). It owns them and hands work between the threads; the logic lives in the modules.

#include <atomic>
#include <memory>
#include <mutex>
#include <set>

#include "ink/InkLayer.h"
#include "ink/PageStore.h"
#include "ink/UiThread.h"
#include "inkpages/WritingSurface.h"
#include "render/Overlay.h"
#include "surface/Composition.h"
#include "surface/SelectionController.h"
#include "surface/Viewport.h"
#include "tools/ToolInput.h"

namespace inkpages::detail
{
    class Surface final : public WritingSurface
    {
    public:
        Surface(HWND parent, SurfaceOptions options);
        ~Surface() override;

        HWND Window() const override { return window; }
        void SetPages(std::vector<PageSpec> pages) override;
        PageLayout const& Layout() const override { return layout; }
        void ZoomTo(float zoom) override;
        void FitWidth() override;
        void ScrollToPage(int page) override;
        View CurrentView() const override { return viewport ? viewport->Current() : View{}; }
        float Scale() const override { return scale; }
        std::optional<int> PageInView() const override { return layout.PageInView(Visible()); }

        IDCompositionVisual* PageLayer(int page) override { return composition->PageLayer(page); }
        IDCompositionDevice* CompositionDevice() override { return composition->Devices().dcomp.get(); }
        float RenderScale() const override { return sheetScale; }
        void Commit() override { composition->Commit(); }
        void SetPictureHost(PictureHost* picturesOfApp) override { pictureHost = picturesOfApp; }
        void InvalidatePage(int page) override;

        void SetTool(ToolState tool) override;
        void SetRuler(bool shown) override;
        void SetProtractor(bool shown) override;

        bool HasSelection() const override { return selection->HasSelection(); }
        void Cut() override { selection->Cut(); }
        void Copy() override { selection->Copy(); }
        void Paste() override { selection->Paste(); }
        void Duplicate() override { selection->Duplicate(); }
        void DeleteSelection() override { selection->Delete(); }
        void SelectAll() override;
        void ClearSelection() override { selection->Clear(); }

        void Undo() override;
        void Redo() override;
        bool CanUndo() const override { return ink->Edits().State().canUndo; }
        bool CanRedo() const override { return ink->Edits().State().canRedo; }
        void MarkSaved() override;
        bool AtSavePoint() const override { return ink->Edits().State().atSavePoint; }

        std::vector<KeyedStroke> StrokesOf(int page) override;
        void Hold(int page, bool hold) override;

        static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wp, LPARAM lp);

    private:
        LRESULT Handle(UINT message, WPARAM wp, LPARAM lp);   // SurfaceWindow.cpp
        void OnPresenterReady();                               // ink thread
        SizeF ClientSize() const;
        RectF Visible() const;
        void Resized();
        void SetScale();
        void Changed(bool settled);
        void DrawOverlay();
        void WhenReady(std::function<void()> action);
        void ZoomToRect(RectF content);

        SurfaceOptions options;
        HWND window = nullptr;
        float scale = 1;
        std::unique_ptr<Composition> composition;
        std::unique_ptr<Viewport> viewport;
        std::unique_ptr<UiThread> ui;
        InkHost host;
        OverlayModel overlay;
        std::atomic<bool> overlayPending = false;
        std::unique_ptr<InkLayer> ink;
        std::unique_ptr<PageStore> pageStore; // ink thread
        std::unique_ptr<ToolInput> tools;    // ink thread
        std::unique_ptr<SelectionController> selection;
        PictureHost* pictureHost = nullptr;
        std::vector<PageSpec> pages;
        PageLayout layout;
        std::set<int> live, held;
        float sheetScale = 0;
        bool presenterReady = false;
        std::vector<std::function<void()>> pending; // until the viewport is ready
        mutable std::mutex viewLock;
        View inkView; // a copy for the ink thread's stroke log
    };
}
