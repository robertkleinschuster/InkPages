#include "tools/ShapeRecognizer.h"

#include <format>

#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.Input.Inking.Analysis.h>

#include "inkpages/Log.h"

namespace inkpages::detail
{
    using namespace winrt::Windows::UI::Input::Inking::Analysis;
    using winrt::Windows::Foundation::AsyncStatus;
    using winrt::Windows::Foundation::IAsyncOperation;

    void ShapeRecognizer::Recognize(InkStroke const& stroke, std::function<void(std::optional<std::vector<PointF>>)> done)
    {
        InkAnalyzer analyzer;
        analyzer.AddDataForStroke(stroke);
        analyzer.SetStrokeDataKind(stroke.Id(), InkAnalysisStrokeKind::Drawing);
        // The analyzer must live until its analysis completes: the handler holds it.
        analyzer.AnalyzeAsync().Completed([this, analyzer, done](IAsyncOperation<InkAnalysisResult> const& operation, AsyncStatus status) {
            std::optional<std::vector<PointF>> outline;
            try
            {
                if (status == AsyncStatus::Completed && operation.GetResults().Status() == InkAnalysisStatus::Updated)
                {
                    for (auto const& node : analyzer.AnalysisRoot().FindNodes(InkAnalysisNodeKind::InkDrawing))
                    {
                        auto drawing = node.as<InkAnalysisInkDrawing>();
                        auto kind = drawing.DrawingKind();
                        if (kind == InkAnalysisDrawingKind::Drawing) continue;
                        std::vector<PointF> points;
                        for (auto const& p : drawing.Points()) points.push_back({ p.X, p.Y });
                        if (kind == InkAnalysisDrawingKind::Circle || kind == InkAnalysisDrawingKind::Ellipse)
                        {
                            outline = EllipseFromAxes({ drawing.Center().X, drawing.Center().Y }, points);
                        }
                        else if (!points.empty())
                        {
                            points.push_back(points.front());
                            outline = points;
                        }
                        Log(std::format("recognition: shape kind {}", static_cast<int>(kind)));
                        break;
                    }
                }
                else if (status != AsyncStatus::Completed)
                {
                    LogHr("InkAnalyzer.AnalyzeAsync", operation.ErrorCode());
                }
            }
            catch (...)
            {
                LogException("recognition");
            }
            host.Post("recognized", [done, outline] { done(outline); });
        });
    }
}
