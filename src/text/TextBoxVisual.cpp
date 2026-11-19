#include "text/TextBoxVisual.h"

#include <d2d1_1helper.h>

#include <algorithm>

#include "inkpages/Log.h"

namespace inkpages::detail
{
    TextBoxVisual::TextBoxVisual(Graphics& graphics, IDCompositionVisual* parent) : graphics(graphics)
    {
        layer.copy_from(parent);
        winrt::check_hresult(graphics.dcomp->CreateVisual(visual.put()));
        winrt::check_hresult(layer->AddVisual(visual.get(), FALSE, nullptr));
    }

    TextBoxVisual::~TextBoxVisual()
    {
        LogHr("text box: RemoveVisual", layer->RemoveVisual(visual.get()));
    }

    void TextBoxVisual::Draw(RichEditBox& box, Placement const& place, Color caretColor, bool caretOn)
    {
        int m = Margin(place.dpiScale);
        SIZE size{ place.text.cx + 2 * m, place.text.cy + 2 * m };
        if (!surface || size.cx != surfaceSize.cx || size.cy != surfaceSize.cy)
        {
            surface = nullptr;
            winrt::check_hresult(graphics.dcomp->CreateSurface(size.cx, size.cy, DXGI_FORMAT_B8G8R8A8_UNORM,
                DXGI_ALPHA_MODE_PREMULTIPLIED, surface.put()));
            winrt::check_hresult(visual->SetContent(surface.get()));
            surfaceSize = size;
        }
        // Surface pixels -> document: the visual sits in the content (document units, shown at the zoom's pixels).
        float unit = 1 / place.pixelsPerUnit;
        D2D1_MATRIX_3X2_F m3{ unit, 0, 0, unit, place.at.x - m * unit, place.at.y - m * unit };
        winrt::check_hresult(visual->SetTransform(m3));

        winrt::com_ptr<ID2D1DeviceContext> dc;
        POINT offset{};
        winrt::check_hresult(surface->BeginDraw(nullptr, __uuidof(ID2D1DeviceContext), dc.put_void(), &offset));
        // RichEdit takes its font scale from the render target's DPI (the window's) and lays out in the pixels of the
        // bounds (measured in the probe).
        float dpi = 96 * place.dpiScale;
        dc->SetDpi(dpi, dpi);
        dc->SetTransform(D2D1::Matrix3x2F::Translation(offset.x / place.dpiScale, offset.y / place.dpiScale));
        dc->Clear(D2D1::ColorF(0, 0, 0, 0));
        box.Draw(dc.get(), RECTL{ m, m, m + place.text.cx, m + place.text.cy });

        // The frame and the caret in pixels.
        dc->SetDpi(96, 96);
        dc->SetTransform(D2D1::Matrix3x2F::Translation(static_cast<float>(offset.x), static_cast<float>(offset.y)));
        winrt::com_ptr<ID2D1SolidColorBrush> brush;
        winrt::check_hresult(dc->CreateSolidColorBrush(D2D1::ColorF(0.45f, 0.45f, 0.45f), brush.put()));
        float line = std::max(1.f, place.dpiScale);
        dc->DrawRectangle({ line / 2, line / 2, size.cx - line / 2, size.cy - line / 2 }, brush.get(), line);
        RichEditBox::Caret caret = box.CaretNow();
        if (caretOn && caret.shown)
        {
            RECT client = box.ClientRect();
            float x = static_cast<float>(caret.at.x - client.left + m), y = static_cast<float>(caret.at.y - client.top + m);
            brush->SetColor(D2D1::ColorF(caretColor.r / 255.f, caretColor.g / 255.f, caretColor.b / 255.f));
            dc->FillRectangle({ x, y, x + std::max<float>(static_cast<float>(caret.size.cx), line), y + caret.size.cy }, brush.get());
        }
        winrt::check_hresult(surface->EndDraw());
    }
}
