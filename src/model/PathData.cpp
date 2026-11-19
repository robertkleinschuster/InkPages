#include "inkpages/model/PathData.h"

#include <cwchar>
#include <cwctype>
#include <string>

namespace inkpages
{
    namespace
    {
        class Reader
        {
        public:
            explicit Reader(std::wstring_view text) : text(text) {}

            void Skip()
            {
                while (pos < text.size() && (std::iswspace(text[pos]) || text[pos] == L',')) ++pos;
            }
            bool AtEnd() { Skip(); return pos >= text.size(); }
            bool AtNumber()
            {
                Skip();
                return pos < text.size() && (std::iswdigit(text[pos]) || text[pos] == L'-' || text[pos] == L'+' || text[pos] == L'.');
            }
            wchar_t Command() { Skip(); return text[pos++]; }

            std::optional<float> Number()
            {
                if (!AtNumber()) return std::nullopt;
                size_t start = pos;
                if (text[pos] == L'-' || text[pos] == L'+') ++pos;
                bool dot = false;
                while (pos < text.size() && (std::iswdigit(text[pos]) || (text[pos] == L'.' && !dot)))
                {
                    dot = dot || text[pos] == L'.';
                    ++pos;
                }
                if (pos < text.size() && (text[pos] == L'e' || text[pos] == L'E'))
                {
                    ++pos;
                    if (pos < text.size() && (text[pos] == L'-' || text[pos] == L'+')) ++pos;
                    while (pos < text.size() && std::iswdigit(text[pos])) ++pos;
                }
                std::wstring number(text.substr(start, pos - start));
                wchar_t* end = nullptr;
                float value = std::wcstof(number.c_str(), &end);
                if (end != number.c_str() + number.size()) return std::nullopt; // "-" or "." alone: not path data
                return value;
            }

            std::optional<bool> Flag()
            {
                Skip();
                if (pos < text.size() && (text[pos] == L'0' || text[pos] == L'1')) return text[pos++] == L'1';
                return std::nullopt;
            }

        private:
            std::wstring_view text;
            size_t pos = 0;
        };
    }

    std::optional<PathData> ParsePath(std::wstring_view text)
    {
        using Kind = PathSegment::Kind;
        Reader r(text);
        PathData path;
        PointF current{}, start{};
        wchar_t command = 0;
        while (!r.AtEnd())
        {
            if (!r.AtNumber()) command = r.Command();
            else if (command == 0) return std::nullopt; // numbers before any command
            bool relative = std::iswlower(command) != 0;
            auto point = [&](float x, float y) { return relative ? PointF{ current.x + x, current.y + y } : PointF{ x, y }; };
            auto num = [&]() { return r.Number(); };
            switch (std::towupper(command))
            {
            case L'F':
            {
                auto rule = num();
                if (!rule) return std::nullopt;
                path.nonzero = *rule == 1;
                command = 0;
                break;
            }
            case L'M':
            {
                auto x = num(), y = num();
                if (!x || !y) return std::nullopt;
                current = start = point(*x, *y);
                path.segments.push_back({ Kind::Move, current });
                command = relative ? L'l' : L'L'; // further pairs are lines
                break;
            }
            case L'L':
            {
                auto x = num(), y = num();
                if (!x || !y) return std::nullopt;
                current = point(*x, *y);
                path.segments.push_back({ Kind::Line, current });
                break;
            }
            case L'H':
            {
                auto x = num();
                if (!x) return std::nullopt;
                current = { relative ? current.x + *x : *x, current.y };
                path.segments.push_back({ Kind::Line, current });
                break;
            }
            case L'V':
            {
                auto y = num();
                if (!y) return std::nullopt;
                current = { current.x, relative ? current.y + *y : *y };
                path.segments.push_back({ Kind::Line, current });
                break;
            }
            case L'C':
            {
                auto x1 = num(), y1 = num(), x2 = num(), y2 = num(), x = num(), y = num();
                if (!x1 || !y1 || !x2 || !y2 || !x || !y) return std::nullopt;
                PathSegment s{ Kind::Cubic };
                s.control1 = point(*x1, *y1);
                s.control2 = point(*x2, *y2);
                s.to = current = point(*x, *y);
                path.segments.push_back(s);
                break;
            }
            case L'A':
            {
                auto rx = num(), ry = num(), rotation = num();
                auto large = r.Flag(), sweep = r.Flag();
                auto x = num(), y = num();
                if (!rx || !ry || !rotation || !large || !sweep || !x || !y) return std::nullopt;
                PathSegment s{ Kind::Arc };
                s.radius = { *rx, *ry };
                s.rotation = *rotation;
                s.largeArc = *large;
                s.clockwise = *sweep; // y points down: sweep 1 turns clockwise on the screen
                s.to = current = point(*x, *y);
                path.segments.push_back(s);
                break;
            }
            case L'Z':
                path.segments.push_back({ Kind::Close, start });
                current = start;
                command = 0;
                break;
            default:
                return std::nullopt;
            }
        }
        return path;
    }
}
