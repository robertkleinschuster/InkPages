#pragma once

// What the pen does on the presenter (ink thread): the active tool decides the presenter's mode (Inking / Erasing /
// None), unprocessed input goes to the tool that runs on it (exact eraser, lasso/rectangle selection, shapes; the
// barrel button always lassos), the presenter's hover takes the pen's eraser end (exact erasing) and gives the pen to
// the window over the selection (moving it is window pointer input), a tap or writing beside the selection clears it,
// and drawn shapes are recognized. Ported from InkTools.cs / ToolController.cs (winui-final).

#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "ink/InkLayer.h"
#include "render/Overlay.h"
#include "tools/ExactEraser.h"
#include "tools/InkSelection.h"
#include "tools/ShapeRecognizer.h"
#include "tools/ShapeTool.h"
#include "tools/Stencils.h"

namespace inkpages::detail
{
    struct ToolSettings
    {
        Tool tool = Tool::Pen;
        bool wholeStrokeErasing = false;
        float eraserRadius = 4;
        SelectMode selectMode = SelectMode::Lasso;
        ShapeLook shape;
        bool recognizeShapes = false;
    };

    class ToolInput
    {
    public:
        ToolInput(InkLayer& ink, OverlayModel& overlay);

        /// Ink thread: a selection was made with the lasso or rectangle (strokes and the polygon, for the app's
        /// pictures); the selection was cleared by writing or tapping beside it; the pen goes to the window / comes back.
        std::function<void(StrokeSelection const&, std::vector<PointF> const& polygon)> selected;
        std::function<void()> cleared;
        std::function<void(bool taken)> penTaken;

        void SetTool(ToolSettings settings);
        /// The selection's frame as the UI shows it (strokes and pictures), or none.
        void SetFrame(std::optional<SelectionFrame> frame);
        /// The window's pen left the selection: the presenter gets it back.
        void GiveBackPen();

        InkSelection& Selection() { return selection; }
        Stencils& Stencil() { return stencils; }

    private:
        enum class Gesture { None, Erase, Select, Shape };
        struct Hover
        {
            PointF at;
            bool pen = false, inverted = false, leaving = false, barrel = false;
        };

        void Subscribe();
        void ApplyMode();
        void OnHover(Hover const& h);
        void Press(PointF at, bool barrel);
        void Move(std::vector<PointF> const& path);
        void Release(PointF at);
        bool DropTap(InkStroke const& stroke);
        void Recognize(std::vector<Placed> const& placed);

        InkLayer& ink;
        OverlayModel& overlay;
        ExactEraser eraser;
        InkSelection selection;
        ShapeTool shapes;
        ShapeRecognizer recognizer;
        Stencils stencils;
        ToolSettings settings;
        std::optional<SelectionFrame> frame;
        bool eraserEndTaken = false;
        bool penTakenNow = false;
        Gesture gesture = Gesture::None;
        bool rectangle = false;
        std::vector<PointF> lasso;
    };
}
