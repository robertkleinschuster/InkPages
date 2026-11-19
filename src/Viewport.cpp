#include "Viewport.h"

#include <format>

#include "inkpages/Log.h"

namespace inkpages::detail
{
    namespace
    {
        char const* Name(DIRECTMANIPULATION_STATUS s)
        {
            switch (s)
            {
            case DIRECTMANIPULATION_BUILDING: return "BUILDING";
            case DIRECTMANIPULATION_ENABLED: return "ENABLED";
            case DIRECTMANIPULATION_DISABLED: return "DISABLED";
            case DIRECTMANIPULATION_RUNNING: return "RUNNING";
            case DIRECTMANIPULATION_INERTIA: return "INERTIA";
            case DIRECTMANIPULATION_READY: return "READY";
            case DIRECTMANIPULATION_SUSPENDED: return "SUSPENDED";
            default: return "?";
            }
        }

        std::string Describe(View v) { return std::format("zoom {:.3f} offset {:.1f},{:.1f} px", v.zoom, v.dx, v.dy); }
    }

    // DirectManipulation calls back on the UI thread (the thread that created the viewport).
    struct Viewport::Handler : winrt::implements<Handler, IDirectManipulationViewportEventHandler>
    {
        explicit Handler(Viewport* owner) : owner(owner) {}
        Viewport* owner;

        HRESULT STDMETHODCALLTYPE OnViewportStatusChanged(IDirectManipulationViewport*, DIRECTMANIPULATION_STATUS now,
            DIRECTMANIPULATION_STATUS before) noexcept override
        {
            try
            {
                owner->status = now;
                Log(std::format("viewport: {} -> {} ({})", Name(before), Name(now), Describe(owner->current)));
                if (owner->onStatus) owner->onStatus(now == DIRECTMANIPULATION_READY);
            }
            catch (...)
            {
                LogException("viewport status");
            }
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE OnViewportUpdated(IDirectManipulationViewport*) noexcept override { return S_OK; }

        HRESULT STDMETHODCALLTYPE OnContentUpdated(IDirectManipulationViewport*, IDirectManipulationContent* content) noexcept override
        {
            try
            {
                float m[6]{};
                winrt::check_hresult(content->GetContentTransform(m, 6));
                owner->current = { m[0], m[4], m[5] };
                if (owner->updates++ % 30 == 0) Log(std::format("viewport: content {}", Describe(owner->current)));
                if (owner->onChanged) owner->onChanged(owner->current);
            }
            catch (...)
            {
                LogException("viewport content updated");
            }
            return S_OK;
        }
    };

    Viewport::Viewport(HWND window, IDCompositionDevice* device, IDCompositionVisual* parent, IDCompositionVisual* child,
        float minZoom, float maxZoom)
        : window(window)
    {
        using winrt::check_hresult;
        manager = winrt::create_instance<IDirectManipulationManager>(CLSID_DirectManipulationManager, CLSCTX_INPROC_SERVER);
        winrt::com_ptr<IDirectManipulationUpdateManager> updateManager;
        check_hresult(manager->GetUpdateManager(IID_PPV_ARGS(updateManager.put())));
        compositor = winrt::create_instance<IDirectManipulationCompositor>(CLSID_DCompManipulationCompositor, CLSCTX_INPROC_SERVER);
        check_hresult(compositor->SetUpdateManager(updateManager.get()));
        auto frameInfo = compositor.as<IDirectManipulationFrameInfoProvider>();
        check_hresult(manager->CreateViewport(frameInfo.get(), window, IID_PPV_ARGS(viewport.put())));
        check_hresult(viewport->ActivateConfiguration(DIRECTMANIPULATION_CONFIGURATION_INTERACTION |
            DIRECTMANIPULATION_CONFIGURATION_TRANSLATION_X | DIRECTMANIPULATION_CONFIGURATION_TRANSLATION_Y |
            DIRECTMANIPULATION_CONFIGURATION_TRANSLATION_INERTIA | DIRECTMANIPULATION_CONFIGURATION_RAILS_X |
            DIRECTMANIPULATION_CONFIGURATION_RAILS_Y | DIRECTMANIPULATION_CONFIGURATION_SCALING |
            DIRECTMANIPULATION_CONFIGURATION_SCALING_INERTIA));
        check_hresult(viewport->AddEventHandler(window, winrt::make<Handler>(this).get(), &cookie));
        winrt::com_ptr<IDirectManipulationPrimaryContent> primary;
        check_hresult(viewport->GetPrimaryContent(IID_PPV_ARGS(primary.put())));
        content = primary.as<IDirectManipulationContent>();
        check_hresult(primary->SetZoomBoundaries(minZoom, maxZoom));
        check_hresult(primary->SetHorizontalAlignment(DIRECTMANIPULATION_HORIZONTALALIGNMENT_CENTER |
            DIRECTMANIPULATION_HORIZONTALALIGNMENT_UNLOCKCENTER));
        check_hresult(manager->Activate(window));
        check_hresult(compositor->AddContent(content.get(), device, parent, child));
        check_hresult(viewport->Enable());
        Log("viewport: DirectManipulation ready");
    }

    Viewport::~Viewport()
    {
        try
        {
            if (viewport)
            {
                LogHr("viewport Stop", viewport->Stop());
                viewport->RemoveEventHandler(cookie);
                LogHr("viewport Abandon", viewport->Abandon());
            }
            if (compositor && content) compositor->RemoveContent(content.get());
            if (manager) manager->Deactivate(window);
        }
        catch (...)
        {
            LogException("viewport shutdown");
        }
    }

    void Viewport::SetViewportRect(RECT pixels)
    {
        winrt::check_hresult(viewport->SetViewportRect(&pixels));
    }

    void Viewport::SetContentSize(SIZE pixels)
    {
        RECT rect{ 0, 0, pixels.cx, pixels.cy };
        winrt::check_hresult(content->SetContentRect(&rect));
    }

    bool Viewport::OnPointer(WPARAM wp)
    {
        UINT32 id = GET_POINTERID_WPARAM(wp);
        POINTER_INPUT_TYPE type = PT_POINTER;
        if (!GetPointerType(id, &type))
        {
            LogHr("GetPointerType", HRESULT_FROM_WIN32(GetLastError()));
            return false;
        }
        if (type != PT_TOUCH && type != PT_TOUCHPAD) return false;
        HRESULT hr = viewport->SetContact(id);
        if (FAILED(hr)) LogHr(std::format("SetContact({} {})", type == PT_TOUCH ? "touch" : "touchpad", id), hr);
        return SUCCEEDED(hr);
    }

    bool Viewport::OnWheel(MSG const& message)
    {
        HRESULT contact = viewport->SetContact(DIRECTMANIPULATION_MOUSEFOCUS);
        if (FAILED(contact))
        {
            LogHr("SetContact(mouse focus)", contact);
            return false;
        }
        BOOL handled = FALSE;
        HRESULT hr = manager->ProcessInput(&message, &handled);
        if (FAILED(hr)) LogHr("ProcessInput(wheel)", hr);
        HRESULT release = viewport->ReleaseContact(DIRECTMANIPULATION_MOUSEFOCUS);
        if (FAILED(release)) LogHr("ReleaseContact(mouse focus)", release);
        return SUCCEEDED(hr) && handled;
    }

    void Viewport::ZoomToRect(RectF r, bool animate)
    {
        HRESULT hr = viewport->ZoomToRect(r.left, r.top, r.right, r.bottom, animate);
        if (FAILED(hr)) LogHr("ZoomToRect", hr);
    }
}
