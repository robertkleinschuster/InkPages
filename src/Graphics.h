#pragma once

// The devices the writing surface draws with: Direct3D 11 (hardware, WARP as fallback), Direct2D on it, and a
// DirectComposition device created from the Direct2D device - so DComp surfaces hand out ID2D1DeviceContext for drawing.
// The DComp device is requested as IDCompositionDevice (what DirectManipulation's DCompManipulationCompositor needs)
// through DCompositionCreateDevice3: its visuals also implement IDCompositionVisual2/3, which the ink presenter needs
// (measured: a DCompositionCreateDevice device's visual made CreateAndInitializeInkPresenter fail with E_NOINTERFACE).

#include <windows.h>
#include <d2d1_1.h>
#include <d3d11.h>
#include <dcomp.h>

#include <winrt/base.h>

namespace inkpages::detail
{
    struct Graphics
    {
        Graphics();

        winrt::com_ptr<ID3D11Device> d3d;
        winrt::com_ptr<ID2D1Factory1> d2dFactory;
        winrt::com_ptr<ID2D1Device> d2d;
        winrt::com_ptr<IDCompositionDevice> dcomp;
    };
}
