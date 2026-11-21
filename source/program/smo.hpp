#pragma once

#include "lib.hpp"

#include <atomic>
#include <cstring>

/* Logs the first time a fixed-size limit is hit, which otherwise fails silently (unblended or unreplayed objects). */
#define SMO_WARN_ONCE(...)                                                                                             \
    do {                                                                                                               \
        static std::atomic<bool> s_Warned { false };                                                                   \
        if (!s_Warned.exchange(true, std::memory_order_relaxed))                                                       \
            Logging.Log(__VA_ARGS__);                                                                                  \
    } while (0)

namespace smo {

    /* A field of a game object, by byte offset. Writable only through a non-const object pointer. */
    template<typename T>
    T& Field(void* object, ptrdiff_t offset) {
        return *reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(object) + offset);
    }

    template<typename T>
    const T& Field(const void* object, ptrdiff_t offset) {
        return *reinterpret_cast<const T*>(reinterpret_cast<uintptr_t>(object) + offset);
    }

    /* A function in main, by .text offset. */
    template<typename Fn>
    Fn MainFunc(ptrdiff_t offset) {
        return reinterpret_cast<Fn>(exl::util::modules::GetTargetOffset(offset));
    }
}

/* Field offsets. Identical in every supported build except where noted. */
namespace smo::fields {
    constexpr ptrdiff_t Scene_liveActorKit = 0x90;
    constexpr ptrdiff_t LiveActorKit_graphicsSystemInfo = 0x38;
    constexpr ptrdiff_t LiveActorKit_modelDisplayListController = 0x60;
    /* The one field that moved between 1.0.0 and 1.3.0; filled in by offsets::Select. */
    inline ptrdiff_t GraphicsSystemInfo_modelOcclusionCulling = 0x988;
    constexpr ptrdiff_t LiveActorKit_cameraDirector = 0x78;
    constexpr ptrdiff_t CameraDirector_sceneCameraInfo = 0x10;
    constexpr ptrdiff_t SceneCameraInfo_viewNumMax = 0x0;
    constexpr ptrdiff_t SceneCameraInfo_views = 0x8;
    constexpr ptrdiff_t CameraViewInfo_isValid = 0x4;
    constexpr ptrdiff_t CameraViewInfo_lookAtCam = 0x8;
    constexpr ptrdiff_t CameraViewInfo_projection = 0x10;

    /* sead::LookAtCamera (from doUpdateMatrix): vtable, Matrix34f view matrix, then pos / at / up. */
    constexpr ptrdiff_t LookAtCamera_matrix = 0x08;
    constexpr ptrdiff_t LookAtCamera_pos = 0x38;
    constexpr ptrdiff_t LookAtCamera_at = 0x44;
    constexpr ptrdiff_t LookAtCamera_up = 0x50;

    /*
     * al::Projection: sead::PerspectiveProjection's parameters (near, far, fovy, the fovy's cached
     * sin / cos / tan, then aspect) start at 0x98, as al::Projection::setProj writes them. The whole
     * object is 0x1f0 bytes (OdysseyDecomp's Projection.h ends with mOffset at 0x1e4..0x1ec).
     */
    constexpr ptrdiff_t Projection_perspectiveParams = 0x98;
    constexpr ptrdiff_t Projection_aspect = 0xb0;
    constexpr size_t Projection_size = 0x1f0;

    /*
     * nn::vfx::EmitterSet (from SetMatrix/SetPos): the placement is the SRT matrix at 0x60 and its
     * scale-free RT copy at 0xa0, each four float4s with the translation last. The flag at 0x18,
     * set by both, tells the set's emitters to rebuild their matrices from the placement.
     */
    constexpr ptrdiff_t EmitterSet_placement = 0x60;
    constexpr ptrdiff_t EmitterSet_matrixDirty = 0x18;

    /* al::ModelCtrl -> nn::g3d::ModelObj -> nn::g3d::SkeletonObj (from ModelCtrl::calc, CalculateWorldImpl). */
    constexpr ptrdiff_t ModelCtrl_modelObj = 0x0;
    constexpr ptrdiff_t ModelObj_skeleton = 0x38;
    constexpr ptrdiff_t SkeletonObj_worldMtxArray = 0x20;
    constexpr ptrdiff_t SkeletonObj_boneCount = 0x50;
}

/*
 * Function offsets and layout differences, one table per game build. The tables are selected once,
 * in exl_main, from main's GNU build-id note; the offsets below are then read by the hooks.
 *
 * 1.0.0 is OdysseyDecomp's map. 1.3.0 is the same bytes as 1.4.0 and 1.4.1 on Switch, so one table
 * covers all three.
 */
namespace smo::offsets {

    struct Table {
        /* Version name, for the log. */
        const char* name;
        /* Where main's GNU build-id note payload sits in the mapped image, and the 16 bytes it holds. */
        u32 buildIdOffset;
        u8 buildId[16];
        ptrdiff_t GraphicsSystemInfo_modelOcclusionCulling;

        /*
         * al::GameFrameworkNx::procFrame_ (overriding sead's) runs one whole frame through the vtable:
         * present_ (previous frame), procCalc_, procDraw_, waitForGpuDone_.
         */
        ptrdiff_t GameFrameworkNx_procFrame;

        /*
         * The whole game tick: GamePadSystem::update, the sequence/scene nerve, AudioSystem::update.
         * Everything else in procCalc_ is sead/agl framework work that the renderer expects exactly
         * once per procDraw_ (e.g. agl::utl::DynamicTextureAllocator's deferred-free bookkeeping).
         */
        ptrdiff_t GameSystem_movement;

        /*
         * Scenes end every tick with al::updateKitListPostOnNerveEnd: LiveActorKit::updateGraphics
         * (per-actor async graphics jobs, GraphicsSystemInfo::updateGraphics) then preDrawGraphics.
         * The scene's draw depends on it running once per frame.
         */
        ptrdiff_t updateKitListPostOnNerveEnd;

        /*
         * sead::ControllerMgr::calc polls every controller and derives trigger/release edges from the
         * previous poll. It runs in the framework's procCalc_, so it must be gated with the game tick
         * or presses that start on a draw-only frame lose their trigger edge before the game sees them.
         */
        ptrdiff_t ControllerMgr_calc;

        /*
         * al::Scene::movement brackets the simulation (updateNerve + control) with per-frame render
         * bookkeeping. Draw-only frames replay that bookkeeping without the simulation:
         *   incrementDrawBufferCounter, waitUpdateDrawBuffer,
         *   [sim: clearGraphicsRequest ... updateKitListPostOnNerveEnd],
         *   waitUpdateCalcView, ModelDisplayListController::update, executeUpdateDrawBuffer,
         *   ModelOcclusionCullingDirector::calc.
         */
        ptrdiff_t incrementDrawBufferCounter;
        ptrdiff_t waitUpdateDrawBuffer;
        ptrdiff_t waitUpdateCalcView;
        ptrdiff_t executeUpdateDrawBuffer;
        ptrdiff_t ModelDisplayListController_update;
        ptrdiff_t ModelOcclusionCullingDirector_clearRequest;
        ptrdiff_t ModelOcclusionCullingDirector_calc;

        /*
         * PrePassLightKeeper::execute has every registered light submit itself into the light buffers,
         * which only clearGraphicsRequest empties (along with transient requestPointLight calls made by
         * the simulation). Draw-only frames skip it: the buffers already hold this tick's lights, and
         * resubmitting overflows them so lights drop out (Peach's Castle flickering darker).
         */
        ptrdiff_t PrePassLightKeeper_execute;

        /*
         * al::RainTextureKeeper::update, reached from GraphicsSystemInfo::updateGraphics. It feeds
         * raindrops into the ripple simulation (FluidSimulateWave::storePoint), whose step only runs
         * on game ticks: left on every frame it adds points twice per step and the ripple buffers
         * overflow (crash in RainTextureKeeper::update via PointSpriteCursor::store).
         * Found by following updateKitListPostOnNerveEnd -> LiveActorKit::updateGraphics ->
         * GraphicsSystemInfo::updateGraphics -> thunk -> here, not from OdysseyDecomp.
         */
        ptrdiff_t RainTextureKeeper_update;

        /*
         * Camera interpolation point: runs at the end of every scene tick (and in our draw-only replay),
         * after the camera for the tick is final and before GraphicsSystemInfo::preDrawGraphics copies it
         * into the renderer's ViewInfo, view-frustum culling and GPU view uniforms.
         */
        ptrdiff_t LiveActorKit_preDrawGraphics;
        ptrdiff_t LookAtCamera_doUpdateMatrix;
        /* al::Projection::setProj(near, far, fovy, aspect) and calcMtx: a full projection rebuild,
         * for fovy blending. */
        ptrdiff_t Projection_setProj;
        ptrdiff_t Projection_calcMtx;

        /*
         * Per-frame GPU uploads of a model's skeleton (bone world matrices -> matrix palette), shape and
         * view matrices: nn::g3d::ModelObj::CalculateSkeleton/Shape/View. Run on worker threads from
         * executeUpdateDrawBuffer; the world matrices themselves are only recomputed on game ticks.
         */
        ptrdiff_t ModelCtrl_updateModelDrawBuffer;

        /*
         * Effects (nn::vfx). Each tick, the scene's effect lists run EffectSystem::preprocess
         * (vfx BeginFrame + SwapBuffer + effect UBO swap), then one EffectGroupDrawer per group calls
         * System::Calculate(group, 1.0 or 0.0 when paused, swapMode), then postprocess (new emitter sets,
         * frame counter). We scale each group's rate by the frame's share of a tick and replay
         * preprocess + the recorded groups on draw-only frames, so particles advance every frame.
         */
        ptrdiff_t EffectSystem_preprocess;
        ptrdiff_t VfxSystem_CalculateGroup;
        /* Every emitter set calculation (group, new-set and forced paths) and set (re)initialization. */
        ptrdiff_t EmitterSet_Calculate;
        ptrdiff_t EmitterSet_Initialize;

        /*
         * FluidSimulateWave (ripples): its calcGpu runs one fixed simulation step whenever its time
         * changed at all, so it only advances on logic frames.
         */
        ptrdiff_t FluidSimulateWave_update;

        /* al::Scene::~Scene (D1/D2); every derived scene destructor ends up here. */
        ptrdiff_t Scene_dtor;
    };

    inline constexpr Table s_1_0_0 = {
        .name = "1.0.0",
        .buildIdOffset = 0x1c4d024,
        .buildId = { 0x3c, 0xa1, 0x2d, 0xfa, 0xaf, 0x9c, 0x82, 0xda, 0x06, 0x4d, 0x16, 0x98, 0xdf, 0x79, 0xcd, 0xa1 },
        .GraphicsSystemInfo_modelOcclusionCulling = 0x988,

        .GameFrameworkNx_procFrame = 0x8a6ab4,
        .GameSystem_movement = 0x536614,
        .updateKitListPostOnNerveEnd = 0x9d0ce4,
        .ControllerMgr_calc = 0x75b9a0,
        .incrementDrawBufferCounter = 0xa69b9c,
        .waitUpdateDrawBuffer = 0xa69b24,
        .waitUpdateCalcView = 0xa69b54,
        .executeUpdateDrawBuffer = 0xa69b1c,
        .ModelDisplayListController_update = 0x941878,
        .ModelOcclusionCullingDirector_clearRequest = 0x94ce1c,
        .ModelOcclusionCullingDirector_calc = 0x94cee8,
        .PrePassLightKeeper_execute = 0x8c23fc,
        .RainTextureKeeper_update = 0xa2a244,
        .LiveActorKit_preDrawGraphics = 0x910650,
        .LookAtCamera_doUpdateMatrix = 0x75c238,
        .Projection_setProj = 0x9c05f0,
        .Projection_calcMtx = 0x9bffe8,
        .ModelCtrl_updateModelDrawBuffer = 0x93fe7c,
        .EffectSystem_preprocess = 0x887454,
        .VfxSystem_CalculateGroup = 0xb38fd4,
        .EmitterSet_Calculate = 0xb2b274,
        .EmitterSet_Initialize = 0xb2a928,
        .FluidSimulateWave_update = 0x899510,
        .Scene_dtor = 0x9ce52c,
    };

    inline constexpr Table s_1_3_0 = {
        .name = "1.3.0/1.4.0/1.4.1",
        .buildIdOffset = 0x1199dbc,
        .buildId = { 0xb4, 0x24, 0xbe, 0x15, 0x0a, 0x8e, 0x7d, 0x78, 0x70, 0x1c, 0xbe, 0x7a, 0x43, 0x9d, 0x9e, 0xbf },
        .GraphicsSystemInfo_modelOcclusionCulling = 0x998,

        .GameFrameworkNx_procFrame = 0x625250,
        .GameSystem_movement = 0x4e2730,
        .updateKitListPostOnNerveEnd = 0x74eab0,
        .ControllerMgr_calc = 0x964b10,
        .incrementDrawBufferCounter = 0x74cd80,
        .waitUpdateDrawBuffer = 0x74cd20,
        .waitUpdateCalcView = 0x74cd30,
        .executeUpdateDrawBuffer = 0x74cd10,
        .ModelDisplayListController_update = 0x6adde0,
        .ModelOcclusionCullingDirector_clearRequest = 0x6bb280,
        .ModelOcclusionCullingDirector_calc = 0x6bb370,
        .PrePassLightKeeper_execute = 0x6450b0,
        .RainTextureKeeper_update = 0x7ab0a0,
        .LiveActorKit_preDrawGraphics = 0x6820a0,
        .LookAtCamera_doUpdateMatrix = 0x9661a0,
        .Projection_setProj = 0x73de60,
        .Projection_calcMtx = 0x73da10,
        .ModelCtrl_updateModelDrawBuffer = 0x6ac390,
        .EffectSystem_preprocess = 0x601730,
        .VfxSystem_CalculateGroup = 0x91d97c,
        .EmitterSet_Calculate = 0x914934,
        .EmitterSet_Initialize = 0x9140cc,
        .FluidSimulateWave_update = 0x6173b0,
        .Scene_dtor = 0x74c7e0,
    };

    /*
     * The active build's table. Select() copies the detected table over it; the names below alias
     * its members, so a field added to Table is picked up without touching Select().
     */
    inline Table s_Active = s_1_0_0;

    inline ptrdiff_t& GameFrameworkNx_procFrame = s_Active.GameFrameworkNx_procFrame;
    inline ptrdiff_t& GameSystem_movement = s_Active.GameSystem_movement;
    inline ptrdiff_t& updateKitListPostOnNerveEnd = s_Active.updateKitListPostOnNerveEnd;
    inline ptrdiff_t& ControllerMgr_calc = s_Active.ControllerMgr_calc;
    inline ptrdiff_t& incrementDrawBufferCounter = s_Active.incrementDrawBufferCounter;
    inline ptrdiff_t& waitUpdateDrawBuffer = s_Active.waitUpdateDrawBuffer;
    inline ptrdiff_t& waitUpdateCalcView = s_Active.waitUpdateCalcView;
    inline ptrdiff_t& executeUpdateDrawBuffer = s_Active.executeUpdateDrawBuffer;
    inline ptrdiff_t& ModelDisplayListController_update = s_Active.ModelDisplayListController_update;
    inline ptrdiff_t& ModelOcclusionCullingDirector_clearRequest = s_Active.ModelOcclusionCullingDirector_clearRequest;
    inline ptrdiff_t& ModelOcclusionCullingDirector_calc = s_Active.ModelOcclusionCullingDirector_calc;
    inline ptrdiff_t& PrePassLightKeeper_execute = s_Active.PrePassLightKeeper_execute;
    inline ptrdiff_t& RainTextureKeeper_update = s_Active.RainTextureKeeper_update;
    inline ptrdiff_t& LiveActorKit_preDrawGraphics = s_Active.LiveActorKit_preDrawGraphics;
    inline ptrdiff_t& LookAtCamera_doUpdateMatrix = s_Active.LookAtCamera_doUpdateMatrix;
    inline ptrdiff_t& Projection_setProj = s_Active.Projection_setProj;
    inline ptrdiff_t& Projection_calcMtx = s_Active.Projection_calcMtx;
    inline ptrdiff_t& ModelCtrl_updateModelDrawBuffer = s_Active.ModelCtrl_updateModelDrawBuffer;
    inline ptrdiff_t& EffectSystem_preprocess = s_Active.EffectSystem_preprocess;
    inline ptrdiff_t& VfxSystem_CalculateGroup = s_Active.VfxSystem_CalculateGroup;
    inline ptrdiff_t& EmitterSet_Calculate = s_Active.EmitterSet_Calculate;
    inline ptrdiff_t& EmitterSet_Initialize = s_Active.EmitterSet_Initialize;
    inline ptrdiff_t& FluidSimulateWave_update = s_Active.FluidSimulateWave_update;
    inline ptrdiff_t& Scene_dtor = s_Active.Scene_dtor;

    inline void Select(const Table& table) {
        s_Active = table;
        fields::GraphicsSystemInfo_modelOcclusionCulling = table.GraphicsSystemInfo_modelOcclusionCulling;
    }

    /*
     * Identifies the loaded build by its GNU build-id note. The note sits at a different offset in
     * each layout, so each table names its own; the image cannot be hooked safely without a match.
     */
    inline const Table* Detect(const void* image, size_t size) {
        if (image == nullptr)
            return nullptr;
        const Table* tables[] = { &s_1_0_0, &s_1_3_0 };
        for (const Table* table : tables) {
            if (table->buildIdOffset + sizeof(table->buildId) > size)
                continue;
            if (std::memcmp(static_cast<const u8*>(image) + table->buildIdOffset, table->buildId,
                            sizeof(table->buildId)) == 0)
                return table;
        }
        return nullptr;
    }
}
