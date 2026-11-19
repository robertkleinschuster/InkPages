#include "InkLayer.h"

#include <format>

#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.Numerics.h>
#include <winrt/Windows.UI.Core.h>

#include "inkpages/Log.h"

namespace inkpages::detail
{
    using namespace winrt::Windows::UI::Input::Inking;
    using winrt::Windows::UI::Core::CoreCursor;
    using winrt::Windows::UI::Core::CoreCursorType;
    using winrt::Windows::UI::Core::CoreInputDeviceTypes;

    namespace
    {
        struct WorkItem : winrt::implements<WorkItem, IInkHostWorkItem>
        {
            WorkItem(char const* name, std::function<void()> work) : name(name), work(std::move(work)) {}
            HRESULT STDMETHODCALLTYPE Invoke() noexcept override
            {
                try
                {
                    work();
                }
                catch (...)
                {
                    LogException(std::format("ink thread: {}", name));
                }
                return S_OK;
            }
            char const* name;
            std::function<void()> work;
        };

        // The presenter asks the app to commit DirectComposition: the device belongs to the UI thread.
        struct CommitHandler : winrt::implements<CommitHandler, IInkCommitRequestHandler>
        {
            CommitHandler(HWND window, UINT message) : window(window), message(message) {}
            HRESULT STDMETHODCALLTYPE OnCommitRequested() noexcept override
            {
                PostMessageW(window, message, 0, 0);
                return S_OK;
            }
            HWND window;
            UINT message;
        };
    }

    InkStroke Translated(InkStroke const& stroke, float dx, float dy)
    {
        std::vector<InkPoint> points;
        for (InkPoint const& p : stroke.GetInkPoints())
        {
            auto pos = p.Position();
            points.emplace_back(winrt::Windows::Foundation::Point{ pos.X + dx, pos.Y + dy }, p.Pressure(), p.TiltX(),
                p.TiltY(), p.Timestamp());
        }
        InkStrokeBuilder builder;
        builder.SetDefaultDrawingAttributes(stroke.DrawingAttributes());
        return builder.CreateStrokeFromInkPoints(winrt::single_threaded_vector(std::move(points)),
            winrt::Windows::Foundation::Numerics::float3x2::identity());
    }

    InkLayer::InkLayer(HWND window, UINT commitMessage, UINT readyMessage, IDCompositionVisual* root, SizeF document, bool mouseInks)
        : window(window), commitMessage(commitMessage), readyMessage(readyMessage)
    {
        host = winrt::create_instance<IInkDesktopHost>(__uuidof(InkDesktopHost), CLSCTX_INPROC_SERVER);
        winrt::com_ptr<IDCompositionVisual> visual;
        visual.copy_from(root);
        OnInkThread("create presenter", [this, visual, document, mouseInks] { Create(visual.get(), document, mouseInks); });
    }

    InkLayer::~InkLayer()
    {
        // Release the presenter's objects on their own thread, then the host (which ends that thread).
        HANDLE done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        OnInkThread("shutdown", [this, done] {
            input = nullptr;
            presenter = nullptr;
            desktop = nullptr;
            SetEvent(done);
        });
        if (WaitForSingleObject(done, 3000) != WAIT_OBJECT_0) LogError("ink: shutdown did not finish within 3 s");
        CloseHandle(done);
        host = nullptr;
    }

    void InkLayer::OnInkThread(char const* name, std::function<void()> work)
    {
        HRESULT hr = host->QueueWorkItem(winrt::make<WorkItem>(name, std::move(work)).get());
        if (FAILED(hr)) LogHr(std::format("QueueWorkItem({})", name), hr);
    }

    void InkLayer::Create(IDCompositionVisual* root, SizeF document, bool mouseInks)
    {
        LARGE_INTEGER frequency, start, end;
        QueryPerformanceFrequency(&frequency);
        QueryPerformanceCounter(&start);
        winrt::check_hresult(LogHr("CreateAndInitializeInkPresenter", host->CreateAndInitializeInkPresenter(root,
            document.width, document.height, __uuidof(IInkPresenterDesktop), desktop.put_void())));
        winrt::check_hresult(desktop->SetCommitRequestHandler(winrt::make<CommitHandler>(window, commitMessage).get()));
        presenter = desktop.as<InkPresenter>();
        presenter.InputDeviceTypes(mouseInks ? CoreInputDeviceTypes::Pen | CoreInputDeviceTypes::Mouse : CoreInputDeviceTypes::Pen);
        presenter.StrokesCollected([this](InkPresenter const&, InkStrokesCollectedEventArgs const& e) {
            try
            {
                for (InkStroke const& stroke : e.Strokes())
                {
                    auto points = stroke.GetInkPoints();
                    auto first = points.GetAt(0).Position();
                    int page = layout.NearestPage({ first.X, first.Y }).value_or(0);
                    owner[stroke.Id()] = page;
                    if (collected) collected(page, stroke);
                }
            }
            catch (...)
            {
                LogException("ink: strokes collected");
            }
        });
        input = Core::CoreInkIndependentInputSource::Create(presenter);
        QueryPerformanceCounter(&end);
        Log(std::format("ink: presenter over {:.0f}x{:.0f} DIP created in {:.1f} ms", document.width, document.height,
            (end.QuadPart - start.QuadPart) * 1000.0 / frequency.QuadPart));
        PostMessageW(window, readyMessage, 0, 0);
    }

    void InkLayer::SetLayout(PageLayout newLayout)
    {
        OnInkThread("layout", [this, newLayout] {
            layout = newLayout;
            if (desktop) LogHr("IInkPresenterDesktop::SetSize", desktop->SetSize(layout.DocumentSize().width, layout.DocumentSize().height));
            PostMessageW(window, commitMessage, 0, 0);
        });
    }

    void InkLayer::SetPen(winrt::Windows::UI::Color color, float size)
    {
        OnInkThread("pen", [this, color, size] {
            InkDrawingAttributes attributes;
            attributes.Color(color);
            attributes.Size({ size, size });
            attributes.IgnorePressure(false);
            attributes.FitToCurve(false); // polylines: few-point shapes stay angular
            presenter.UpdateDefaultDrawingAttributes(attributes);
        });
    }

    void InkLayer::SetCursor(UINT resourceId)
    {
        OnInkThread("cursor", [this, resourceId] {
            cursor = resourceId;
            if (cursorActive) ApplyCursor();
        });
    }

    void InkLayer::ActivateCursor()
    {
        OnInkThread("activate cursor", [this] {
            cursorActive = true;
            ApplyCursor();
        });
    }

    void InkLayer::ApplyCursor()
    {
        if (!input) return;
        input.PointerCursor(cursor ? CoreCursor(CoreCursorType::Custom, cursor) : nullptr);
        Log(std::format("ink: pen cursor {}", cursor));
    }

    void InkLayer::SetLivePages(std::set<int> const& pages)
    {
        OnInkThread("live pages", [this, pages] {
            if (!presenter) return;
            for (int page : std::set<int>(live))
            {
                if (!pages.contains(page)) Release(page);
            }
            for (int page : pages)
            {
                if (!live.contains(page)) Restore(page);
            }
        });
    }

    void InkLayer::Release(int page)
    {
        RectF r = layout.Page(page);
        std::vector<InkStroke> strokes;
        auto container = presenter.StrokeContainer();
        for (InkStroke const& stroke : container.GetStrokes())
        {
            auto it = owner.find(stroke.Id());
            bool mine = it != owner.end() && it->second == page;
            stroke.Selected(mine);
            if (mine)
            {
                strokes.push_back(Translated(stroke, -r.left, -r.top));
                owner.erase(it);
            }
        }
        container.DeleteSelected();
        live.erase(page);
        Log(std::format("ink: page {} released ({} strokes)", page, strokes.size()));
        if (store) store(page, std::move(strokes));
    }

    void InkLayer::Restore(int page)
    {
        live.insert(page);
        if (!load) return;
        RectF r = layout.Page(page);
        auto container = presenter.StrokeContainer();
        std::vector<InkStroke> strokes = load(page);
        for (InkStroke const& stroke : strokes)
        {
            InkStroke placed = Translated(stroke, r.left, r.top);
            container.AddStroke(placed);
            owner[placed.Id()] = page;
        }
        if (!strokes.empty()) Log(std::format("ink: page {} restored ({} strokes)", page, strokes.size()));
    }
}
