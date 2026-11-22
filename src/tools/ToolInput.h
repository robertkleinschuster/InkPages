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
        bool shapeEnabled = false;
        bool squareEraser = false;
        int laserSeconds = 2;
    };

    class ToolInput
    {
    public:
        ToolInput(InkLayer& ink, OverlayModel& overlay);
        ~ToolInput();

        /// Ink thread: a selection was made with the lasso or rectangle (strokes and the polygon, for the app's
        /// pictures); the selection was cleared by writing or tapping beside it; the pen goes to the window / comes back.
        std::function<void(StrokeSelection const&, std::vector<PointF> const& polygon, bool rectangle)> selected;
        std::function<void()> cleared;
        std::function<void(bool taken)> penTaken;
        /// Ink thread: a pen tap with the Text tool (document point: edit the text there or start one).
        std::function<void(PointF at)> textTapped;
        /// Any thread: the view's zoom now (taps are measured on the screen).
        std::function<float()> zoom;

        void SetTool(ToolSettings settings);
        /// The running gesture ends (its pointer was lost, the pages changed under it): no shape, no selection; what an
        /// exact erase took stays, as its own undo step.
        void Cancel();
        /// The selection's frame as the UI shows it (strokes and pictures), or none.
        void SetFrame(std::optional<SelectionFrame> frame);
        /// While the selection is dragged: the ink previews `m` (PointTransform) and the frame shows it too, in the same
        /// work item (identity: as they are). The frame the tools hit-test stays until the drag is committed.
        void PreviewSelection(std::optional<Affine> transform);
        /// The text box being edited (document), or none: the pen hovering over it goes to the window (it edits there).
        void SetTextBox(std::optional<RectF> box);
        /// The window's pen left the selection: the presenter gets it back.
        void GiveBackPen();
        /// The mouse (window input, it never reaches the presenter) selects as the selection tool does, whatever tool
        /// is active: a lasso or a rectangle by the tool's select mode. `path` = the points since the press.
        void MouseSelectBegin(std::vector<PointF> const& path, bool forceLasso);
        void MouseSelectMove(std::vector<PointF> const& path);
        void MouseSelectEnd(PointF at);
        void MouseSelectCancel();

        InkSelection& Selection() { return selection; }
        Stencils& Stencil() { return stencils; }

    private:
        enum class Gesture { None, Erase, Select, Shape, Text, Laser };
        PointF laserAt{};
        struct Hover
        {
            PointF at;
            bool pen = false, inverted = false, leaving = false, barrel = false;
        };

        void Subscribe();
        void Unsubscribe() noexcept;
        void ApplyMode();
        void OnHover(Hover const& h);
        void Press(PointF at, bool barrel, bool inverted);
        void Move(std::vector<PointF> const& path);
        void Release(PointF at);
        bool DropTap(InkStroke const& stroke);
        float Zoom() const { return zoom ? zoom() : 1; }
        void Recognize(std::vector<Placed> const& placed);

        InkLayer& ink;
        struct PointerDispatch;
        std::shared_ptr<PointerDispatch> pointerDispatch;
        winrt::Windows::UI::Input::Inking::InkUnprocessedInput events{ nullptr };
        std::optional<winrt::event_token> pressed, moved, released, lost;
        std::optional<winrt::event_token> entering, hovering, exiting, moving;
        OverlayModel& overlay;
        ExactEraser eraser;
        InkSelection selection;
        ShapeTool shapes;
        ShapeRecognizer recognizer;
        Stencils stencils;
        ToolSettings settings;
        std::optional<SelectionFrame> frame;
        std::optional<RectF> textBox;
        PointF textPress;
        uint64_t textPressMs = 0;
        float textFarthest = 0; // how far the gesture went from the press (page units)
        bool eraserEndHover = false;
        bool penTakenNow = false;
        Gesture gesture = Gesture::None;
        bool rectangle = false;
        bool mouseGesture = false; // the running selection gesture is the mouse's
        std::vector<PointF> lasso;
    };
}
