#include "lib.hpp"
#include "actors.hpp"
#include "camera.hpp"
#include "clock.hpp"
#include "config.hpp"
#include "effects.hpp"
#include "nvn.hpp"
#include "smo.hpp"
#include "tuning.hpp"

#include <algorithm>
#include <cmath>

using smo::Field;
using smo::MainFunc;

namespace {
    using ObjFn = void (*)(void*);

    constexpr u64 ReportTicks = smo::clock::TicksPerSecond * smo::tuning::ReportIntervalSeconds;
    /* Rates are logged with one decimal place, computed in tenths. */
    constexpr u64 Tenths = 10;

    smo::clock::FrameClock s_Clock;

    /* This frame's blend weight per system, and how bones are blended (see clock::BlendWeight). */
    struct FrameBlend {
        float camera = 1.f;
        float models = 1.f;
        float effects = 1.f;
        smo::actors::BoneOptions bones;
    };
    FrameBlend s_Blend;

    void AdvanceClock() {
        const auto& config = smo::config::Get();
        s_Clock.Advance(svcGetSystemTick(), config.fps,
                        config.framePacing == smo::config::Settings::FramePacing::Smooth);

        float leftover = s_Clock.leftover;
        s_Blend.camera = smo::clock::BlendWeight(config.camera, leftover, config.maxExtrapolation);
        s_Blend.models = smo::clock::BlendWeight(config.models, leftover, config.maxExtrapolation);
        s_Blend.effects = smo::clock::BlendWeight(config.effects, leftover, config.maxExtrapolation);

        using BoneBlend = smo::config::Settings::BoneBlend;
        bool extrapolating = config.models == smo::config::Settings::Smoothing::Extrapolate;
        s_Blend.bones.rotation =
            config.boneBlend == BoneBlend::Rotation || (config.boneBlend == BoneBlend::Auto && extrapolating);
        s_Blend.bones.minHalfTurnCos = config.maxBoneTurnDegrees > 0.f
                                           ? std::cos(smo::tuning::DegreesToRadians(config.maxBoneTurnDegrees) / 2.f)
                                           : -1.f;
    }

    /* Rendered frames and game ticks, logged as rates every ReportTicks. */
    struct RateReport {
        u64 frames = 0;
        u64 ticks = 0;
        u64 windowStartTick = 0;
        u64 windowStartFrames = 0;
        u64 windowStartTicks = 0;
    };
    RateReport s_Report;

    void Report() {
        ++s_Report.frames;

        u64 now = svcGetSystemTick();
        u64 elapsed = now - s_Report.windowStartTick;
        if (s_Report.windowStartTick != 0 && elapsed < ReportTicks)
            return;

        if (s_Report.windowStartTick != 0) {
            u64 fps = (s_Report.frames - s_Report.windowStartFrames) * smo::clock::TicksPerSecond * Tenths / elapsed;
            u64 steps = (s_Report.ticks - s_Report.windowStartTicks) * smo::clock::TicksPerSecond * Tenths / elapsed;
            Logging.Log("render %lu.%lu fps, logic %lu.%lu steps/s", fps / Tenths, fps % Tenths, steps / Tenths,
                        steps % Tenths);
        }
        s_Report.windowStartTick = now;
        s_Report.windowStartFrames = s_Report.frames;
        s_Report.windowStartTicks = s_Report.ticks;
    }

    /* The scene whose graphics were updated by the last game tick, refreshed on draw-only frames. */
    void* s_GraphicsScene = nullptr;
    /* The camera is captured once per tick, at the tick's first render-side step. */
    bool s_CameraCapturedThisTick = false;

    /*
     * The scene camera info: al::LiveActorKit+0x78 is the CameraDirector, +0x10 its SceneCameraInfo.
     */
    void* SceneCameraInfo(void* kit) {
        void* director = kit ? Field<void*>(kit, smo::fields::LiveActorKit_cameraDirector) : nullptr;
        return director ? Field<void*>(director, smo::fields::CameraDirector_sceneCameraInfo) : nullptr;
    }

    /*
     * The scene's kit: al::Scene+0x90 is its LiveActorKit.
     */
    void* SceneKit(void* scene) {
        return scene ? Field<void*>(scene, smo::fields::Scene_liveActorKit) : nullptr;
    }

    /*
     * Put this frame's camera in place before the frame's render-side work, not just before the draw.
     * The simulation ends by building render state from the camera (LiveActorKit::updateGraphics: the
     * async view-update jobs, then GraphicsSystemInfo's per-frame work), and the draw later uses
     * whatever camera is in the CameraDirector. Blending only after that state was built leaves the two
     * on different cameras by the blend's sub-tick offset; on detail edges the game's temporal
     * reprojection then samples the previous frame at a slightly wrong pose, which shimmers while the
     * camera moves (and is invisible when it does not, or with camera = off, where the offset is zero).
     *
     * Capturing here is safe because the scene's nerve has finished posing the camera: this runs at the
     * end of the tick. On a draw-only frame the tick's camera was captured already, so this only
     * repeats the blend.
     */
    void ApplyCameraForFrame(void* kit) {
        void* cameraInfo = SceneCameraInfo(kit);
        if (s_Clock.runCalcThisFrame && !s_CameraCapturedThisTick) {
            smo::camera::Capture(cameraInfo);
            s_CameraCapturedThisTick = true;
        }
        smo::camera::Apply(cameraInfo, s_Blend.camera);
    }

    /* The effect systems and groups the last tick updated, replayed on draw-only frames. */
    struct EffectGroupCall {
        void* system;
        int group;
        float rate;
        int swapMode;
    };
    constexpr size_t MaxEffectGroupCalls = smo::tuning::MaxEffectGroupCalls;
    EffectGroupCall s_EffectGroupCalls[MaxEffectGroupCalls];
    size_t s_EffectGroupCallCount = 0;
    constexpr size_t MaxEffectSystems = smo::tuning::MaxEffectSystems;
    void* s_EffectSystems[MaxEffectSystems];
    size_t s_EffectSystemCount = 0;

    void ForgetEffects() {
        s_EffectGroupCallCount = 0;
        s_EffectSystemCount = 0;
    }
}

HOOK_DEFINE_TRAMPOLINE(ProcFrame) {
    static void Callback(void* framework) {
        AdvanceClock();
        Orig(framework);
        Report();
    }
};

HOOK_DEFINE_TRAMPOLINE(ControllerMgrCalc) {
    static void Callback(void* controllerMgr) {
        if (s_Clock.runCalcThisFrame)
            Orig(controllerMgr);
    }
};

HOOK_DEFINE_TRAMPOLINE(PrePassLightKeeperExecute) {
    static void Callback(void* keeper, void* shadowDirector) {
        if (s_Clock.runCalcThisFrame)
            Orig(keeper, shadowDirector);
    }
};

/*
 * RainTextureKeeper::update runs from GraphicsSystemInfo::updateGraphics, which draw-only frames
 * replay, and feeds raindrops into the ripple simulation (FluidSimulateWave::storePoint). The
 * ripple step only runs on game ticks, so rain on every frame adds points twice per step and the
 * ripple buffers overflow. Keep rain on the ripple step's cadence.
 */
HOOK_DEFINE_TRAMPOLINE(RainTextureKeeperUpdate) {
    static void Callback(void* keeper) {
        if (s_Clock.runCalcThisFrame)
            Orig(keeper);
    }
};

HOOK_DEFINE_TRAMPOLINE(UpdateKitListPostOnNerveEnd) {
    static void Callback(void* scene) {
        /* Runs LiveActorKit::updateGraphics, so the blend has to be in place before it: see
         * ApplyCameraForFrame. This is the tick's first render-side step. */
        ApplyCameraForFrame(SceneKit(scene));
        Orig(scene);
        s_GraphicsScene = scene;
    }
};

HOOK_DEFINE_TRAMPOLINE(LiveActorKitPreDrawGraphics) {
    static void Callback(void* kit) {
        /* Usually already blended by UpdateKitListPostOnNerveEnd; this makes sure the renderer's copy
         * of the camera (ViewInfo) is the frame's camera. */
        ApplyCameraForFrame(kit);
        Orig(kit);
    }
};

HOOK_DEFINE_TRAMPOLINE(EffectSystemPreprocess) {
    static void Callback(void* effectSystem) {
        auto* end = s_EffectSystems + s_EffectSystemCount;
        if (s_Clock.runCalcThisFrame && std::find(s_EffectSystems, end, effectSystem) == end) {
            if (s_EffectSystemCount < MaxEffectSystems)
                s_EffectSystems[s_EffectSystemCount++] = effectSystem;
            else
                SMO_WARN_ONCE("limit: more than %zu effect systems in a tick", MaxEffectSystems);
        }
        Orig(effectSystem);
    }
};

HOOK_DEFINE_TRAMPOLINE(VfxSystemCalculateGroup) {
    static void Callback(void* system, int group, float rate, int swapMode) {
        if (s_Clock.runCalcThisFrame) {
            if (s_EffectGroupCallCount < MaxEffectGroupCalls)
                s_EffectGroupCalls[s_EffectGroupCallCount++] = { system, group, rate, swapMode };
            else
                SMO_WARN_ONCE("limit: more than %zu effect group calls in a tick", MaxEffectGroupCalls);
        }
        Orig(system, group, rate * s_Clock.frameTicks, swapMode);
    }
};

HOOK_DEFINE_TRAMPOLINE(EmitterSetCalculate) {
    static void Callback(void* emitterSet, float rate, int swapMode, bool flag, void* lodCallback) {
        smo::effects::ScopedBlend blend(emitterSet, s_Blend.effects);
        Orig(emitterSet, rate, swapMode, flag, lodCallback);
    }
};

/*
 * nn::vfx::EmitterSet::Initialize. Only the set pointer matters here; the five ints are passed
 * through untouched and their meaning isn't known (OdysseyDecomp doesn't cover nn::vfx).
 */
HOOK_DEFINE_TRAMPOLINE(EmitterSetInitialize) {
    static void Callback(void* emitterSet, int a, int b, int c, int d, int e, void* heap) {
        smo::effects::Forget(emitterSet);
        Orig(emitterSet, a, b, c, d, e, heap);
    }
};

HOOK_DEFINE_TRAMPOLINE(FluidSimulateWaveUpdate) {
    static void Callback(void* wave, void* updateInfo) {
        if (s_Clock.runCalcThisFrame)
            Orig(wave, updateInfo);
    }
};

/* ModelCtrl's per-frame skeleton upload: bones are blended around it. */
HOOK_DEFINE_TRAMPOLINE(ModelCtrlUpdateModelDrawBuffer) {
    static void Callback(void* modelCtrl, int bufferIndex) {
        smo::actors::ScopedBlend blend(modelCtrl, s_Blend.models, s_Blend.bones);
        Orig(modelCtrl, bufferIndex);
    }
};

HOOK_DEFINE_TRAMPOLINE(SceneDtor) {
    static void Callback(void* scene) {
        if (scene == s_GraphicsScene) {
            smo::camera::Reset();
            s_GraphicsScene = nullptr;
        }
        /* The scene owns its effect system; nothing may be replayed into it after this. */
        ForgetEffects();
        Orig(scene);
        /* The scene's models and their worker jobs are gone now, so no upload can race the reset. */
        smo::actors::Reset();
        smo::effects::Reset();
    }
};

namespace {
    /* al::Scene::movement minus updateNerve/control: every render-side step, paired with this frame's draw. */
    void RefreshSceneGraphics(void* scene) {
        void* kit = SceneKit(scene);
        if (kit == nullptr)
            return;

        /* Before updateGraphics: it and the model uploads must see the same camera the frame draws with. */
        ApplyCameraForFrame(kit);

        void* graphics = Field<void*>(kit, smo::fields::LiveActorKit_graphicsSystemInfo);
        void* occlusion =
            graphics ? Field<void*>(graphics, smo::fields::GraphicsSystemInfo_modelOcclusionCulling) : nullptr;
        void* displayLists = Field<void*>(kit, smo::fields::LiveActorKit_modelDisplayListController);

        MainFunc<ObjFn>(smo::offsets::incrementDrawBufferCounter)(kit);
        MainFunc<ObjFn>(smo::offsets::waitUpdateDrawBuffer)(kit);

        /* The tick's effect update (updateKitListPost): swap vfx buffers, advance each group by this frame's share. */
        for (size_t i = 0; i < s_EffectSystemCount; ++i)
            EffectSystemPreprocess::Orig(s_EffectSystems[i]);
        for (size_t i = 0; i < s_EffectGroupCallCount; ++i) {
            const auto& call = s_EffectGroupCalls[i];
            VfxSystemCalculateGroup::Orig(call.system, call.group, call.rate * s_Clock.frameTicks, call.swapMode);
        }

        /* Stands in for the simulation's clearGraphicsRequest: update() appends every query again. */
        if (occlusion != nullptr)
            MainFunc<ObjFn>(smo::offsets::ModelOcclusionCullingDirector_clearRequest)(occlusion);
        UpdateKitListPostOnNerveEnd::Orig(scene);

        MainFunc<ObjFn>(smo::offsets::waitUpdateCalcView)(kit);
        if (displayLists != nullptr)
            MainFunc<ObjFn>(smo::offsets::ModelDisplayListController_update)(displayLists);
        MainFunc<ObjFn>(smo::offsets::executeUpdateDrawBuffer)(kit);
        if (occlusion != nullptr)
            MainFunc<ObjFn>(smo::offsets::ModelOcclusionCullingDirector_calc)(occlusion);
    }
}

HOOK_DEFINE_TRAMPOLINE(GameSystemMovement) {
    static void Callback(void* gameSystem) {
        /* The simulation (and our replay) must start from the camera the game itself produced. */
        smo::camera::Restore();

        if (s_Clock.runCalcThisFrame) {
            s_CameraCapturedThisTick = false;
            smo::actors::BeginTick();
            smo::effects::BeginTick();
            ForgetEffects();
            ++s_Report.ticks;
            Orig(gameSystem);
            return;
        }

        /* Draw-only frame: skip the simulation but keep the scene's per-frame render work paired with the draw. */
        if (s_GraphicsScene != nullptr)
            RefreshSceneGraphics(s_GraphicsScene);
    }
};

extern "C" void exl_main(void* x0, void* x1) {
    exl::hook::Initialize();

    /*
     * Pick the offset table from main's GNU build-id note. Every address below belongs to one build;
     * with no match, installing hooks would overwrite whatever happens to be at those offsets.
     */
    const auto& main = exl::util::GetMainModuleInfo().m_Total;
    const smo::offsets::Table* table =
        smo::offsets::Detect(reinterpret_cast<const void*>(main.m_Start), main.m_Size);
    if (table == nullptr) {
        Logging.Log("main is not a supported SMO build, not installing hooks");
        return;
    }
    smo::offsets::Select(*table);
    /* Bases, so a crash address in an emulator log can be turned back into an offset. */
    const auto& self = exl::util::GetSelfModuleInfo().m_Total;
    Logging.Log("using offsets for SMO %s (main at %p, mod at %p)", table->name,
                reinterpret_cast<void*>(main.m_Start), reinterpret_cast<void*>(self.m_Start));

    smo::nvn::InstallHooks();
    ProcFrame::InstallAtOffset(smo::offsets::GameFrameworkNx_procFrame);
    GameSystemMovement::InstallAtOffset(smo::offsets::GameSystem_movement);
    ControllerMgrCalc::InstallAtOffset(smo::offsets::ControllerMgr_calc);
    PrePassLightKeeperExecute::InstallAtOffset(smo::offsets::PrePassLightKeeper_execute);
    RainTextureKeeperUpdate::InstallAtOffset(smo::offsets::RainTextureKeeper_update);
    UpdateKitListPostOnNerveEnd::InstallAtOffset(smo::offsets::updateKitListPostOnNerveEnd);
    LiveActorKitPreDrawGraphics::InstallAtOffset(smo::offsets::LiveActorKit_preDrawGraphics);
    EffectSystemPreprocess::InstallAtOffset(smo::offsets::EffectSystem_preprocess);
    VfxSystemCalculateGroup::InstallAtOffset(smo::offsets::VfxSystem_CalculateGroup);
    FluidSimulateWaveUpdate::InstallAtOffset(smo::offsets::FluidSimulateWave_update);
    EmitterSetCalculate::InstallAtOffset(smo::offsets::EmitterSet_Calculate);
    EmitterSetInitialize::InstallAtOffset(smo::offsets::EmitterSet_Initialize);
    ModelCtrlUpdateModelDrawBuffer::InstallAtOffset(smo::offsets::ModelCtrl_updateModelDrawBuffer);
    SceneDtor::InstallAtOffset(smo::offsets::Scene_dtor);

    Logging.Log("hooks installed");
}

extern "C" NORETURN void exl_exception_entry() {
    EXL_ABORT("Default exception handler called!");
}
