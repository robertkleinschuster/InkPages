#include "tools/ShapeTool.h"
#include "inkpages/model/PaperAppearance.h"

#include <cmath>

namespace inkpages::detail
{
    using namespace winrt::Windows::UI::Input::Inking;

    InkStroke ShapeStroke(std::vector<PointF> const& outline, Color color, float width, bool highlighter)
    {
        InkDrawingAttributes a;
        a.Color({ color.a, color.r, color.g, color.b });
        a.Size({ width, width });
        a.PenTip(PenTipShape::Circle);
        a.FitToCurve(false);
        a.IgnorePressure(true);
        a.DrawAsHighlighter(highlighter);
        std::vector<InkSample> samples;
        for (PointF p : outline) samples.push_back({ p, 0.5f });
        return MakeStroke(samples, a);
    }

    void ShapeTool::Begin(PointF p, ShapeLook shape)
    {
        look = shape;
        start = p;
        previewColor = ink.DarkPage(ink.layout.PageAt(start)) ?
            PaperAppearance::DarkToolInk(look.color, look.highlighter) : look.color;
        if (look.highlighter) previewColor.a = 0x7F;
        running = true;
    }

    void ShapeTool::Move(PointF p)
    {
        if (!running) return;
        overlay.SetShape(ShapeOutline(look.kind, start, p, look.width), previewColor, look.width);
    }

    std::shared_ptr<StrokeEdit> ShapeTool::End(PointF end)
    {
        auto edit = std::make_shared<StrokeEdit>(ink);
        bool was = running;
        Cancel();
        if (!was || std::abs(end.x - start.x) + std::abs(end.y - start.y) < 2) return edit; // a tap, not a shape
        InkStroke stroke = ShapeStroke(ShapeOutline(look.kind, start, end, look.width), look.color, look.width, look.highlighter);
        for (Placed const& p : ink.Split(stroke))
        {
            ink.Add(p.stroke, p.page);
            edit->Added(p.stroke, p.page);
        }
        return edit;
    }

    void ShapeTool::Cancel()
    {
        running = false;
        overlay.SetShape({}, {}, 1);
    }
}
