#include "lib.hpp"
#include "actors.hpp"
#include "camera.hpp"
#include "config.hpp"
#include "effects.hpp"
#include "nvn.hpp"
#include "smo.hpp"

#include <algorithm>
#include <cstring>

using smo::Field;
using smo::MainFunc;

namespace {
    using ObjFn = void (*)(void*);

    bool IsSupportedBuild() {
        const auto& main = exl::util::GetMainModuleInfo().m_Total;
        if (main.m_Size < smo::BuildIdOffset + sizeof(smo::BuildId))
            return false;
        return std::memcmp(reinterpret_cast<const void*>(main.m_Start + smo::BuildIdOffset),
                           smo::BuildId, sizeof(smo::BuildId)) == 0;
    }

    /* The Switch system tick runs at 19.2 MHz. */
    constexpr u64 TicksPerSecond = 19'200'000;
    /* Game logic is authored for exactly 60 steps per second. */
    constexpr u64 TicksPerLogicStep = TicksPerSecond / 60;
    constexpr u64 ReportTicks = TicksPerSecond * 2;

    /*
     * Fixed-timestep accumulator. Each rendered frame adds its real elapsed time; the game tick
     * (GameSystem::movement) only runs when a whole logic step has accumulated. The leftover
     * fraction (s_Alpha) is how far the display is between the last two logic states.
     */
    u64 s_LastFrameTick = 0;
    u64 s_Accumulator = 0;
    bool s_RunCalcThisFrame = true;
    /* How far the displayed state is from the previous tick towards the newest one, in [0, 1]. */
    float s_Alpha = 1.f;
    /* This frame's real duration in ticks (~0.5 at 120 Hz); effects advance by this much per frame. */
    float s_FrameTicks = 1.f;

    void AdvanceClock() {
        u64 now = svcGetSystemTick();
        if (s_LastFrameTick == 0)
            s_LastFrameTick = now - TicksPerLogicStep;

        u64 elapsed = now - s_LastFrameTick;
        s_FrameTicks = std::min(float(elapsed) / float(TicksPerLogicStep), 2.f);

        /* Cap the debt at two steps so a long stall (loading, breakpoint) doesn't fast-forward. */
        s_Accumulator = std::min(s_Accumulator + elapsed, 2 * TicksPerLogicStep);
        s_LastFrameTick = now;

        /* At most one logic step per rendered frame: below 60 fps the game slows down, as in vanilla. */
        s_RunCalcThisFrame = s_Accumulator >= TicksPerLogicStep;
        if (s_RunCalcThisFrame)
            s_Accumulator -= TicksPerLogicStep;

        /* Rendering runs one tick behind: blend from the previous tick to the newest by the leftover time. */
        s_Alpha = smo::config::Get().interpolation ? std::min(float(s_Accumulator) / float(TicksPerLogicStep), 1.f) : 1.f;
    }

    u64 s_FrameCount = 0;
    u64 s_CalcCount = 0;
    u64 s_WindowStartTick = 0;
    u64 s_WindowStartFrame = 0;
    u64 s_WindowStartCalc = 0;

    void Report() {
        ++s_FrameCount;

        u64 now = svcGetSystemTick();
        u64 elapsed = now - s_WindowStartTick;
        if (s_WindowStartTick != 0 && elapsed < ReportTicks)
            return;

        if (s_WindowStartTick != 0) {
            u64 fps10 = (s_FrameCount - s_WindowStartFrame) * TicksPerSecond * 10 / elapsed;
            u64 calc10 = (s_CalcCount - s_WindowStartCalc) * TicksPerSecond * 10 / elapsed;
            Logging.Log("render %lu.%lu fps, logic %lu.%lu steps/s",
                        fps10 / 10, fps10 % 10, calc10 / 10, calc10 % 10);
        }
        s_WindowStartTick = now;
        s_WindowStartFrame = s_FrameCount;
        s_WindowStartCalc = s_CalcCount;
    }

    /* The scene whose graphics were updated by the last game tick, refreshed on draw-only frames. */
    void* s_GraphicsScene = nullptr;
    /* The camera is captured once per tick, at the tick's first preDrawGraphics. */
    bool s_CameraCapturedThisTick = false;

    /* The effect systems and groups the last tick updated, replayed on draw-only frames. */
    struct EffectGroupCall {
        void* system;
        int group;
        float rate;
        int swapMode;
    };
    constexpr size_t MaxEffectGroupCalls = 64;
    EffectGroupCall s_EffectGroupCalls[MaxEffectGroupCalls];
    size_t s_EffectGroupCallCount = 0;
    constexpr size_t MaxEffectSystems = 4;
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
        if (s_RunCalcThisFrame)
            Orig(controllerMgr);
    }
};

HOOK_DEFINE_TRAMPOLINE(PrePassLightKeeperExecute) {
    static void Callback(void* keeper, void* shadowDirector) {
        if (s_RunCalcThisFrame)
            Orig(keeper, shadowDirector);
    }
};

HOOK_DEFINE_TRAMPOLINE(UpdateKitListPostOnNerveEnd) {
    static void Callback(void* scene) {
        Orig(scene);
        s_GraphicsScene = scene;
    }
};

HOOK_DEFINE_TRAMPOLINE(LiveActorKitPreDrawGraphics) {
    static void Callback(void* kit) {
        void* director = Field<void*>(kit, smo::fields::LiveActorKit_cameraDirector);
        void* cameraInfo = director ? Field<void*>(director, smo::fields::CameraDirector_sceneCameraInfo) : nullptr;

        if (s_RunCalcThisFrame && !s_CameraCapturedThisTick) {
            smo::camera::Capture(cameraInfo);
            s_CameraCapturedThisTick = true;
        }
        smo::camera::Apply(cameraInfo, s_Alpha);

        Orig(kit);
    }
};

HOOK_DEFINE_TRAMPOLINE(EffectSystemPreprocess) {
    static void Callback(void* effectSystem) {
        auto* end = s_EffectSystems + s_EffectSystemCount;
        if (s_RunCalcThisFrame && s_EffectSystemCount < MaxEffectSystems && std::find(s_EffectSystems, end, effectSystem) == end)
            s_EffectSystems[s_EffectSystemCount++] = effectSystem;
        Orig(effectSystem);
    }
};

HOOK_DEFINE_TRAMPOLINE(VfxSystemCalculateGroup) {
    static void Callback(void* system, int group, float rate, int swapMode) {
        if (s_RunCalcThisFrame && s_EffectGroupCallCount < MaxEffectGroupCalls)
            s_EffectGroupCalls[s_EffectGroupCallCount++] = { system, group, rate, swapMode };
        Orig(system, group, rate * s_FrameTicks, swapMode);
    }
};

HOOK_DEFINE_TRAMPOLINE(EmitterSetCalculate) {
    static void Callback(void* emitterSet, float rate, int swapMode, bool flag, void* lodCallback) {
        smo::effects::ScopedBlend blend(emitterSet, s_Alpha);
        Orig(emitterSet, rate, swapMode, flag, lodCallback);
    }
};

HOOK_DEFINE_TRAMPOLINE(EmitterSetInitialize) {
    static void Callback(void* emitterSet, int a, int b, int c, int d, int e, void* heap) {
        smo::effects::Forget(emitterSet);
        Orig(emitterSet, a, b, c, d, e, heap);
    }
};

HOOK_DEFINE_TRAMPOLINE(UpdatePartsGraphics) {
    static void Callback(void* graphicsSystemInfo, void* updateInfo) {
        float& step = Field<float>(updateInfo, 0);
        float tickStep = step;
        step = tickStep * s_FrameTicks;
        Orig(graphicsSystemInfo, updateInfo);
        step = tickStep;
    }
};

HOOK_DEFINE_TRAMPOLINE(FluidSimulateWaveUpdate) {
    static void Callback(void* wave, void* updateInfo) {
        if (s_RunCalcThisFrame)
            Orig(wave, updateInfo);
    }
};

/* Both of ModelCtrl's GPU upload paths upload blended bone matrices. */
HOOK_DEFINE_TRAMPOLINE(ModelCtrlUpdateModelDrawBuffer) {
    static void Callback(void* modelCtrl, int bufferIndex) {
        smo::actors::ScopedBlend blend(modelCtrl, s_Alpha);
        Orig(modelCtrl, bufferIndex);
    }
};

HOOK_DEFINE_TRAMPOLINE(ModelCtrlUpdateGpuBuffer) {
    static void Callback(void* modelCtrl, int bufferIndex) {
        smo::actors::ScopedBlend blend(modelCtrl, s_Alpha);
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
    /* al::Scene::movement minus updateNerve/control, keeping every render-side step paired with this frame's draw. */
    void RefreshSceneGraphics(void* scene) {
        void* kit = Field<void*>(scene, smo::fields::Scene_liveActorKit);
        if (kit == nullptr)
            return;

        void* graphics = Field<void*>(kit, smo::fields::LiveActorKit_graphicsSystemInfo);
        void* occlusion = graphics ? Field<void*>(graphics, smo::fields::GraphicsSystemInfo_modelOcclusionCulling) : nullptr;
        void* displayLists = Field<void*>(kit, smo::fields::LiveActorKit_modelDisplayListController);

        MainFunc<ObjFn>(smo::offsets::incrementDrawBufferCounter)(kit);
        MainFunc<ObjFn>(smo::offsets::waitUpdateDrawBuffer)(kit);

        /* The tick's effect update (updateKitListPost): swap vfx buffers, then advance each group by this frame's share. */
        for (size_t i = 0; i < s_EffectSystemCount; ++i)
            EffectSystemPreprocess::Orig(s_EffectSystems[i]);
        for (size_t i = 0; i < s_EffectGroupCallCount; ++i) {
            const auto& call = s_EffectGroupCalls[i];
            VfxSystemCalculateGroup::Orig(call.system, call.group, call.rate * s_FrameTicks, call.swapMode);
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

        if (s_RunCalcThisFrame) {
            s_CameraCapturedThisTick = false;
            smo::actors::BeginTick();
            smo::effects::BeginTick();
            ForgetEffects();
            ++s_CalcCount;
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

    /* Every offset below is for 1.0.0; hooking any other build would corrupt random code. */
    if (!IsSupportedBuild()) {
        Logging.Log("main is not SMO 1.0.0 (3CA12DFA...), not installing hooks");
        return;
    }

    smo::nvn::InstallHooks();
    ProcFrame::InstallAtOffset(smo::offsets::GameFrameworkNx_procFrame);
    GameSystemMovement::InstallAtOffset(smo::offsets::GameSystem_movement);
    ControllerMgrCalc::InstallAtOffset(smo::offsets::ControllerMgr_calc);
    PrePassLightKeeperExecute::InstallAtOffset(smo::offsets::PrePassLightKeeper_execute);
    UpdateKitListPostOnNerveEnd::InstallAtOffset(smo::offsets::updateKitListPostOnNerveEnd);
    LiveActorKitPreDrawGraphics::InstallAtOffset(smo::offsets::LiveActorKit_preDrawGraphics);
    EffectSystemPreprocess::InstallAtOffset(smo::offsets::EffectSystem_preprocess);
    VfxSystemCalculateGroup::InstallAtOffset(smo::offsets::VfxSystem_CalculateGroup);
    UpdatePartsGraphics::InstallAtOffset(smo::offsets::GraphicsSystemInfo_updatePartsGraphics);
    FluidSimulateWaveUpdate::InstallAtOffset(smo::offsets::FluidSimulateWave_update);
    EmitterSetCalculate::InstallAtOffset(smo::offsets::EmitterSet_Calculate);
    EmitterSetInitialize::InstallAtOffset(smo::offsets::EmitterSet_Initialize);
    ModelCtrlUpdateModelDrawBuffer::InstallAtOffset(smo::offsets::ModelCtrl_updateModelDrawBuffer);
    ModelCtrlUpdateGpuBuffer::InstallAtOffset(smo::offsets::ModelCtrl_updateGpuBuffer);
    SceneDtor::InstallAtOffset(smo::offsets::Scene_dtor);

    Logging.Log("hooks installed");
}

extern "C" NORETURN void exl_exception_entry() {
    EXL_ABORT("Default exception handler called!");
}
