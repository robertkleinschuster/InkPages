#include "tools/Stencils.h"

#include <format>

#include "inkpages/Log.h"

namespace inkpages::detail
{
    using namespace winrt::Windows::UI::Input::Inking;

    void Stencils::ShowRuler(bool shown)
    {
        if (!ruler) ruler = InkPresenterRuler(presenter); // documented constructor: the stencil of this presenter
        ruler.IsVisible(shown);
        Log(std::format("stencils: ruler {}", shown ? "shown" : "hidden"));
    }

    void Stencils::ShowProtractor(bool shown)
    {
        if (!protractor) protractor = InkPresenterProtractor(presenter);
        protractor.IsVisible(shown);
        Log(std::format("stencils: protractor {}", shown ? "shown" : "hidden"));
    }
}
