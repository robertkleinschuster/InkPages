#pragma once

#include "inkpages/model/Paper.h"

#include <array>
#include <cmath>

namespace inkpages::PaperAppearance
{
    inline constexpr Color DarkSheet{ 255, 21, 22, 25 };

    inline double Luminance(Color color)
    {
        static const auto linear = [] {
            std::array<double, 256> values{};
            for (std::size_t i = 0; i < values.size(); ++i)
            {
                double s = i / 255.0;
                values[i] = s <= 0.04045 ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4);
            }
            return values;
        }();
        return 0.2126 * linear[color.r] + 0.7152 * linear[color.g] + 0.0722 * linear[color.b];
    }

    /// Display color on dark paper. Keep opacity and hue while lifting dark RGB values toward white.
    inline Color DarkInk(Color ink, bool highlighter = false)
    {
        if (!ink.a) return ink;
        Color white{ ink.a, 255, 255, 255 };
        if (!ink.r && !ink.g && !ink.b && !highlighter) return white;

        auto mix = [](int a, int b, int fraction) {
            return static_cast<uint8_t>((a * (255 - fraction) + b * fraction + 127) / 255);
        };
        auto lighten = [&](int lift) {
            return Color{ ink.a, mix(ink.r, 255, lift), mix(ink.g, 255, lift), mix(ink.b, 255, lift) };
        };
        const double target = (Luminance(DarkSheet) + 0.05) * (highlighter ? 2.5 : 4.5) - 0.05;
        auto readable = [&](Color choice) {
            Color visible{ 255, mix(DarkSheet.r, choice.r, ink.a), mix(DarkSheet.g, choice.g, ink.a),
                mix(DarkSheet.b, choice.b, ink.a) };
            return Luminance(visible) >= target;
        };
        if (readable(ink)) return ink;
        if (!readable(white)) return white;
        int first = 1, last = 255;
        while (first < last)
        {
            int mid = (first + last) / 2;
            if (readable(lighten(mid))) last = mid;
            else first = mid + 1;
        }
        return lighten(first);
    }

    /// Windows Ink blends highlighters at half opacity. Keep the original alpha on the attributes and swatches.
    inline Color DarkToolInk(Color color, bool highlighter)
    {
        Color sample = color;
        if (highlighter) sample.a = 127;
        Color display = DarkInk(sample, highlighter);
        display.a = color.a;
        return display;
    }
}
