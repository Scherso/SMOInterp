#pragma once

#include "config.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstring>

/*
 * The config.ini parser, kept free of exlaunch and SDK types so the host tests can run it.
 * Problems are reported through `warn` (a printf-style format with one string argument).
 */
namespace smo::config::parse {
    using Warn = void (*)(const char* format, const char* arg);
    using Smoothing = Settings::Smoothing;
    using BoneBlend = Settings::BoneBlend;

    constexpr int MinFps = 60;
    constexpr int MaxFps = 500;
    constexpr float MaxBoneTurnDegrees = 180.f;

    /* Strips leading and trailing whitespace in place. */
    inline char* Trim(char* s) {
        while (std::isspace(static_cast<unsigned char>(*s)))
            ++s;
        char* end = s + std::strlen(s);
        while (end > s && std::isspace(static_cast<unsigned char>(end[-1])))
            *--end = '\0';
        return s;
    }

    inline bool Is(const char* value, const char* a, const char* b = nullptr, const char* c = nullptr) {
        return !std::strcmp(value, a) || (b && !std::strcmp(value, b)) || (c && !std::strcmp(value, c));
    }

    /* The whole value must be a decimal integer; atoi would turn "abc" or "12x" into a silent 0 or 12. */
    inline bool ParseInt(const char* value, int& out) {
        const char* end = value + std::strlen(value);
        auto [ptr, ec] = std::from_chars(value, end, out);
        return ec == std::errc {} && ptr == end && ptr != value;
    }

    /* The whole value must be a decimal number, e.g. "25", "0.5". */
    inline bool ParseFloat(const char* value, float& out) {
        const char* end = value + std::strlen(value);
        auto [ptr, ec] = std::from_chars(value, end, out);
        return ec == std::errc {} && ptr == end && ptr != value;
    }

    /* One system's mode: camera / models / effects. */
    inline bool ParseSmoothing(const char* value, Smoothing& out) {
        if (Is(value, "interpolate", "on", "true"))
            out = Smoothing::Interpolate;
        else if (Is(value, "extrapolate"))
            out = Smoothing::Extrapolate;
        else if (Is(value, "off", "false", "0"))
            out = Smoothing::Off;
        else
            return false;
        return true;
    }

    /* The `interpolation` preset, setting all three systems at once. */
    inline bool ParsePreset(const char* value, Smoothing& camera, Smoothing& rest) {
        if (Is(value, "on", "true", "1")) {
            camera = Smoothing::Interpolate;
            rest = Smoothing::Interpolate;
        } else if (Is(value, "camera")) {
            camera = Smoothing::Extrapolate;
            rest = Smoothing::Interpolate;
        } else if (Is(value, "extrapolate")) {
            camera = Smoothing::Extrapolate;
            rest = Smoothing::Extrapolate;
        } else if (Is(value, "off", "false", "0")) {
            camera = Smoothing::Off;
            rest = Smoothing::Off;
        } else {
            return false;
        }
        return true;
    }

    inline bool ParseBoneBlend(const char* value, BoneBlend& out) {
        if (Is(value, "auto"))
            out = BoneBlend::Auto;
        else if (Is(value, "linear"))
            out = BoneBlend::Linear;
        else if (Is(value, "rotation"))
            out = BoneBlend::Rotation;
        else
            return false;
        return true;
    }

    inline bool ParseFramePacing(const char* value, Settings::FramePacing& out) {
        if (Is(value, "smooth"))
            out = Settings::FramePacing::Smooth;
        else if (Is(value, "measured"))
            out = Settings::FramePacing::Measured;
        else
            return false;
        return true;
    }

    inline const char* FramePacingName(Settings::FramePacing pacing) {
        return pacing == Settings::FramePacing::Smooth ? "smooth" : "measured";
    }

    inline const char* SmoothingName(Smoothing smoothing) {
        switch (smoothing) {
        case Smoothing::Off:
            return "off";
        case Smoothing::Interpolate:
            return "interpolate";
        case Smoothing::Extrapolate:
            return "extrapolate";
        }
        return "?";
    }

    inline const char* BoneBlendName(BoneBlend blend) {
        switch (blend) {
        case BoneBlend::Auto:
            return "auto";
        case BoneBlend::Linear:
            return "linear";
        case BoneBlend::Rotation:
            return "rotation";
        }
        return "?";
    }

    /* What the file said, before the preset and the per-system overrides are combined. */
    struct Parsed {
        Settings settings;
        bool hasCamera = false, hasModels = false, hasEffects = false;
        Smoothing camera = Smoothing::Interpolate, models = Smoothing::Interpolate, effects = Smoothing::Interpolate;
    };

    /* One `key = value  # comment` line. Invalid values keep the previous setting. */
    inline void ParseLine(char* line, Parsed& parsed, Warn warn) {
        if (char* comment = std::strchr(line, '#'))
            *comment = '\0';
        char* eq = std::strchr(line, '=');
        if (eq == nullptr)
            return;
        *eq = '\0';
        const char* key = Trim(line);
        const char* value = Trim(eq + 1);
        Settings& s = parsed.settings;

        if (Is(key, "fps")) {
            int fps = 0;
            if (ParseInt(value, fps))
                s.fps = std::clamp(fps, MinFps, MaxFps);
            else
                warn("config: ignoring fps = \"%s\" (not a whole number)", value);
        } else if (Is(key, "interpolation")) {
            Smoothing camera, rest;
            if (ParsePreset(value, camera, rest)) {
                s.camera = camera;
                s.models = s.effects = rest;
            } else {
                warn("config: ignoring interpolation = \"%s\" (expected on, camera, extrapolate or off)", value);
            }
        } else if (Is(key, "camera") || Is(key, "models") || Is(key, "effects")) {
            Smoothing mode;
            if (!ParseSmoothing(value, mode)) {
                warn("config: ignoring \"%s\" (expected interpolate, extrapolate or off)", value);
            } else if (Is(key, "camera")) {
                parsed.hasCamera = true;
                parsed.camera = mode;
            } else if (Is(key, "models")) {
                parsed.hasModels = true;
                parsed.models = mode;
            } else {
                parsed.hasEffects = true;
                parsed.effects = mode;
            }
        } else if (Is(key, "bone_blend")) {
            if (!ParseBoneBlend(value, s.boneBlend))
                warn("config: ignoring bone_blend = \"%s\" (expected auto, linear or rotation)", value);
        } else if (Is(key, "max_bone_turn")) {
            float degrees = 0.f;
            if (ParseFloat(value, degrees))
                s.maxBoneTurnDegrees = std::clamp(degrees, 0.f, MaxBoneTurnDegrees);
            else
                warn("config: ignoring max_bone_turn = \"%s\" (not a number)", value);
        } else if (Is(key, "max_extrapolation")) {
            float ticks = 0.f;
            if (ParseFloat(value, ticks))
                s.maxExtrapolation = std::clamp(ticks, 0.f, 1.f);
            else
                warn("config: ignoring max_extrapolation = \"%s\" (not a number)", value);
        } else if (Is(key, "frame_pacing")) {
            if (!ParseFramePacing(value, s.framePacing))
                warn("config: ignoring frame_pacing = \"%s\" (expected smooth or measured)", value);
        } else {
            warn("config: ignoring unknown key \"%s\"", key);
        }
    }

    /* The whole file. Splits on CR/LF in place; per-system keys win over the preset in any order. */
    inline void Parse(char* text, Settings& settings, Warn warn) {
        Parsed parsed;
        parsed.settings = settings;
        while (*text != '\0') {
            char* end = text + std::strcspn(text, "\r\n");
            bool last = *end == '\0';
            *end = '\0';
            ParseLine(text, parsed, warn);
            if (last)
                break;
            text = end + 1;
        }
        settings = parsed.settings;
        if (parsed.hasCamera)
            settings.camera = parsed.camera;
        if (parsed.hasModels)
            settings.models = parsed.models;
        if (parsed.hasEffects)
            settings.effects = parsed.effects;
    }
}
