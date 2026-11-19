#include "render/Graphics.h"

#include <dxgi.h>

#include "inkpages/Log.h"

namespace inkpages::detail
{
    Graphics::Graphics()
    {
        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, nullptr, 0, D3D11_SDK_VERSION,
            d3d.put(), nullptr, nullptr);
        if (FAILED(hr))
        {
            LogHr("D3D11CreateDevice(hardware)", hr);
            winrt::check_hresult(LogHr("D3D11CreateDevice(WARP)", D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
                flags, nullptr, 0, D3D11_SDK_VERSION, d3d.put(), nullptr, nullptr)));
        }
        D2D1_FACTORY_OPTIONS options{};
        winrt::check_hresult(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &options,
            d2dFactory.put_void()));
        winrt::check_hresult(d2dFactory->CreateDevice(d3d.as<IDXGIDevice>().get(), d2d.put()));
        winrt::check_hresult(LogHr("DCompositionCreateDevice3",
            DCompositionCreateDevice3(d2d.get(), __uuidof(IDCompositionDevice), dcomp.put_void())));
    }
}
