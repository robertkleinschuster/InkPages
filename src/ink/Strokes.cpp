#include "ink/Strokes.h"

#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.Numerics.h>

namespace inkpages::detail
{
    using namespace winrt::Windows::UI::Input::Inking;

    namespace
    {
        InkStroke Build(std::vector<InkPoint> points, InkDrawingAttributes const& attributes)
        {
            thread_local InkStrokeBuilder builder; // not agile: one per thread
            builder.SetDefaultDrawingAttributes(attributes);
            InkStroke stroke = builder.CreateStrokeFromInkPoints(winrt::single_threaded_vector(std::move(points)),
                winrt::Windows::Foundation::Numerics::float3x2::identity());
            stroke.DrawingAttributes(attributes);
            return stroke;
        }
    }

    InkStroke MakeStroke(std::vector<InkSample> const& samples, InkDrawingAttributes const& attributes)
    {
        std::vector<InkPoint> points;
        points.reserve(samples.size());
        for (InkSample const& s : samples) points.emplace_back(winrt::Windows::Foundation::Point{ s.position.x, s.position.y }, s.pressure);
        return Build(std::move(points), attributes);
    }

    InkStroke Transformed(InkStroke const& stroke, Affine const& m)
    {
        std::vector<InkPoint> points;
        for (InkPoint const& p : stroke.GetInkPoints())
        {
            PointF at = m.Apply(PointF{ p.Position().X, p.Position().Y });
            points.emplace_back(winrt::Windows::Foundation::Point{ at.x, at.y }, p.Pressure(), p.TiltX(), p.TiltY(), p.Timestamp());
        }
        return Build(std::move(points), stroke.DrawingAttributes());
    }

    std::vector<InkSample> SamplesOf(InkStroke const& stroke)
    {
        std::vector<InkSample> samples;
        for (InkPoint const& p : stroke.GetInkPoints()) samples.push_back({ { p.Position().X, p.Position().Y }, p.Pressure() });
        return samples;
    }

    RectF BoundsOf(InkStroke const& stroke)
    {
        auto b = stroke.BoundingRect(); // includes the stroke's width
        return { b.X, b.Y, b.X + b.Width, b.Y + b.Height };
    }
}
