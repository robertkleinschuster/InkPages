#include "InkState.h"

#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.Numerics.h>

namespace inkpages::detail
{
    using namespace winrt::Windows::UI::Input::Inking;

    int InkState::PageFor(InkStroke const& stroke) const
    {
        auto points = stroke.GetInkPoints();
        if (points.Size() == 0) return 0;
        auto first = points.GetAt(0).Position();
        return layout.NearestPage({ first.X, first.Y }).value_or(0);
    }

    int InkState::PageOf(InkStroke const& stroke) const
    {
        auto it = owner.find(stroke.Id());
        return it != owner.end() ? it->second : PageFor(stroke);
    }

    void InkState::Add(InkStroke const& stroke, int page)
    {
        container.AddStroke(stroke);
        owner[stroke.Id()] = page;
    }

    void InkState::Remove(std::vector<InkStroke> const& strokes)
    {
        if (strokes.empty()) return;
        std::set<uint32_t> ids;
        for (InkStroke const& s : strokes) ids.insert(s.Id());
        for (InkStroke const& s : container.GetStrokes()) s.Selected(ids.contains(s.Id()));
        container.DeleteSelected();
        for (uint32_t id : ids) owner.erase(id);
    }

    InkStroke MakeStroke(std::vector<InkSample> const& samples, InkDrawingAttributes const& attributes)
    {
        std::vector<InkPoint> points;
        points.reserve(samples.size());
        for (InkSample const& s : samples)
        {
            points.emplace_back(winrt::Windows::Foundation::Point{ s.position.x, s.position.y }, s.pressure);
        }
        // InkStrokeBuilder is not agile: one per thread.
        thread_local InkStrokeBuilder builder;
        builder.SetDefaultDrawingAttributes(attributes);
        InkStroke stroke = builder.CreateStrokeFromInkPoints(winrt::single_threaded_vector(std::move(points)),
            winrt::Windows::Foundation::Numerics::float3x2::identity());
        stroke.DrawingAttributes(attributes);
        return stroke;
    }

    InkStroke Translated(InkStroke const& stroke, float dx, float dy)
    {
        std::vector<InkPoint> points;
        for (InkPoint const& p : stroke.GetInkPoints())
        {
            auto pos = p.Position();
            points.emplace_back(winrt::Windows::Foundation::Point{ pos.X + dx, pos.Y + dy }, p.Pressure(), p.TiltX(),
                p.TiltY(), p.Timestamp());
        }
        thread_local InkStrokeBuilder builder;
        builder.SetDefaultDrawingAttributes(stroke.DrawingAttributes());
        InkStroke moved = builder.CreateStrokeFromInkPoints(winrt::single_threaded_vector(std::move(points)),
            winrt::Windows::Foundation::Numerics::float3x2::identity());
        moved.DrawingAttributes(stroke.DrawingAttributes());
        return moved;
    }

    std::vector<InkSample> SamplesOf(InkStroke const& stroke)
    {
        std::vector<InkSample> samples;
        for (InkPoint const& p : stroke.GetInkPoints())
        {
            auto pos = p.Position();
            samples.push_back({ { pos.X, pos.Y }, p.Pressure() });
        }
        return samples;
    }
}
