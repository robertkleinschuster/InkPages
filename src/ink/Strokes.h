#pragma once

// Making strokes from points (InkStrokeBuilder: one per thread, it is not agile) and reading them back.

#include <vector>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.Input.Inking.h>

#include "inkpages/model/Geometry.h"
#include "inkpages/model/StrokeEraser.h"

namespace inkpages::detail
{
    using winrt::Windows::UI::Input::Inking::InkDrawingAttributes;
    using winrt::Windows::UI::Input::Inking::InkStroke;

    /// A stroke through these samples with these attributes (no point transform).
    InkStroke MakeStroke(std::vector<InkSample> const& samples, InkDrawingAttributes const& attributes);

    /// The stroke's points mapped by `m` (as a new stroke; the pen's width stays - a scaled stroke keeps its width).
    InkStroke Transformed(InkStroke const& stroke, Affine const& m);

    /// The stroke's samples (its point transform not applied).
    std::vector<InkSample> SamplesOf(InkStroke const& stroke);

    RectF BoundsOf(InkStroke const& stroke);
}
