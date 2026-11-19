#include "inkpages/model/Pens.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>
#include <sstream>

namespace inkpages
{
    namespace
    {
        template <size_t N> int Nearest(std::array<float, N> const& steps, float width)
        {
            int best = 0;
            for (int i = 1; i < static_cast<int>(N); ++i)
            {
                if (std::abs(steps[i] - width) < std::abs(steps[best] - width)) best = i;
            }
            return best;
        }

        std::string Presets(std::vector<PenPreset> const& presets)
        {
            std::string text;
            for (PenPreset const& p : presets)
            {
                if (!text.empty()) text += ',';
                text += std::format("{}:{}", p.color, p.width);
            }
            return text;
        }

        bool Number(std::string_view text, float& value)
        {
            auto result = std::from_chars(text.data(), text.data() + text.size(), value);
            return result.ec == std::errc() && result.ptr == text.data() + text.size() && std::isfinite(value);
        }

        bool Number(std::string_view text, int& value)
        {
            auto result = std::from_chars(text.data(), text.data() + text.size(), value);
            return result.ec == std::errc() && result.ptr == text.data() + text.size();
        }

        std::vector<PenPreset> ParsePresets(std::string const& text, size_t paletteSize)
        {
            std::vector<PenPreset> presets;
            std::stringstream items(text);
            std::string item;
            while (std::getline(items, item, ','))
            {
                size_t colon = item.find(':');
                PenPreset p;
                if (colon == std::string::npos || !Number(std::string_view(item).substr(0, colon), p.color) ||
                    !Number(std::string_view(item).substr(colon + 1), p.width) || p.color < 0 ||
                    p.color >= static_cast<int>(paletteSize) || p.width <= 0)
                {
                    return {}; // damaged: the defaults
                }
                presets.push_back(p);
            }
            return presets;
        }
    }

    int PenWidths::StepOf(float width) { return Nearest(Steps, width); }
    int HighlighterWidths::StepOf(float width) { return Nearest(Steps, width); }

    std::string ToolChoices::Serialize() const
    {
        return std::format("pens={};highlighters={};eraser={};whole={};tool={};preset={};select={};shape={};recognize={}",
            Presets(pens), Presets(highlighters), eraserRadius, wholeStrokeErasing ? 1 : 0, static_cast<int>(tool), preset,
            static_cast<int>(selectMode), static_cast<int>(shape), recognizeShapes ? 1 : 0);
    }

    ToolChoices ToolChoices::Parse(std::string const& text)
    {
        ToolChoices c;
        std::stringstream fields(text);
        std::string field;
        while (std::getline(fields, field, ';'))
        {
            size_t eq = field.find('=');
            if (eq == std::string::npos) continue;
            std::string key = field.substr(0, eq), value = field.substr(eq + 1);
            int i = 0;
            float f = 0;
            if (key == "pens") c.pens = ParsePresets(value, Palette::Pens.size());
            else if (key == "highlighters") c.highlighters = ParsePresets(value, Palette::Highlighters.size());
            else if (key == "eraser" && Number(value, f)) c.eraserRadius = std::clamp(f, 1.f, 20.f);
            else if (key == "whole" && Number(value, i)) c.wholeStrokeErasing = i != 0;
            else if (key == "tool" && Number(value, i) && i >= 0 && i <= 4) c.tool = static_cast<Tool>(i);
            else if (key == "select" && Number(value, i) && i >= 0 && i <= 1) c.selectMode = static_cast<SelectMode>(i);
            else if (key == "shape" && Number(value, i) && i >= 0 && i <= 3) c.shape = static_cast<ShapeKind>(i);
            else if (key == "recognize" && Number(value, i)) c.recognizeShapes = i != 0;
            else if (key == "preset" && Number(value, i) && i >= 0) c.preset = i;
        }
        return c;
    }
}
