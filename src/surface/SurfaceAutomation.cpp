#include "surface/SurfaceAutomation.h"

#include <format>

#include "inkpages/Log.h"
#include "text/RichEditBox.h"

namespace inkpages::detail
{
    struct SurfaceAutomation::State
    {
        HWND window = nullptr;
        IRawElementProviderFragment* root = nullptr;           // the root while the surface lives (not owned: no cycle)
        winrt::com_ptr<IRawElementProviderSimple> text;        // the edited box's RichEdit provider, or none
        std::function<RECT()> screen;                           // the box on the screen

        winrt::com_ptr<IRawElementProviderFragment> TextFragment() const
        {
            return text ? text.try_as<IRawElementProviderFragment>() : nullptr;
        }
    };

    namespace
    {
        // The window's own fragment: hosted by the window's default provider (name, bounds, runtime id come from it).
        struct Root : winrt::implements<Root, IRawElementProviderSimple, IRawElementProviderFragment, IRawElementProviderFragmentRoot>
        {
            explicit Root(std::shared_ptr<SurfaceAutomation::State> state) : state(std::move(state)) {}
            std::shared_ptr<SurfaceAutomation::State> state;

            // IRawElementProviderSimple
            HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions* options) noexcept override
            {
                *options = static_cast<ProviderOptions>(ProviderOptions_ServerSideProvider | ProviderOptions_UseComThreading);
                return S_OK;
            }
            HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID, IUnknown** pattern) noexcept override
            {
                *pattern = nullptr;
                return S_OK;
            }
            HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID, VARIANT* value) noexcept override
            {
                VariantInit(value); // the host's (the window's) properties
                return S_OK;
            }
            HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(IRawElementProviderSimple** host) noexcept override
            {
                if (!state->window) return UIA_E_ELEMENTNOTAVAILABLE;
                return UiaHostProviderFromHwnd(state->window, host);
            }

            // IRawElementProviderFragment
            HRESULT STDMETHODCALLTYPE Navigate(NavigateDirection direction, IRawElementProviderFragment** next) noexcept override
            {
                *next = nullptr;
                if (direction == NavigateDirection_FirstChild || direction == NavigateDirection_LastChild)
                {
                    if (auto text = state->TextFragment()) *next = text.detach();
                }
                return S_OK;
            }
            HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY** id) noexcept override
            {
                *id = nullptr; // a hosted root: the window's
                return S_OK;
            }
            HRESULT STDMETHODCALLTYPE get_BoundingRectangle(UiaRect* bounds) noexcept override
            {
                *bounds = {}; // the window's
                return S_OK;
            }
            HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(SAFEARRAY** roots) noexcept override
            {
                *roots = nullptr;
                return S_OK;
            }
            HRESULT STDMETHODCALLTYPE SetFocus() noexcept override { return S_OK; }
            HRESULT STDMETHODCALLTYPE get_FragmentRoot(IRawElementProviderFragmentRoot** root) noexcept override
            {
                *root = static_cast<IRawElementProviderFragmentRoot*>(this);
                AddRef();
                return S_OK;
            }

            // IRawElementProviderFragmentRoot
            HRESULT STDMETHODCALLTYPE ElementProviderFromPoint(double x, double y, IRawElementProviderFragment** element) noexcept override
            {
                *element = nullptr;
                if (auto text = state->TextFragment(); text && state->screen)
                {
                    RECT r = state->screen();
                    if (x >= r.left && x < r.right && y >= r.top && y < r.bottom)
                    {
                        *element = text.detach();
                        return S_OK;
                    }
                }
                *element = static_cast<IRawElementProviderFragment*>(this);
                AddRef();
                return S_OK;
            }
            HRESULT STDMETHODCALLTYPE GetFocus(IRawElementProviderFragment** element) noexcept override
            {
                *element = nullptr;
                if (::GetFocus() == state->window)
                {
                    if (auto text = state->TextFragment()) *element = text.detach();
                }
                return S_OK;
            }
        };

        // The site of the windowless RichEdit: its parent (the root), its runtime id prefix and where it is.
        struct Site : winrt::implements<Site, IRawElementProviderWindowlessSite, IRichEditUiaInformation>
        {
            explicit Site(std::shared_ptr<SurfaceAutomation::State> state) : state(std::move(state)) {}
            std::shared_ptr<SurfaceAutomation::State> state;

            HRESULT STDMETHODCALLTYPE GetAdjacentFragment(NavigateDirection direction, IRawElementProviderFragment** fragment) noexcept override
            {
                *fragment = nullptr;
                if (direction == NavigateDirection_FirstChild || direction == NavigateDirection_LastChild) return E_INVALIDARG;
                if (direction == NavigateDirection_Parent && state->root)
                {
                    *fragment = state->root;
                    state->root->AddRef();
                }
                return S_OK; // no siblings
            }
            HRESULT STDMETHODCALLTYPE GetRuntimeIdPrefix(SAFEARRAY** prefix) noexcept override
            {
                // [doc] UiaAppendRuntimeId and a number unique to this site (the surface has one).
                int id[] = { UiaAppendRuntimeId, 1 };
                SAFEARRAY* array = SafeArrayCreateVector(VT_I4, 0, 2);
                if (!array) return E_OUTOFMEMORY;
                for (LONG i = 0; i < 2; ++i)
                {
                    HRESULT hr = SafeArrayPutElement(array, &i, &id[i]);
                    if (FAILED(hr))
                    {
                        SafeArrayDestroy(array);
                        return LogHr("UIA site: SafeArrayPutElement", hr);
                    }
                }
                *prefix = array;
                return S_OK;
            }
            // IRichEditUiaInformation: RichEdit asks where it is on the screen and whether it shows.
            HRESULT STDMETHODCALLTYPE GetBoundaryRectangle(UiaRect* rect) noexcept override
            {
                RECT r = state->screen ? state->screen() : RECT{};
                *rect = { static_cast<double>(r.left), static_cast<double>(r.top), static_cast<double>(r.right - r.left),
                    static_cast<double>(r.bottom - r.top) };
                return S_OK;
            }
            HRESULT STDMETHODCALLTYPE IsVisible() noexcept override { return state->text ? S_OK : S_FALSE; }
        };
    }

    SurfaceAutomation::SurfaceAutomation(HWND window) : window(window), state(std::make_shared<State>())
    {
        state->window = window;
        auto made = winrt::make_self<Root>(state);
        state->root = made.get();
        root = made.as<IRawElementProviderSimple>();
    }

    SurfaceAutomation::~SurfaceAutomation()
    {
        TextBoxEnds();
        state->root = nullptr;
        state->window = nullptr;
        // [doc] UiaReturnRawElementProvider with nulls: UI Automation lets go of the window's providers.
        UiaReturnRawElementProvider(window, 0, 0, nullptr);
        LogHr("UIA: disconnect the surface", UiaDisconnectProvider(root.get()));
    }

    bool SurfaceAutomation::OnGetObject(WPARAM wp, LPARAM lp, LRESULT& result)
    {
        if (static_cast<long>(lp) != static_cast<long>(UiaRootObjectId)) return false;
        result = UiaReturnRawElementProvider(window, wp, lp, root.get());
        return true;
    }

    void SurfaceAutomation::TextBoxBegins(RichEditBox& box, std::function<RECT()> screen)
    {
        TextBoxEnds();
        state->screen = std::move(screen);
        state->text = box.AutomationProvider(winrt::make<Site>(state).get());
        // The focus moved into the text box: clients (the touch keyboard's auto-invoke, Narrator) hear it.
        HRESULT hr = UiaRaiseAutomationEvent(state->text.get(), UIA_AutomationFocusChangedEventId);
        if (FAILED(hr)) LogHr("UIA: focus changed event", hr);
        Log(std::format("UIA: the text box is the focused element (clients listening: {})", UiaClientsAreListening() != FALSE));
    }

    void SurfaceAutomation::TextBoxEnds()
    {
        if (!state->text) return;
        auto text = std::move(state->text);
        state->text = nullptr;
        state->screen = nullptr;
        HRESULT hr = UiaDisconnectProvider(text.get());
        if (FAILED(hr)) LogHr("UIA: disconnect the text box", hr);
    }
}
