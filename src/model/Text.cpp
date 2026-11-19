#include "inkpages/model/Text.h"

#include <cmath>

namespace inkpages
{
    int TextSizes::StepOf(float size)
    {
        int best = 0;
        for (int i = 1; i < static_cast<int>(Steps.size()); ++i)
        {
            if (std::abs(Steps[i] - size) < std::abs(Steps[best] - size)) best = i;
        }
        return best;
    }

    long RichEditText::Twips(float size, float zoom)
    {
        return std::lround(size * zoom * 0.75f * 20);
    }

    std::wstring RichEditText::FromRichEdit(std::wstring const& text)
    {
        std::wstring out;
        out.reserve(text.size());
        for (size_t i = 0; i < text.size(); ++i)
        {
            if (text[i] == L'\r')
            {
                out += L'\n';
                if (i + 1 < text.size() && text[i + 1] == L'\n') ++i; // \r\n is one break
            }
            else out += text[i];
        }
        return out;
    }

    std::wstring RichEditText::ToRichEdit(std::wstring const& text)
    {
        std::wstring out;
        out.reserve(text.size());
        for (wchar_t c : text) out += c == L'\n' ? L'\r' : c;
        return out;
    }
}
