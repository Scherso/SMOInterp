#pragma once

#include "lib.hpp"

/* SMO 1.0.0 (main build ID 3CA12DFAAF9C82DA064D1698DF79CDA1). Offsets from OdysseyDecomp data/file_list.yml. */
namespace smo {
    /* Payload of main's GNU build-id note, which is mapped in memory at BuildIdOffset. */
    constexpr ptrdiff_t BuildIdOffset = 0x1c4d024;
    constexpr u8 BuildId[16] = {
        0x3c, 0xa1, 0x2d, 0xfa, 0xaf, 0x9c, 0x82, 0xda, 0x06, 0x4d, 0x16, 0x98, 0xdf, 0x79, 0xcd, 0xa1,
    };

    /* A field of a game object, by byte offset. */
    template<typename T>
    T& Field(const void* object, ptrdiff_t offset) {
        return *reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(object) + offset);
    }

    /* A function in main, by .text offset. */
    template<typename Fn>
    Fn MainFunc(ptrdiff_t offset) {
        return reinterpret_cast<Fn>(exl::util::modules::GetTargetOffset(offset));
    }
}

namespace smo::offsets {
    /*
     * al::GameFrameworkNx::procFrame_ (overriding sead's) runs one whole frame through the vtable:
     * present_ (previous frame), procCalc_, procDraw_, waitForGpuDone_.
     */
    constexpr ptrdiff_t GameFrameworkNx_procFrame = 0x8a6ab4;

    /*
     * The whole game tick: GamePadSystem::update, the sequence/scene nerve, AudioSystem::update.
     * Everything else in procCalc_ is sead/agl framework work that the renderer expects exactly
     * once per procDraw_ (e.g. agl::utl::DynamicTextureAllocator's deferred-free bookkeeping).
     */
    constexpr ptrdiff_t GameSystem_movement = 0x536614;

    /*
     * Scenes end every tick with al::updateKitListPostOnNerveEnd: LiveActorKit::updateGraphics
     * (per-actor async graphics jobs, GraphicsSystemInfo::updateGraphics) then preDrawGraphics.
     * The scene's draw depends on it running once per frame.
     */
    constexpr ptrdiff_t updateKitListPostOnNerveEnd = 0x9d0ce4;
    /*
     * sead::ControllerMgr::calc polls every controller and derives trigger/release edges from the
     * previous poll. It runs in the framework's procCalc_, so it must be gated with the game tick
     * or presses that start on a draw-only frame lose their trigger edge before the game sees them.
     */
    constexpr ptrdiff_t ControllerMgr_calc = 0x75b9a0;
    /*
     * al::Scene::movement brackets the simulation (updateNerve + control) with per-frame render
     * bookkeeping. Draw-only frames replay that bookkeeping without the simulation:
     *   incrementDrawBufferCounter, waitUpdateDrawBuffer,
     *   [sim: clearGraphicsRequest ... updateKitListPostOnNerveEnd],
     *   waitUpdateCalcView, ModelDisplayListController::update, executeUpdateDrawBuffer,
     *   ModelOcclusionCullingDirector::calc.
     */
    constexpr ptrdiff_t incrementDrawBufferCounter = 0xa69b9c;
    constexpr ptrdiff_t waitUpdateDrawBuffer = 0xa69b24;
    constexpr ptrdiff_t waitUpdateCalcView = 0xa69b54;
    constexpr ptrdiff_t executeUpdateDrawBuffer = 0xa69b1c;
    constexpr ptrdiff_t ModelDisplayListController_update = 0x941878;
    constexpr ptrdiff_t ModelOcclusionCullingDirector_clearRequest = 0x94ce1c;
    constexpr ptrdiff_t ModelOcclusionCullingDirector_calc = 0x94cee8;

    /*
     * PrePassLightKeeper::execute has every registered light submit itself into the light buffers,
     * which only clearGraphicsRequest empties (along with transient requestPointLight calls made by
     * the simulation). Draw-only frames skip it: the buffers already hold this tick's lights, and
     * resubmitting overflows them so lights drop out (Peach's Castle flickering darker).
     */
    constexpr ptrdiff_t PrePassLightKeeper_execute = 0x8c23fc;

    /*
     * Camera interpolation point: runs at the end of every scene tick (and in our draw-only replay),
     * after the camera for the tick is final and before GraphicsSystemInfo::preDrawGraphics copies it
     * into the renderer's ViewInfo, view-frustum culling and GPU view uniforms.
     */
    constexpr ptrdiff_t LiveActorKit_preDrawGraphics = 0x910650;
    constexpr ptrdiff_t LookAtCamera_doUpdateMatrix = 0x75c238;
    /* al::Projection::setProj(near, far, fovy, aspect) and calcMtx: a full projection rebuild, for fovy blending. */
    constexpr ptrdiff_t Projection_setProj = 0x9c05f0;
    constexpr ptrdiff_t Projection_calcMtx = 0x9bffe8;

    /*
     * Per-frame GPU uploads of a model's skeleton (bone world matrices -> matrix palette), shape and
     * view matrices: nn::g3d::ModelObj::CalculateSkeleton/Shape/View. Run on worker threads from
     * executeUpdateDrawBuffer; the world matrices themselves are only recomputed on game ticks.
     */
    constexpr ptrdiff_t ModelCtrl_updateModelDrawBuffer = 0x93fe7c;
    constexpr ptrdiff_t ModelCtrl_updateGpuBuffer = 0x93fbc0;

    /*
     * Effects (nn::vfx). Each tick, the scene's effect lists run EffectSystem::preprocess
     * (vfx BeginFrame + SwapBuffer + effect UBO swap), then one EffectGroupDrawer per group calls
     * System::Calculate(group, 1.0 or 0.0 when paused, swapMode), then postprocess (new emitter sets,
     * frame counter). We scale each group's rate by the frame's share of a tick and replay
     * preprocess + the recorded groups on draw-only frames, so particles advance every frame.
     */
    constexpr ptrdiff_t EffectSystem_preprocess = 0x887454;
    constexpr ptrdiff_t VfxSystem_CalculateGroup = 0xb38fd4;
    /* Every emitter set calculation (group, new-set and forced paths) and set (re)initialization. */
    constexpr ptrdiff_t EmitterSet_Calculate = 0xb2b274;
    constexpr ptrdiff_t EmitterSet_Initialize = 0xb2a928;

    /*
     * "Parts graphics" (water, ocean, sky, clouds, noise textures...) are updated every
     * updateGraphics with a GraphicsUpdateInfo whose first field is a time step (1.0 per tick) that
     * they accumulate. We scale it to each frame's share of a tick. FluidSimulateWave (ripples) is
     * the exception: its calcGpu runs one fixed simulation step whenever its time changed at all, so
     * it only advances on logic frames.
     */
    constexpr ptrdiff_t GraphicsSystemInfo_updatePartsGraphics = 0x8799b4;
    constexpr ptrdiff_t FluidSimulateWave_update = 0x899510;

    /* al::Scene::~Scene (D1/D2); every derived scene destructor ends up here. */
    constexpr ptrdiff_t Scene_dtor = 0x9ce52c;
}

/* Field offsets, as used by al::Scene::movement and al::updateKitListPostOnNerveEnd in 1.0.0. */
namespace smo::fields {
    constexpr ptrdiff_t Scene_liveActorKit = 0x90;
    constexpr ptrdiff_t LiveActorKit_graphicsSystemInfo = 0x38;
    constexpr ptrdiff_t LiveActorKit_modelDisplayListController = 0x60;
    constexpr ptrdiff_t GraphicsSystemInfo_modelOcclusionCulling = 0x988;
    constexpr ptrdiff_t LiveActorKit_cameraDirector = 0x78;
    constexpr ptrdiff_t CameraDirector_sceneCameraInfo = 0x10;
    constexpr ptrdiff_t SceneCameraInfo_viewNumMax = 0x0;
    constexpr ptrdiff_t SceneCameraInfo_views = 0x8;
    constexpr ptrdiff_t CameraViewInfo_isValid = 0x4;
    constexpr ptrdiff_t CameraViewInfo_lookAtCam = 0x8;
    constexpr ptrdiff_t CameraViewInfo_projection = 0x10;

    /* al::ModelCtrl -> nn::g3d::ModelObj -> nn::g3d::SkeletonObj (from ModelCtrl::calc, CalculateWorldImpl). */
    constexpr ptrdiff_t ModelCtrl_modelObj = 0x0;
    constexpr ptrdiff_t ModelObj_skeleton = 0x38;
    constexpr ptrdiff_t SkeletonObj_worldMtxArray = 0x20;
    constexpr ptrdiff_t SkeletonObj_boneCount = 0x50;
}
