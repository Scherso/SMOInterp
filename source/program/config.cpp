#include "config.hpp"

#include "config_parse.hpp"
#include "lib.hpp"
#include "tuning.hpp"
#include "nn/fs.hpp"

#include <algorithm>
#include <atomic>

namespace nn::fs {
    Result Unmount(char const* mount);
}

namespace smo::config {
    namespace {
        constexpr const char* MountName = "smointerp";
        constexpr const char* Path = "smointerp:/SMOInterp/config.ini";

        Settings s_Settings;

        /* Get() can be reached from more than one thread at startup; exactly one of them loads the file. */
        enum class LoadState : u8 { Unloaded, Loading, Loaded };
        std::atomic<LoadState> s_LoadState { LoadState::Unloaded };

        void WarnToLog(const char* format, const char* arg) {
            Logging.Log(format, arg);
        }

        void Load() {
            if (R_FAILED(nn::fs::MountSdCardForDebug(MountName))) {
                Logging.Log("config: SD card unavailable, using defaults");
                return;
            }

            nn::fs::FileHandle file;
            if (R_SUCCEEDED(nn::fs::OpenFile(&file, Path, nn::fs::OpenMode_Read))) {
                char text[tuning::MaxConfigBytes] = {};
                long size = 0;
                bool ok = R_SUCCEEDED(nn::fs::GetFileSize(&size, file)) &&
                          R_SUCCEEDED(nn::fs::ReadFile(file, 0, text, std::min<long>(size, sizeof(text) - 1)));
                nn::fs::CloseFile(file);
                if (ok)
                    parse::Parse(text, s_Settings, WarnToLog);
                else
                    Logging.Log("config: could not read %s, using defaults", Path);
            } else {
                Logging.Log("config: no %s, using defaults", Path);
            }

            nn::fs::Unmount(MountName);
        }
    }

    const Settings& Get() {
        LoadState state = s_LoadState.load(std::memory_order_acquire);
        if (state == LoadState::Loaded)
            return s_Settings;

        LoadState expected = LoadState::Unloaded;
        if (s_LoadState.compare_exchange_strong(expected, LoadState::Loading, std::memory_order_acq_rel)) {
            Load();
            Logging.Log("config: fps = %d, camera = %s, models = %s, effects = %s", s_Settings.fps,
                        parse::SmoothingName(s_Settings.camera), parse::SmoothingName(s_Settings.models),
                        parse::SmoothingName(s_Settings.effects));
            Logging.Log("config: bone_blend = %s, max_bone_turn = %d, max_extrapolation = %d%%, frame_pacing = %s",
                        parse::BoneBlendName(s_Settings.boneBlend), static_cast<int>(s_Settings.maxBoneTurnDegrees),
                        static_cast<int>(s_Settings.maxExtrapolation * 100.f),
                        parse::FramePacingName(s_Settings.framePacing));
            s_LoadState.store(LoadState::Loaded, std::memory_order_release);
        } else {
            /* Another thread is reading the file; it's a few hundred bytes, so wait it out. */
            while (s_LoadState.load(std::memory_order_acquire) != LoadState::Loaded)
                svcSleepThread(0);
        }
        return s_Settings;
    }
}
