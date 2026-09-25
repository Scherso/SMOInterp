#include "config.hpp"

#include "lib.hpp"
#include "nn/fs.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

namespace nn::fs {
    Result Unmount(char const* mount);
}

namespace smo::config {
    namespace {
        constexpr const char* MountName = "smointerp";
        constexpr const char* Path = "smointerp:/SMOInterp/config.ini";
        constexpr int MinFps = 60;
        constexpr int MaxFps = 500;

        Settings s_Settings;
        bool s_Loaded = false;

        char* Trim(char* s) {
            while (std::isspace(static_cast<unsigned char>(*s)))
                ++s;
            char* end = s + std::strlen(s);
            while (end > s && std::isspace(static_cast<unsigned char>(end[-1])))
                *--end = '\0';
            return s;
        }

        bool ParseBool(const char* value, bool fallback) {
            if (!std::strcmp(value, "on") || !std::strcmp(value, "true") || !std::strcmp(value, "1"))
                return true;
            if (!std::strcmp(value, "off") || !std::strcmp(value, "false") || !std::strcmp(value, "0"))
                return false;
            return fallback;
        }

        void Parse(char* text) {
            for (char* line = std::strtok(text, "\r\n"); line != nullptr; line = std::strtok(nullptr, "\r\n")) {
                if (char* comment = std::strchr(line, '#'))
                    *comment = '\0';
                char* eq = std::strchr(line, '=');
                if (eq == nullptr)
                    continue;
                *eq = '\0';
                const char* key = Trim(line);
                const char* value = Trim(eq + 1);

                if (!std::strcmp(key, "fps"))
                    s_Settings.fps = std::clamp(std::atoi(value), MinFps, MaxFps);
                else if (!std::strcmp(key, "interpolation"))
                    s_Settings.interpolation = ParseBool(value, s_Settings.interpolation);
            }
        }

        void Load() {
            if (R_FAILED(nn::fs::MountSdCardForDebug(MountName))) {
                Logging.Log("config: SD card unavailable, using defaults");
                return;
            }

            nn::fs::FileHandle file;
            if (R_SUCCEEDED(nn::fs::OpenFile(&file, Path, nn::fs::OpenMode_Read))) {
                char text[1024] = {};
                long size = 0;
                nn::fs::GetFileSize(&size, file);
                nn::fs::ReadFile(file, 0, text, std::min<long>(size, sizeof(text) - 1));
                nn::fs::CloseFile(file);
                Parse(text);
            } else {
                Logging.Log("config: no %s, using defaults", Path);
            }

            nn::fs::Unmount(MountName);
        }
    }

    const Settings& Get() {
        if (!s_Loaded) {
            s_Loaded = true;
            Load();
            Logging.Log("config: fps = %d, interpolation = %s", s_Settings.fps,
                        s_Settings.interpolation ? "on" : "off");
        }
        return s_Settings;
    }
}
