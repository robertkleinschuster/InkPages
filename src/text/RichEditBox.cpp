#include "text/RichEditBox.h"

#include <algorithm>
#include <format>

#include <imm.h>

#include <winrt/base.h>

#include "inkpages/Log.h"

namespace inkpages::detail
{
    namespace
    {
        // msftedit.dll exports the text services' factory and the interface ids (textserv.h declares them extern).
        struct Msftedit
        {
            HMODULE module = nullptr;
            PCreateTextServices create = nullptr;
            IID textServices2{}, textHost{}, textHost2{};

            static Msftedit const& Get()
            {
                static Msftedit const loaded = [] {
                    Msftedit m;
                    m.module = LoadLibraryW(L"msftedit.dll");
                    winrt::check_bool(m.module != nullptr);
                    m.create = reinterpret_cast<PCreateTextServices>(GetProcAddress(m.module, "CreateTextServices"));
                    auto ts2 = reinterpret_cast<IID const*>(GetProcAddress(m.module, "IID_ITextServices2"));
                    auto th = reinterpret_cast<IID const*>(GetProcAddress(m.module, "IID_ITextHost"));
                    auto th2 = reinterpret_cast<IID const*>(GetProcAddress(m.module, "IID_ITextHost2"));
                    if (!m.create || !ts2 || !th || !th2) throw winrt::hresult_error(E_NOINTERFACE, L"msftedit exports");
                    m.textServices2 = *ts2;
                    m.textHost = *th;
                    m.textHost2 = *th2;
                    return m;
                }();
                return loaded;
            }
        };

        constexpr DWORD PropertyBits = TXTBIT_MULTILINE | TXTBIT_D2DDWRITE; // plain text, no wrapping
    }

    // The host RichEdit calls back (the box's lifetime: not reference counted).
    class RichEditBox::Host final : public ITextHost2
    {
    public:
        Host(RichEditBox& box) : box(box)
        {
            cf.cbSize = sizeof(cf);
            cf.dwMask = CFM_FACE | CFM_SIZE | CFM_COLOR | CFM_CHARSET | CFM_BOLD | CFM_ITALIC | CFM_UNDERLINE |
                CFM_STRIKEOUT | CFM_OFFSET | CFM_PROTECTED;
            cf.yHeight = 12 * 20;
            cf.bCharSet = DEFAULT_CHARSET;
            wcscpy_s(cf.szFaceName, L"Arial");
            pf.cbSize = sizeof(pf);
            pf.dwMask = PFM_ALL;
            pf.wAlignment = PFA_LEFT;
            pf.cTabCount = 1;
            pf.rgxTabs[0] = lDefaultTab;
        }

        RichEditBox& box;
        CHARFORMAT2W cf{};
        PARAFORMAT2 pf{};
        Caret caret;
        bool capture = false;

        void Changed() { if (box.invalidated) box.invalidated(); }

        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override
        {
            auto const& m = Msftedit::Get();
            if (riid == IID_IUnknown || riid == m.textHost || riid == m.textHost2)
            {
                *ppv = static_cast<ITextHost2*>(this);
                return S_OK;
            }
            *ppv = nullptr;
            return E_NOINTERFACE;
        }
        ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
        ULONG STDMETHODCALLTYPE Release() override { return 1; }

        // ITextHost
        HDC TxGetDC() override { return GetDC(box.window); }
        INT TxReleaseDC(HDC hdc) override { return ReleaseDC(box.window, hdc); }
        BOOL TxShowScrollBar(INT, BOOL) override { return FALSE; }
        BOOL TxEnableScrollBar(INT, INT) override { return FALSE; }
        BOOL TxSetScrollRange(INT, LONG, INT, BOOL) override { return FALSE; }
        BOOL TxSetScrollPos(INT, INT, BOOL) override { return FALSE; }
        void TxInvalidateRect(LPCRECT, BOOL) override { Changed(); }
        void TxViewChange(BOOL) override { Changed(); }
        BOOL TxCreateCaret(HBITMAP, INT w, INT h) override
        {
            caret.size = { std::max(w, 1), h };
            // The system caret too (hidden under the composition): Magnifier and Narrator follow it.
            if (!CreateCaret(box.window, nullptr, caret.size.cx, h)) LogHr("CreateCaret", HRESULT_FROM_WIN32(GetLastError()));
            return TRUE;
        }
        BOOL TxShowCaret(BOOL show) override
        {
            caret.shown = show != FALSE;
            if (show) ShowCaret(box.window);
            else HideCaret(box.window);
            Changed();
            return TRUE;
        }
        BOOL TxSetCaretPos(INT x, INT y) override
        {
            caret.at = { x, y };
            SetCaretPos(x, y);
            Changed();
            return TRUE;
        }
        BOOL TxSetTimer(UINT id, UINT timeout) override { return SetTimer(box.window, box.timerBase + id, timeout, nullptr) != 0; }
        void TxKillTimer(UINT id) override { KillTimer(box.window, box.timerBase + id); }
        void TxScrollWindowEx(INT, INT, LPCRECT, LPCRECT, HRGN, LPRECT, UINT) override { Changed(); }
        void TxSetCapture(BOOL c) override
        {
            capture = c != FALSE;
            if (capture) SetCapture(box.window);
            else if (GetCapture() == box.window) ReleaseCapture();
        }
        void TxSetFocus() override { SetFocus(box.window); }
        void TxSetCursor(HCURSOR h, BOOL) override { SetCursor(h); }
        BOOL TxScreenToClient(LPPOINT p) override { return ScreenToClient(box.window, p); }
        BOOL TxClientToScreen(LPPOINT p) override { return ClientToScreen(box.window, p); }
        HRESULT TxActivate(LONG* old) override { if (old) *old = 0; return S_OK; }
        HRESULT TxDeactivate(LONG) override { return S_OK; }
        HRESULT TxGetClientRect(LPRECT prc) override { *prc = box.client; return S_OK; }
        HRESULT TxGetViewInset(LPRECT prc) override { *prc = {}; return S_OK; }
        HRESULT TxGetCharFormat(const CHARFORMATW** ppCF) override { *ppCF = reinterpret_cast<CHARFORMATW*>(&cf); return S_OK; }
        HRESULT TxGetParaFormat(const PARAFORMAT** ppPF) override { *ppPF = &pf; return S_OK; }
        COLORREF TxGetSysColor(int i) override { return GetSysColor(i); }
        HRESULT TxGetBackStyle(TXTBACKSTYLE* s) override { *s = TXTBACK_TRANSPARENT; return S_OK; }
        HRESULT TxGetMaxLength(DWORD* l) override { *l = INFINITE; return S_OK; }
        HRESULT TxGetScrollBars(DWORD* s) override { *s = 0; return S_OK; }
        HRESULT TxGetPasswordChar(TCHAR*) override { return S_FALSE; }
        HRESULT TxGetAcceleratorPos(LONG* cp) override { *cp = -1; return S_OK; }
        HRESULT TxGetExtent(LPSIZEL) override { return E_NOTIMPL; }
        HRESULT OnTxCharFormatChange(const CHARFORMATW*) override { return S_OK; }
        HRESULT OnTxParaFormatChange(const PARAFORMAT*) override { return S_OK; }
        HRESULT TxGetPropertyBits(DWORD mask, DWORD* bits) override { *bits = PropertyBits & mask; return S_OK; }
        HRESULT TxNotify(DWORD, void*) override { return S_OK; }
        HIMC TxImmGetContext() override { return ImmGetContext(box.window); }
        void TxImmReleaseContext(HIMC h) override { ImmReleaseContext(box.window, h); }
        HRESULT TxGetSelectionBarWidth(LONG* w) override { *w = 0; return S_OK; }
        // ITextHost2
        BOOL TxIsDoubleClickPending() override
        {
            MSG m;
            return PeekMessageW(&m, box.window, WM_LBUTTONDBLCLK, WM_LBUTTONDBLCLK, PM_NOREMOVE | PM_NOYIELD);
        }
        HRESULT TxGetWindow(HWND* h) override { *h = box.window; return S_OK; }
        HRESULT TxSetForegroundWindow() override { SetForegroundWindow(box.window); return S_OK; }
        HPALETTE TxGetPalette() override { return nullptr; }
        HRESULT TxGetEastAsianFlags(LONG* f) override { *f = 0; return S_OK; }
        HCURSOR TxSetCursor2(HCURSOR h, BOOL) override { return SetCursor(h); }
        void TxFreeTextServicesNotification() override {}
        HRESULT TxGetEditStyle(DWORD, DWORD* d) override { *d = 0; return S_OK; }
        HRESULT TxGetWindowStyles(DWORD* s, DWORD* ex) override { *s = WS_VISIBLE; *ex = 0; return S_OK; }
        HRESULT TxShowDropCaret(BOOL, HDC, LPCRECT) override { return S_OK; }
        HRESULT TxDestroyCaret() override
        {
            caret.shown = false;
            DestroyCaret();
            Changed();
            return S_OK;
        }
        HRESULT TxGetHorzExtent(LONG* l) override { *l = 0; return S_OK; }
    };

    RichEditBox::RichEditBox(HWND window, UINT_PTR timerBase) : window(window), timerBase(timerBase)
    {
        host = std::make_unique<Host>(*this);
        auto const& m = Msftedit::Get();
        winrt::com_ptr<IUnknown> unknown;
        winrt::check_hresult(m.create(nullptr, host.get(), unknown.put()));
        winrt::check_hresult(unknown->QueryInterface(m.textServices2, reinterpret_cast<void**>(&services)));
        client = { 0, 0, 1, 1 };
        winrt::check_hresult(services->OnTxInPlaceActivate(&client));
        // The host's property bits (multi-line, plain, D2D) take effect when announced.
        winrt::check_hresult(services->OnTxPropertyBitsChange(TXTBIT_MULTILINE | TXTBIT_RICHTEXT | TXTBIT_WORDWRAP | TXTBIT_D2DDWRITE, PropertyBits));
    }

    RichEditBox::~RichEditBox()
    {
        if (!services) return;
        LogHr("RichEdit OnTxInPlaceDeactivate", services->OnTxInPlaceDeactivate());
        services->Release();
    }

    void RichEditBox::SetText(std::wstring const& text)
    {
        winrt::check_hresult(services->TxSetText(RichEditText::ToRichEdit(text).c_str()));
    }

    std::wstring RichEditBox::Text() const
    {
        BSTR text = nullptr;
        winrt::check_hresult(services->TxGetText(&text));
        std::wstring result = text ? std::wstring(text, SysStringLen(text)) : std::wstring();
        SysFreeString(text);
        return RichEditText::FromRichEdit(result);
    }

    void RichEditBox::SetFormat(std::wstring const& face, long twips, Color color)
    {
        wcsncpy_s(host->cf.szFaceName, face.c_str(), _TRUNCATE);
        host->cf.yHeight = twips;
        host->cf.crTextColor = RGB(color.r, color.g, color.b);
        winrt::check_hresult(services->OnTxPropertyBitsChange(TXTBIT_CHARFORMATCHANGE, TXTBIT_CHARFORMATCHANGE));
        Send(EM_SETCHARFORMAT, SCF_ALL, reinterpret_cast<LPARAM>(&host->cf)); // the text there too
    }

    void RichEditBox::SetClientRect(RECT rect)
    {
        if (EqualRect(&rect, &client)) return;
        client = rect;
        winrt::check_hresult(services->OnTxPropertyBitsChange(TXTBIT_CLIENTRECTCHANGE, TXTBIT_CLIENTRECTCHANGE));
    }

    SIZE RichEditBox::NaturalSize() const
    {
        HDC dc = GetDC(window);
        LONG width = 0x7FFF, height = 0x7FFF; // in: the space offered; out: what the text takes
        // The extent (HIMETRIC) is the client's own size: no zoom.
        int dpi = static_cast<int>(GetDpiForWindow(window));
        SIZEL extent{ MulDiv(width, 2540, dpi), MulDiv(height, 2540, dpi) };
        HRESULT hr = services->TxGetNaturalSize(DVASPECT_CONTENT, dc, nullptr, nullptr, TXTNS_FITTOCONTENT, &extent, &width, &height);
        ReleaseDC(window, dc);
        if (FAILED(hr))
        {
            LogHr("RichEdit TxGetNaturalSize", hr);
            return { client.right - client.left, client.bottom - client.top };
        }
        return { width, height };
    }

    void RichEditBox::Draw(ID2D1DeviceContext* dc, RECTL bounds)
    {
        HRESULT hr = services->TxDrawD2D(dc, &bounds, nullptr, TXTVIEW_ACTIVE);
        if (FAILED(hr)) LogHr("RichEdit TxDrawD2D", hr);
    }

    void RichEditBox::Focus(bool focused)
    {
        Send(focused ? WM_SETFOCUS : WM_KILLFOCUS, 0, 0);
    }

    bool RichEditBox::Send(UINT message, WPARAM wp, LPARAM lp, LRESULT* result)
    {
        LRESULT r = 0;
        HRESULT hr = services->TxSendMessage(message, wp, lp, &r);
        if (result) *result = r;
        if (FAILED(hr) && hr != E_NOTIMPL) LogHr(std::format("RichEdit TxSendMessage 0x{:04X}", message), hr);
        return hr == S_OK;
    }

    void RichEditBox::SetCursorAt(POINT at)
    {
        HRESULT hr = services->OnTxSetCursor(DVASPECT_CONTENT, -1, nullptr, nullptr, nullptr, nullptr, nullptr, at.x, at.y);
        if (FAILED(hr)) LogHr("RichEdit OnTxSetCursor", hr);
    }

    void RichEditBox::OnTimer(UINT_PTR id)
    {
        Send(WM_TIMER, id - timerBase, 0);
    }

    winrt::com_ptr<IRawElementProviderSimple> RichEditBox::AutomationProvider(IRawElementProviderWindowlessSite* site)
    {
        winrt::com_ptr<IRicheditWindowlessAccessibility> accessibility;
        winrt::check_hresult(services->QueryInterface(__uuidof(IRicheditWindowlessAccessibility), accessibility.put_void()));
        winrt::com_ptr<IRawElementProviderSimple> provider;
        winrt::check_hresult(accessibility->CreateProvider(site, provider.put()));
        return provider;
    }

    RichEditBox::Caret RichEditBox::CaretNow() const { return host->caret; }
    bool RichEditBox::HasCapture() const { return host->capture; }
}
