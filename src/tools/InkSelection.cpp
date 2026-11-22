#include "tools/InkSelection.h"

#include <algorithm>
#include <format>

#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.Numerics.h>

#include "inkpages/Log.h"

namespace inkpages::detail
{
    using winrt::Windows::Foundation::Numerics::float3x2;

    namespace
    {
        float3x2 ToMatrix(Affine const& m) { return { m.m11, m.m12, m.m21, m.m22, m.dx, m.dy }; }
        Affine FromMatrix(float3x2 const& m) { return { m.m11, m.m12, m.m21, m.m22, m.m31, m.m32 }; }
    }

    void InkSelection::Measure()
    {
        bool any = false;
        RectF b{};
        for (InkStroke const& s : Strokes())
        {
            RectF r = BoundsOf(s);
            b = any ? RectF{ std::min(b.left, r.left), std::min(b.top, r.top), std::max(b.right, r.right), std::max(b.bottom, r.bottom) } : r;
            any = true;
        }
        selection.bounds = b;
    }

    StrokeSelection InkSelection::Select(std::vector<PointF> const& polygon)
    {
        Clear();
        if (polygon.size() < 3) return selection;
        std::vector<winrt::Windows::Foundation::Point> points;
        for (PointF p : polygon) points.push_back({ p.x, p.y });
        for (InkStroke const& s : ink.container.GetStrokes()) s.Selected(false);
        ink.container.SelectWithPolyLine(winrt::single_threaded_vector(std::move(points)));
        for (InkStroke const& s : ink.container.GetStrokes())
        {
            if (s.Selected()) selection.ids.insert(s.Id());
            s.Selected(false); // the library keeps the selection itself; Selected is the container's removal mark
        }
        Measure();
        return selection;
    }

    StrokeSelection InkSelection::SelectAll(int page)
    {
        Clear();
        for (InkStroke const& s : ink.StrokesOn(page)) selection.ids.insert(s.Id());
        Measure();
        return selection;
    }

    StrokeSelection InkSelection::SelectAt(PointF point, float radius)
    {
        Clear();
        ink.container.SelectWithLine({ point.x - radius, point.y }, { point.x + radius, point.y });
        for (InkStroke const& stroke : ink.container.GetStrokes())
        {
            if (stroke.Selected()) selection.ids.insert(stroke.Id());
            stroke.Selected(false);
        }
        Measure();
        return selection;
    }

    void InkSelection::Clear()
    {
        EndPreview();
        selection = {};
    }

    void InkSelection::Preview(Affine const& m)
    {
        if (!previewStrokes) previewStrokes = Strokes();
        auto matrix = ToMatrix(m);
        for (InkStroke const& s : *previewStrokes) s.PointTransform(matrix);
    }

    void InkSelection::EndPreview()
    {
        if (!previewStrokes) return;
        auto strokes = std::move(*previewStrokes);
        previewStrokes.reset();
        auto identity = ToMatrix({});
        for (InkStroke const& s : strokes) s.PointTransform(identity);
    }

    void InkSelection::Place(std::vector<InkStroke> const& strokes, StrokeEdit& edit)
    {
        EndPreview();
        selection = {};
        for (InkStroke const& s : strokes)
        {
            for (Placed const& p : ink.Split(s))
            {
                ink.Add(p.stroke, p.page);
                edit.Added(p.stroke, p.page);
                selection.ids.insert(p.stroke.Id());
            }
        }
        Measure();
    }

    std::shared_ptr<StrokeEdit> InkSelection::Commit(Affine const& m)
    {
        auto edit = std::make_shared<StrokeEdit>(ink);
        auto strokes = Strokes();
        EndPreview();
        if (m.IsIdentity() || strokes.empty()) return edit;
        std::vector<InkStroke> moved;
        for (InkStroke const& s : strokes)
        {
            edit->Removed(s, ink.PageOf(s));
            moved.push_back(ink.TransformForDocument(s, m));
            ink.keys.Moved(s.Id(), moved.back().Id(), m); // the app's stroke, moved (cut at a page edge: new ink)
        }
        ink.Remove(strokes);
        Place(moved, *edit);
        std::string pages;
        for (int page : edit->Pages()) pages += std::format(" {}", page);
        Log(std::format("selection: {} strokes moved (pages{})", moved.size(), pages));
        return edit;
    }

    std::shared_ptr<StrokeEdit> InkSelection::Delete()
    {
        EndPreview();
        auto edit = std::make_shared<StrokeEdit>(ink);
        auto strokes = Strokes();
        for (InkStroke const& s : strokes) edit->Removed(s, ink.PageOf(s));
        ink.Remove(strokes);
        selection = {};
        return edit;
    }

    std::shared_ptr<StrokeEdit> InkSelection::Duplicate(Affine const& m)
    {
        auto edit = std::make_shared<StrokeEdit>(ink);
        std::vector<InkStroke> copies;
        for (InkStroke const& s : Strokes()) copies.push_back(ink.TransformForDocument(s, m));
        Place(copies, *edit);
        return edit;
    }

    std::vector<InkStroke> InkSelection::Copies(PointF origin) const
    {
        std::vector<InkStroke> copies;
        for (InkStroke const& s : Strokes()) copies.push_back(ink.TransformForDocument(s, Affine::Translation(-origin.x, -origin.y)));
        return copies;
    }

    std::shared_ptr<StrokeEdit> InkSelection::Add(std::vector<InkStroke> const& relative, PointF at)
    {
        auto edit = std::make_shared<StrokeEdit>(ink);
        std::vector<InkStroke> placed;
        for (InkStroke const& s : relative) placed.push_back(Transformed(s, Affine::Translation(at.x, at.y)));
        Place(placed, *edit);
        return edit;
    }

    bool InkSelection::CopyToClipboard()
    {
        auto strokes = Strokes();
        if (strokes.empty()) return false;
        InkStrokeContainer copy;
        for (InkStroke const& stroke : strokes)
        {
            auto clone = ink.TransformForDocument(stroke, {});
            copy.AddStroke(clone);
            clone.Selected(true);
        }
        copy.CopySelectedToClipboard();
        Log(std::format("selection: {} strokes copied to the clipboard (Windows ink)", strokes.size()));
        return true;
    }

    std::shared_ptr<StrokeEdit> InkSelection::PasteFromClipboard(PointF at)
    {
        auto edit = std::make_shared<StrokeEdit>(ink);
        if (!ink.container.CanPasteFromClipboard()) return edit;
        std::set<uint32_t> before;
        for (InkStroke const& s : ink.container.GetStrokes()) before.insert(s.Id());
        ink.container.PasteFromClipboard({ at.x, at.y });
        std::vector<InkStroke> pasted;
        for (InkStroke const& s : ink.container.GetStrokes())
        {
            if (!before.contains(s.Id())) pasted.push_back(s);
        }
        // Pasted strokes go onto the pages like every stroke: cut at the pages' edges, owned by their page.
        std::vector<InkStroke> copies;
        for (InkStroke const& s : pasted) copies.push_back(Transformed(s, FromMatrix(s.PointTransform())));
        ink.Remove(pasted);
        Place(copies, *edit);
        Log(std::format("selection: {} strokes pasted from the clipboard (Windows ink)", copies.size()));
        return edit;
    }
}
