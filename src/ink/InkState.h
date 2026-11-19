#pragma once

// The ink as the library keeps it (Windows' ink host thread only): the presenter's stroke container, which page each
// stroke belongs to, and the page layout. Helpers to make, move and remove strokes.

#include <map>
#include <set>
#include <vector>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.Input.Inking.h>

#include "inkpages/model/PageLayout.h"
#include "inkpages/model/StrokeEraser.h"

namespace inkpages::detail
{
    using winrt::Windows::UI::Input::Inking::InkDrawingAttributes;
    using winrt::Windows::UI::Input::Inking::InkStroke;
    using winrt::Windows::UI::Input::Inking::InkStrokeContainer;

    struct InkState
    {
        InkStrokeContainer container{ nullptr };
        std::map<uint32_t, int> owner; // stroke id -> page
        PageLayout layout;

        /// A stroke belongs to the page nearest to its first point.
        int PageFor(InkStroke const& stroke) const;
        int PageOf(InkStroke const& stroke) const;

        void Add(InkStroke const& stroke, int page);
        /// Removes exactly these strokes from the container (by id).
        void Remove(std::vector<InkStroke> const& strokes);
    };

    /// A stroke through these samples with these attributes (no point transform).
    InkStroke MakeStroke(std::vector<InkSample> const& samples, InkDrawingAttributes const& attributes);

    /// A copy of the stroke moved by (dx, dy): new ink points, the same drawing attributes, no point transform.
    InkStroke Translated(InkStroke const& stroke, float dx, float dy);

    /// The stroke's samples (page units; its point transform is not applied - the library sets none).
    std::vector<InkSample> SamplesOf(InkStroke const& stroke);
}
