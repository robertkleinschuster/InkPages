#include "inkpages/Paper.h"

namespace inkpages
{
    std::vector<Line> Paper::Lines(float width, float height) const
    {
        std::vector<Line> lines;
        if (kind == PaperKind::Plain || spacing <= 0 || width <= 0 || height <= 0)
        {
            return lines;
        }
        // Counted, not accumulated: no drift over a long sheet.
        if (kind == PaperKind::Graph)
        {
            for (int i = 0; i * spacing <= width + 1e-3f; ++i)
            {
                float x = i * spacing;
                lines.push_back({ { x, 0 }, { x, height } });
            }
        }
        for (int i = 0; i * spacing <= height + 1e-3f; ++i)
        {
            float y = i * spacing;
            lines.push_back({ { 0, y }, { width, y } });
        }
        return lines;
    }
}
