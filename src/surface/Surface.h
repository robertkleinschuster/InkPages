#pragma once

// The writing surface's implementation: the child window and the modules it wires together - Composition (visual
// tree, sheets, page layers, overlay), Viewport (DirectManipulation), InkLayer + PageStore + ToolInput (ink thread),
// SelectionController, MouseSelect, TouchTaps (UI thread). It owns them and hands work between the threads; the logic lives in the modules.

#include <atomic>
#include <memory>
#include <mutex>
#include <set>

#include <winrt/Windows.UI.ViewManagement.h>

#include "ink/InkLayer.h"
#include "ink/PageStore.h"
#include "ink/UiThread.h"
#include "inkpages/WritingSurface.h"
#include "render/Overlay.h"
#include "render/PageMark.h"
#include "surface/Composition.h"
#include "surface/MouseSelect.h"
#include "surface/SelectionController.h"
#include "surface/SurfaceAutomation.h"
#include "surface/TextEditor.h"
#include "surface/TouchTaps.h"
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
        void InsertPages(int index, std::vector<PageSpec> added) override;
        void RemovePage(int index) override;
        void MovePage(int from, int to) override;
        PageLayout const& Layout() const override { return layout; }
        void ZoomTo(float zoom) override;
        void FitWidth() override;
        void ScrollToPage(int page) override;
        void ScrollTo(int page, PointF point) override;
        void RevealRect(int page, RectF area) override;
        View CurrentView() const override { return viewport ? viewport->Current() : View{}; }
        float Scale() const override { return scale; }
        std::optional<int> PageInView() const override { return layout.PageInView(Visible()); }
        void MarkCurrentPage(bool mark) override;

        IDCompositionVisual* PageLayer(int page) override { return composition->PageLayer(page); }
        IDCompositionDevice* CompositionDevice() override { return composition->Devices().dcomp.get(); }
        float RenderScale() const override { return sheetScale; }
        void Commit() override { composition->Commit(); }
        void SetPictureHost(PictureHost* picturesOfApp) override { pictureHost = picturesOfApp; }
        void SetTextHost(TextHost* textsOfApp) override;
        bool EditingText() const override { return textEditor && textEditor->Editing(); }
        void EndTextEditing() override;
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

        std::optional<std::vector<KeyedStroke>> StrokesOf(int page) override;
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
        void ShowPageMark();
        void ReadAccent();
        void WhenReady(std::function<void()> action);
        void ZoomToRect(RectF content);
        void Rearrange(std::vector<PageSpec> specs, PageMap const& map);
        void BuildLayout(std::vector<PageSpec> specs, std::vector<Paper>& papers);
        PointF ToDocument(POINT client) const;
        void Tapped(PointF document, PointerKind kind);
        // The app's mouse gestures (mouseClaims/mouseGesture): its claim where nothing of the library's lies, ink at
        // a press (ink thread, waited for), a gesture to the app in the press page's coordinates.
        bool AppClaims(PointF document) const;
        bool InkAt(PointF document);
        void AppGesture(MousePhase phase, PointF press, PointF document, int clicks);

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
        std::unique_ptr<MouseSelect> mouse;
        std::unique_ptr<TouchTaps> touchTaps;
        std::unique_ptr<SurfaceAutomation> automation;
        std::unique_ptr<PageMark> pageMark;
        bool markCurrentPage = false;
        Color accent{ 0xFF, 0x00, 0x5F, 0xB8 };
        winrt::Windows::UI::ViewManagement::UISettings uiSettings{ nullptr };
        winrt::event_token accentChanged{};
        std::unique_ptr<TextEditor> textEditor;
        TextHost* textHost = nullptr;
        TextStyle textStyle; // the Text tool's
        Tool currentTool = Tool::Pen;
        // Page operations issued (UI thread) and applied on the ink thread: a tool's result made before the ink thread
        // applied every issued one refers to the old pages and is dropped.
        std::atomic<unsigned> pagesGeneration = 0, inkPagesGeneration = 0;
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
