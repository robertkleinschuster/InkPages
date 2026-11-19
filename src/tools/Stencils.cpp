#include "tools/Stencils.h"

#include <format>

#include <winrt/Windows.Foundation.Numerics.h>

#include "inkpages/Log.h"

namespace inkpages::detail
{
    using namespace winrt::Windows::UI::Input::Inking;

    void Stencils::ShowRuler(bool shown, PointF at)
    {
        if (!ruler) ruler = InkPresenterRuler(presenter); // documented constructor: the stencil of this presenter
        if (shown) ruler.Transform(winrt::Windows::Foundation::Numerics::make_float3x2_translation(at.x, at.y));
        ruler.IsVisible(shown);
        Log(std::format("stencils: ruler {}", shown ? "shown" : "hidden"));
    }

    void Stencils::ShowProtractor(bool shown, PointF at)
    {
        if (!protractor) protractor = InkPresenterProtractor(presenter);
        if (shown) protractor.Transform(winrt::Windows::Foundation::Numerics::make_float3x2_translation(at.x, at.y));
        protractor.IsVisible(shown);
        Log(std::format("stencils: protractor {}", shown ? "shown" : "hidden"));
    }
}
