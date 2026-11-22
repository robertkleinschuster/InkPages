#pragma once

// The tools' feedback on the sheets, below the ink (a visual above the ink would sit in the pen's way): the exact
// eraser's circle, the lasso or rectangle being drawn, the selection's frame with its corner handles, a shape being
// dragged out. Tools set the content from any thread (OverlayModel); the UI thread draws it with Direct2D into a
// surface covering just that content (OverlayLayer).

#include <dcomp.h>

#include <functional>
#include <cstdint>
#include <mutex>
#include <optional>
#include <vector>

#include <winrt/base.h>

#include "inkpages/model/Geometry.h"

namespace inkpages::detail
{
    struct Graphics;

    struct LaserSegment
    {
        PointF from, to;
        float width;
        uint64_t expires, duration;
    };

    struct OverlayContent
    {
        std::optional<PointF> circleAt;
        float circleRadius = 0;
        bool squareEraser = false;
        std::vector<PointF> lasso;
        std::optional<SelectionFrame> frame;
        std::vector<PointF> shape;
        Color shapeColor;
        float shapeWidth = 1;
        std::vector<LaserSegment> laser;
        uint64_t now = 0;
    };

    class OverlayModel
    {
    public:
        /// Any thread: the content changed (the owner has it drawn on the UI thread).
        std::function<void()> changed;

        void SetEraser(std::optional<PointF> at, float extent, bool square);
        void SetLasso(std::vector<PointF> points);
        void SetFrame(std::optional<SelectionFrame> frame);
        void SetShape(std::vector<PointF> points, Color color, float width);
        PointF AddLaser(PointF from, std::vector<PointF> const& points, float width, int seconds);
        void ClearLaser();
        OverlayContent Get();

    private:
        template <typename F> void Change(F&& f);
        mutable std::mutex lock;
        OverlayContent content;
    };

    class OverlayLayer
    {
    public:
        explicit OverlayLayer(Graphics& graphics);
        IDCompositionVisual* Visual() const { return visual.get(); }
        /// Draws the content at `renderScale` pixels per page unit.
        void Draw(OverlayContent const& content, float renderScale);

    private:
        Graphics& graphics;
        winrt::com_ptr<IDCompositionVisual> visual;
        winrt::com_ptr<IDCompositionSurface> surface;
        std::optional<SelectionFrame> drawnFrame;
        PointF drawnOrigin{};
        float drawnScale = 0;
        float frameRenderScale = 0;
    };
}
