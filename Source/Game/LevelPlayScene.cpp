#include "Game/LevelPlayScene.h"

#include "Game/Block.h"
#include "Game/Blocks/DecorationBlock.h"
#include "Game/Blocks/HazardBlock.h"
#include "Game/Blocks/PoleBlock.h"
#include "Game/Blocks/SlopeBlock.h"
#include "Game/Blocks/WaterBlock.h"
#include "Game/Player.h"
#include "Game/SkinnedDebugCharacter.h"
#include "Game/Undo/EditTarget.h"

#include "Framework/Scene/AssetManager.h"
#include "Framework/Scene/Components/CameraBrainComponent.h"
#include "Framework/Scene/Components/CameraComponent.h"
#include "Framework/Scene/Components/HazardComponent.h"
#include "Framework/Scene/Components/PlacedVirtualCamera.h"
#include "Framework/Scene/Components/PoleComponent.h"
#include "Framework/Scene/GameObject.h"

#include "Framework/App/Application.h"
#include "Framework/Core/Clock.h"
#include "Framework/Core/Filesystem.h"
#include "Framework/Core/LogCategories.h"
#include "Framework/Core/Logger.h"
#include "Framework/Graphics/CommandList.h"
#include "Framework/Graphics/InstanceBatcher.h"
#include "Framework/Graphics/Material.h"
#include "Framework/Graphics/MeshPrimitives.h"
#include "Framework/Graphics/Renderer.h"
#include "Framework/Graphics/Shader.h"
#include "Framework/Graphics/Skybox.h"
#include "Framework/Graphics/StaticMesh.h"
#include "Framework/Graphics/Texture.h"
#include "Framework/Graphics/TextureArray.h"
#include "Framework/Physics/Capsule.h"
#include "Framework/Platform/Input.h"
#include "Framework/Platform/Keyboard.h"
#include "Framework/Platform/Window.h"
#include "Framework/Scene/Components/MeshRendererComponent.h"
#include "Framework/Scene/IRenderable.h"
#include "Framework/Scene/RenderContext.h"
#include "Framework/Scene/Transform.h"
#include "Game/Blocks/AutoTile.h"
#include "Game/Blocks/BlockRegistry.h"
#include "Game/Level/ChunkIO.h"
#include "Game/Theme/ThemeRegistry.h"

#include <algorithm>
#include <cmath>

namespace
{
    constexpr NS::Math::Vector3 kPlayerColor{0.85f, 0.20f, 0.20f};
    constexpr NS::Math::Vector3 kCellHalfExtents{0.5f, 0.5f, 0.5f};

    /// 編集体験の起点となる最小床。 LevelData に grid block 1 個 + spawn を仕込んでおく
    void SeedInitialLevel(NS::Game::Level::LevelData& level)
    {
        level.objects.clear();
        level.objects.push_back(NS::Game::Level::MakeGridObject(0, 0, 0, NS::Game::Blocks::kBlockIdSolid, 0));
        level.spawnX = 0;
        level.spawnY = 1;
        level.spawnZ = 0;
    }
} // namespace

LevelPlayScene::LevelPlayScene() = default;

LevelPlayScene::~LevelPlayScene() = default;

void LevelPlayScene::LoadInitialLevel()
{
    // 同梱 default レベルがあればそれを、 無ければ最小床を seed する
    const auto exeDir = NS::Core::FileSystem::GetExeDirectory();
    const auto defaultPath = exeDir / "Levels" / "default.nslvl";
    if (!NS::Game::Level::LoadLevelFromFile(m_level, defaultPath))
        SeedInitialLevel(m_level);
    RebuildObjectIds();
}

void LevelPlayScene::RebuildObjectIds() noexcept
{
    NS::Game::Undo::EditTarget target{m_level, m_objectIds, m_nextObjectId};
    NS::Game::Undo::ResetEditIds(target);
}

void LevelPlayScene::OnStart()
{
    auto* app = NS::App::Application::Get();
    if (app == nullptr)
    {
        NS_LOG_ERROR(::NS::Core::LogCat::Game, "LevelPlayScene::OnStart: Application::Get()==null");
        return;
    }

    auto& renderer = app->Renderer();
    const auto exeDir = NS::Core::FileSystem::ContentRoot();

    // builtin mesh と共有 material は AssetManager がアプリ寿命で所有する。 ここは使う時に引くだけ
    auto& assets = app->Assets();

    // 接地シャドウは Player が scene 寿命のあいだ参照を握る。 builtin quad + 共有 shadow material を渡す
    auto* shadowMesh = assets.Builtin("shadowQuad");
    auto* shadowMaterial = assets.SharedMaterial("shadow");

    // 全テーマ block texture を Texture2DArray 1 本に集約。 アセット未取得のため cube_test.png を 40 slice 充填
    {
        NS::Graphics::TextureArrayDesc taDesc{};
        const auto placeholderSlice = exeDir / "Assets" / "Textures" / "cube_test.png";
        constexpr std::size_t kPlaceholderSliceCount = 40; // 5 theme x 8 variant
        taDesc.slicePaths.reserve(kPlaceholderSliceCount);
        for (std::size_t i = 0; i < kPlaceholderSliceCount; ++i)
        {
            taDesc.slicePaths.push_back(placeholderSlice);
        }
        taDesc.generateMipmaps = true;
        taDesc.sRGB = false;
        auto* blockTextures = assets.GetOrCreateTextureArray("block", taDesc);
        if (blockTextures->IsUsingFallback())
            NS_LOG_WARN(::NS::Core::LogCat::Game,
                        "LevelPlayScene: block 用 TextureArray の slice 読込で失敗あり、 magenta fallback で続行");
    }

    m_instanceBatcher = NS::Graphics::InstanceBatcher::Create();
    if (!m_instanceBatcher->IsValid())
        NS_LOG_WARN(::NS::Core::LogCat::Game, "LevelPlayScene: InstanceBatcher 構築失敗、 block 描画はスキップされる");

    // placeholder skybox。 kurt 6-face PNG をロードし、 取得できなければ
    // 1x1 マゼンタ cubemap fallback で続行する (描画は OnRenderScene 末尾)
    m_skybox = NS::Graphics::Skybox::Create();
    if (m_skybox->IsValid())
    {
        const auto kurtDir = exeDir / "Assets" / "Skybox" / "kurt";
        if (!m_skybox->LoadCubemap(kurtDir))
            NS_LOG_WARN(::NS::Core::LogCat::Game,
                        "LevelPlayScene: kurt cubemap 読込失敗、 magenta fallback で続行: {}",
                        kurtDir.string());
    }
    else
    {
        NS_LOG_ERROR(::NS::Core::LogCat::Game, "LevelPlayScene: Skybox 構築失敗 (Device 不在?)");
    }

    m_player = std::make_unique<Player>(assets.Builtin("cube"), assets.SharedMaterial("player"), &app->Input());
    m_player->AttachScene(this);
    m_player->Root().SetPosition({0.0f, 1.0f, -4.0f});
    // cube mesh の半サイズは 0.5 だが capsule collider は radius=0.4 / halfHeight=0.5
    // (= AABB 半サイズ 0.4, 0.9, 0.4)。両者が一致するよう scale で mesh を縮める
    m_player->Root().SetScale({0.8f, 1.8f, 0.8f});
    m_player->MeshComp().SetBaseColor(kPlayerColor);
    m_player->Shadow().SetResources(shadowMesh, shadowMaterial);

    LoadInitialLevel();
    RebuildBlocksFromLevelData();

    m_cameraRig = std::make_unique<CameraRig>(&app->Input(), &m_player->Root(), &m_player->Movement());
    m_cameraRig->AttachScene(this);
    // follow vcam の投影設定 (near 0.1 / fov 60 は既定、 play は遠景を 100 までに抑える)
    m_cameraRig->Follow().SetFarPlane(100.0f);

    m_player->OnStart();
    m_cameraRig->OnStart();

    // 実カメラ 1 個 + Brain を載せる host を作り、 follow vcam を登録する
    // 描画 / aspect / PlayerInput forward は全て Brain 出力カメラへ集約する
    m_cameraHost = std::make_unique<NS::Scene::GameObject>();
    m_mainCamera = m_cameraHost->AddComponent<NS::Scene::CameraComponent>();
    m_brain = m_cameraHost->AddComponent<NS::Scene::CameraBrainComponent>();
    m_brain->SetCamera(m_mainCamera);
    m_mainCamera->SetAspectRatioFromRenderer(renderer);
    m_mainCamera->SetUp({0.0f, 1.0f, 0.0f});
    m_brain->AddVirtualCamera(&m_cameraRig->Follow());
    m_cameraHost->AttachScene(this);
    m_cameraHost->OnStart();

    // level の cameraVolumes から area camera を生成し Brain へ登録する (Brain 構築後に呼ぶ必要がある)
    RebuildAreaCamerasFromLevelData();

    // 仮 skinned キャラをプレイ画面で常時表示し、 アニメ再生を画面で確認できるようにする
    // 形 / 骨 / 材質は AssetManager 所有を借り、 アセットが無ければ Create が nullptr を返して通常進行する
    m_animatedModel = SkinnedDebugCharacter::Create(
        assets, this, exeDir / "Assets" / "Models", exeDir / "Assets" / "Materials" / "skinned_debug.mat");

    // 出荷も開発も、 起動直後はプレイ可能な状態にする。 開発時は editor が直後に編集モードへ切替える
    SetPlaying(true);
}

void LevelPlayScene::SetPlaying(bool playing) noexcept
{
    m_playing = playing;
    if (playing)
    {
        // 編集中の変形を確定した最新 level でプレイするため、 collision snapshot を作り直す
        // CommitTransformEdit は dirty を立てないため、 ここで突入時に一度作り直して取りこぼしを防ぐ
        RebuildBlocksFromLevelData();
        // spawn を計算して player をそこへ置き、 物理 / 入力 / follow camera を有効化する
        m_playMode.Enter(m_level, m_play);
        m_playMode.SetActive(true);
        if (m_player)
        {
            m_player->MeshComp().SetActive(true);
            m_player->Movement().SetActive(true);
            m_player->InputComp().SetActive(true);
            m_player->Root().SetPosition(m_play.playerPosition);
            m_player->Movement().ResetState();
        }
        if (m_cameraRig)
            m_cameraRig->Follow().SetActive(true);
    }
    else
    {
        // 編集モードへ: paused/clear/death をリセットし player を凍結、 follow / area camera を休止する
        // (free-fly カメラは editor が握るため scene は触らない)
        m_playMode.Exit(m_play);
        m_playMode.SetActive(false);
        if (m_player)
        {
            m_player->MeshComp().SetActive(false);
            m_player->Movement().SetActive(false);
            m_player->InputComp().SetActive(false);
        }
        if (m_cameraRig)
            m_cameraRig->Follow().SetActive(false);
        for (auto& area : m_areaCameras)
        {
            if (area.cam)
                area.cam->SetActive(false);
        }
    }
}

void LevelPlayScene::OnUpdate()
{
    auto* app = NS::App::Application::Get();
    if (app == nullptr)
        return;

    // F5 で編集中の HLSL を再起動なしで反映する (reload-in-place、 play / edit 共通の dev hot reload)
    // ImGui 入力中は誤爆を防ぐため無効化する
    if (!app->Input().UiWantsKeyboard() && app->Input().Keyboard().IsPressed(NS::Platform::Key::F5))
        app->Assets().ReloadAllShaders();

    // プレイ中の Esc は終了。 編集中は editor が Esc を握る (選択解除 / 終了) ので scene は触らない
    if (m_playing && app->Input().Keyboard().IsPressed(NS::Platform::Key::Escape))
    {
        NS::App::Application::Quit();
        return;
    }

    // Application が Renderer::Resize を排他で握っているため、 Camera の aspect ratio は
    // Renderer の現在 Size から毎フレーム pull する (callback 上書きで競合させない)
    if (m_mainCamera)
        m_mainCamera->SetAspectRatioFromRenderer(app->Renderer());

    // active vcam が入れ替わったらブレンドを進める (play / edit 共通、 fixed step ごとに 1 度)
    if (m_brain)
        m_brain->OnUpdate();

    SnapshotDisplayBlocks();

    // 編集中はプレイ更新を止める。 free-fly カメラ / 編集入力は editor 側 (overlay layer) が回す
    if (m_playing)
        TickPlay();

    UpdateDisplayBlocks();

    UpdateAnimatedModel();
}

void LevelPlayScene::TickPlay()
{
    auto* app = NS::App::Application::Get();
    if (app == nullptr)
        return;

    const float dt = NS::Core::FrameTimer::FixedDelta();

    // camera 水平 forward を先に渡してから tick。 priority 順 PlayerInput→CharacterMovement で入力→物理が確定し
    // Transform に書かれる
    if (m_player)
    {
        NS::Math::Vector3 camForward{0.0f, 0.0f, 1.0f};
        if (m_brain)
            camForward = m_brain->ForwardHorizontal();
        m_player->InputComp().SetCameraForward(camForward);
        m_player->OnUpdate();

        // 落下死 / coin / star / hazard 判定が読む PlayState.playerPosition に Transform をミラーする
        m_play.playerPosition = m_player->Root().Position();
    }

    // Play のゲームルール (落下死 / coin / star)。 物理は持たず player 位置を読むだけ
    m_playMode.Tick(m_level, m_play, dt);

    // hazard は solid 衝突世界にも含まれ capsule 中心は表面外に留まるため芯線分から AABB の最近距離で判定する
    if (m_player)
    {
        NS::Physics::Capsule playerCapsule{};
        playerCapsule.center = m_player->Root().Position();
        playerCapsule.radius = m_player->Movement().CapsuleRadius();
        playerCapsule.halfHeight = m_player->Movement().CapsuleHalfHeight();
        for (auto& hazard : m_hazards)
        {
            if (!hazard)
                continue;
            if (NS::Physics::IntersectsCapsuleAabb(playerCapsule, hazard->Collider().WorldAABB()))
                hazard->Hazard().OnPlayerOverlap(m_play);
        }
    }

    // area camera: 各 vcam が自分のトリガ AABB でプレイヤー進入を判定し、自分を active 化する
    // active / 解除の切替は Brain が優先度で選びブレンドする
    for (auto& area : m_areaCameras)
    {
        if (area.cam)
            area.cam->UpdateActivation(m_play.playerPosition);
    }

    // 落下死 (PlayMode が y < kFallDeathThreshold で立てた deathTriggered) は respawn 経路
    // ハザード接触の死 (playerHealth==0) は Game.cpp が Application::Quit を呼ぶためここでは respawn しない
    if (m_play.deathTriggered && m_play.playerHealth > 0)
    {
        m_play.deathTriggered = false;
        m_playMode.Enter(m_level, m_play);
        if (m_player)
        {
            m_player->Root().SetPosition(m_play.playerPosition);
            m_player->Movement().ResetState();
        }
    }
    // clearTriggered のクリア後遷移 (編集へ戻る / 次レベル / リザルト) は scene の外で扱う
    // 開発時は editor が観測して編集モードへ戻す。 出荷のクリア演出は後続フェーズ

    if (m_player)
        m_player->Root().Snapshot();
    if (m_cameraRig)
    {
        m_cameraRig->Root().Snapshot();
        m_cameraRig->OnUpdate();
    }
}

void LevelPlayScene::SnapshotDisplayBlocks()
{
    // 各ブロック GameObject の Snapshot は edit / play 共通 (静的 display object なので常時)
    for (auto& block : m_blocks)
        block->Root().Snapshot();
    for (auto& slope : m_slopes)
        slope->Root().Snapshot();
    for (auto& pole : m_poles)
        pole->Root().Snapshot();
    for (auto& hazard : m_hazards)
        hazard->Root().Snapshot();
    for (auto& water : m_waters)
        water->Root().Snapshot();
    for (auto& deco : m_decorations)
        deco->Root().Snapshot();
    for (auto& obj : m_freeObjects)
        obj->Root().Snapshot();
}

void LevelPlayScene::UpdateDisplayBlocks()
{
    for (auto& block : m_blocks)
        block->OnUpdate();
    for (auto& slope : m_slopes)
        slope->OnUpdate();
    for (auto& pole : m_poles)
        pole->OnUpdate();
    for (auto& hazard : m_hazards)
        hazard->OnUpdate();
    for (auto& water : m_waters)
        water->OnUpdate();
    for (auto& deco : m_decorations)
        deco->OnUpdate();
}

NS::Graphics::RenderSettingsOverride LevelPlayScene::BuildSceneOverride()
{
    // 範囲外 themeId は ThemeRegistry::Get 側で Grass にフォールバックされる
    const ThemeData& theme = ThemeRegistry::Get(m_level.themeId);

    NS::Graphics::RenderSettingsOverride over{};
    if (theme.lightDirection.LengthSquared() > 1e-6f)
    {
        over.lightDir = theme.lightDirection;
    }
    else
    {
        // zero ベクトルは normalize で拡散光が無言で消えるため override せず既定 lightDir に落とす
        static bool s_warnedZeroLightDir = false;
        if (!s_warnedZeroLightDir)
        {
            NS_LOG_WARN(::NS::Core::LogCat::Game,
                        "LevelPlayScene: テーマの lightDirection が zero のため既定 lightDir で描画する");
            s_warnedZeroLightDir = true;
        }
    }
    over.lightColor = theme.lightColor;
    over.ambientColor = theme.ambientColor;
    return over;
}

void LevelPlayScene::OnRenderScene()
{
    auto* app = NS::App::Application::Get();
    if (app == nullptr)
        return;

    NS::Scene::RenderContext ctx{};
    ctx.renderer = &app->Renderer();
    ctx.alpha = NS::Core::FrameTimer::Alpha();

    if (m_brain == nullptr || m_mainCamera == nullptr)
        return;

    // 有効な vcam (edit=free-fly / play=follow) を選び、 alpha 補間で実カメラへ書く
    // follow は補間 target を追うのでここで alpha を渡す (旧 ApplyCameraTransform 相当の補間)
    m_brain->Evaluate(ctx.alpha);
    ctx.viewProjection = m_brain->ViewProjection();

    // 半透明 back-to-front ソート用に実カメラの world 座標を渡す (view 行列の逆変換の平行移動成分)
    ctx.cameraPosition = m_mainCamera->Camera().View().Invert().Translation();

    // 基底が BuildSceneOverride() を Resolve するので、 theme override が scene 解決値として ctx に載る
    // mesh 経路は ctx 経由で pull、 block 経路はこの解決値を FrameCB に詰めて同一値を流す
    ctx.resolvedSettings = ResolveSceneSettings(ctx.renderer->Settings());
    // editor の RenderSettings パネルが friend で読むため解決値を退避する
    m_lastResolvedSettings = ctx.resolvedSettings;

    // テーマ swap は同一 frame 内で skybox / block / lighting に同じ ThemeData を反映させる必要がある
    // 範囲外 themeId は ThemeRegistry::Get 側で Grass にフォールバックされる
    const ThemeData& theme = ThemeRegistry::Get(m_level.themeId);

    // Block 描画は InstanceBatcher bucket 経由に統一。 MeshRendererComponent が非アクティブなので旧 per-block
    // 経路は通らない
    if (m_instanceBatcher && m_instanceBatcher->IsValid())
    {
        // builtin cube と共有 block material は AssetManager 所有。 毎フレームここで 1 度だけ引く
        auto& assets = app->Assets();
        auto* cubeMesh = assets.Builtin("cube");
        auto* blockMat = assets.SharedMaterial("block");

        // scene 解決値を block 全体の FrameCB に流す。 baseColor は per-instance で個体色を別途乗算する
        NS::Scene::FrameCB blockCB{};
        blockCB.viewProj = ctx.viewProjection;
        blockCB.lightDir = ctx.resolvedSettings.lightDir;
        blockCB.lightDir.Normalize();
        blockCB.baseColor = NS::Math::Vector3{1.0f, 1.0f, 1.0f}; // per-instance baseColor と乗算するので 1 に固定
        blockCB.lightColor = ctx.resolvedSettings.lightColor;
        blockCB.ambientColor = ctx.resolvedSettings.ambientColor;
        if (blockMat)
            blockMat->SetParams(*ctx.renderer, blockCB);

        m_instanceBatcher->BeginFrame();
        for (std::size_t bi = 0; bi < m_blocks.size(); ++bi)
        {
            const auto& block = m_blocks[bi];
            if (!block)
                continue;
            // m_blocks は gridAligned solid のみ。 cell は world 座標を丸めて求め、 近傍マスクは solid 同士で取る
            const NS::Math::Vector3 wp = block->Root().Position();
            const std::int16_t x = static_cast<std::int16_t>(std::lround(wp.x));
            const std::int16_t y = static_cast<std::int16_t>(std::lround(wp.y));
            const std::int16_t z = static_cast<std::int16_t>(std::lround(wp.z));
            constexpr std::uint16_t blockId = NS::Game::Blocks::kBlockIdSolid;
            const std::uint8_t mask = NS::Game::Blocks::ComputeNeighborMask(m_level, x, y, z, blockId);
            const std::uint16_t slice =
                NS::Game::Blocks::LookupTextureSlice(static_cast<ThemeId>(m_level.themeId), mask, blockId);

            NS::Graphics::BlockInstance inst{};
            inst.worldMatrix = block->Root().InterpolatedWorldMatrix(ctx.alpha);
            // 個体色は GetBaseColor を流し込んでおく (theme tint は FrameCB の lightColor/ambientColor で行う)
            const auto color = NS::Game::Blocks::GetBaseColor(blockId);
            inst.baseColor = NS::Math::Vector3{color.R(), color.G(), color.B()};
            inst.textureSlice = static_cast<float>(slice);
            m_instanceBatcher->Submit(cubeMesh, blockMat, inst);
        }

        // TextureArray を t0 に bind してから FlushAll。 Material::Bind では slot 0 を触っていない
        // (SetTexture せず構築した) ため、 ここで bind した SRV が bucket 描画まで残る
        if (auto* blockTextures = assets.TextureArrayByName("block"))
            ctx.renderer->Commands().SetTextureArray(*blockTextures, 0u, NS::Graphics::ShaderType::Pixel);
        m_instanceBatcher->FlushAll(*ctx.renderer);
    }

    // 不透明 IRenderable。各 Draw が自分の Pipeline を set する (基底が bucket 分類して登録順に呼ぶ)
    DrawOpaque(ctx);

    // Skybox は不透明描画後・半透明前。 view の translation 行 (_41/_42/_43) を 0 化して camera 中心に固定する
    if (m_skybox && m_skybox->IsValid())
    {
        // 毎フレーム LoadCubemap すると I/O が常時走るため、 前回パスと差分があるときだけ再ロードする
        if (!theme.skyboxCubemapPath.empty() && theme.skyboxCubemapPath != m_loadedSkyboxPath)
        {
            const auto exeDir = NS::Core::FileSystem::ContentRoot();
            const auto absPath =
                theme.skyboxCubemapPath.is_absolute() ? theme.skyboxCubemapPath : exeDir / theme.skyboxCubemapPath;
            if (m_skybox->LoadCubemap(absPath))
            {
                m_loadedSkyboxPath = theme.skyboxCubemapPath;
            }
            else
            {
                NS_LOG_WARN(::NS::Core::LogCat::Game,
                            "LevelPlayScene: テーマ '{}' の cubemap 読込失敗 ({}), 既存を維持",
                            theme.displayName,
                            absPath.string());
                // 失敗時は m_loadedSkyboxPath は更新しないので次フレームで再試行可能
            }
        }

        const auto& cam = m_mainCamera->Camera();
        NS::Math::Matrix viewNoTranslate = cam.View();
        viewNoTranslate._41 = 0.0f;
        viewNoTranslate._42 = 0.0f;
        viewNoTranslate._43 = 0.0f;
        const NS::Math::Matrix viewProjNoTranslate = viewNoTranslate * cam.Projection();
        m_skybox->Render(*ctx.renderer, viewProjNoTranslate);
    }

    // 半透明 IRenderable は不透明 + skybox の後。カメラから遠い順に各 Draw が alpha/additive Pipeline を set する
    DrawTransparent(ctx);
}

void LevelPlayScene::OnShutdown()
{
    if (m_cameraHost)
        m_cameraHost->OnEndPlay();
    for (auto& area : m_areaCameras)
    {
        if (area.host)
            area.host->OnEndPlay();
    }
    if (m_cameraRig)
        m_cameraRig->OnEndPlay();
    for (auto it = m_blocks.rbegin(); it != m_blocks.rend(); ++it)
        (*it)->OnEndPlay();
    for (auto it = m_slopes.rbegin(); it != m_slopes.rend(); ++it)
        (*it)->OnEndPlay();
    for (auto it = m_poles.rbegin(); it != m_poles.rend(); ++it)
        (*it)->OnEndPlay();
    for (auto it = m_hazards.rbegin(); it != m_hazards.rend(); ++it)
        (*it)->OnEndPlay();
    for (auto it = m_waters.rbegin(); it != m_waters.rend(); ++it)
        (*it)->OnEndPlay();
    for (auto it = m_decorations.rbegin(); it != m_decorations.rend(); ++it)
        (*it)->OnEndPlay();
    for (auto it = m_freeObjects.rbegin(); it != m_freeObjects.rend(); ++it)
        (*it)->OnEndPlay();
    if (m_animatedModel)
        m_animatedModel->OnEndPlay();
    if (m_player)
        m_player->OnEndPlay();

    // Brain は vcam を非所有参照するので、 rig / area camera より先に host を畳んで dangling を避ける
    m_cameraHost.reset();
    m_mainCamera = nullptr;
    m_brain = nullptr;
    m_areaCameras.clear();
    m_cameraRig.reset();
    m_animatedModel.reset();
    m_player.reset();
    m_blocks.clear();
    m_slopes.clear();
    m_poles.clear();
    m_hazards.clear();
    m_waters.clear();
    m_decorations.clear();
    m_freeObjects.clear();
    m_freeSourceIndices.clear();
    m_blockSourceIndices.clear();

    // Skybox / InstanceBatcher は Renderer の DeviceContext を ComPtr で握るため、 Renderer (Application) より
    // 先に破棄する。 builtin / leaf / 共有 material / block TextureArray / skinned model は AssetManager が Clear
    // で解放する
    m_instanceBatcher.reset();
    m_skybox.reset();
}

void LevelPlayScene::RebuildBlocksFromLevelData()
{
    for (auto it = m_blocks.rbegin(); it != m_blocks.rend(); ++it)
        (*it)->OnEndPlay();
    for (auto it = m_slopes.rbegin(); it != m_slopes.rend(); ++it)
        (*it)->OnEndPlay();
    for (auto it = m_poles.rbegin(); it != m_poles.rend(); ++it)
        (*it)->OnEndPlay();
    for (auto it = m_hazards.rbegin(); it != m_hazards.rend(); ++it)
        (*it)->OnEndPlay();
    for (auto it = m_waters.rbegin(); it != m_waters.rend(); ++it)
        (*it)->OnEndPlay();
    for (auto it = m_decorations.rbegin(); it != m_decorations.rend(); ++it)
        (*it)->OnEndPlay();
    for (auto it = m_freeObjects.rbegin(); it != m_freeObjects.rend(); ++it)
        (*it)->OnEndPlay();
    m_blocks.clear();
    m_slopes.clear();
    m_poles.clear();
    m_hazards.clear();
    m_waters.clear();
    m_decorations.clear();
    m_freeObjects.clear();
    m_freeSourceIndices.clear();
    m_blockSourceIndices.clear();
    m_collisionWorld.clear();
    m_collisionTriangles.clear();
    m_collisionObbs.clear();
    m_polePtrs.clear();

    m_collisionWorld.reserve(m_level.objects.size());

    const auto exeDir = NS::Core::FileSystem::ContentRoot();
    auto* app = NS::App::Application::Get();

    // builtin mesh と共有 material は AssetManager 所有。 この再構築の間だけローカルに引く
    NS::Graphics::StaticMesh* cubeMesh = nullptr;
    NS::Graphics::StaticMesh* wedge45 = nullptr;
    NS::Graphics::StaticMesh* wedge30 = nullptr;
    NS::Graphics::StaticMesh* wedge22 = nullptr;
    NS::Graphics::StaticMesh* wedge15 = nullptr;
    NS::Graphics::StaticMesh* poleMesh = nullptr;
    NS::Graphics::Material* playerMat = nullptr;
    NS::Graphics::Material* blockMat = nullptr;
    NS::Graphics::Material* waterMat = nullptr;
    if (app != nullptr)
    {
        auto& assets = app->Assets();
        cubeMesh = assets.Builtin("cube");
        wedge45 = assets.Builtin("wedge45");
        wedge30 = assets.Builtin("wedge30");
        wedge22 = assets.Builtin("wedge22");
        wedge15 = assets.Builtin("wedge15");
        poleMesh = assets.Builtin("pole");
        playerMat = assets.SharedMaterial("player");
        blockMat = assets.SharedMaterial("block");
        waterMat = assets.SharedMaterial("water");
    }

    // ObjectInstance.materialIndex から runtime Material* を解決する。 無効なら既定の共有 player material
    const auto resolveMaterial = [&](const NS::Game::Level::ObjectInstance& object) -> NS::Graphics::Material* {
        if (app != nullptr && object.materialIndex >= 0 &&
            static_cast<std::size_t>(object.materialIndex) < m_level.materialPaths.size())
        {
            const auto loaded = app->Assets().LoadMaterial(exeDir / m_level.materialPaths[object.materialIndex]);
            if (loaded.material != nullptr)
                return loaded.material;
        }
        return playerMat;
    };

    for (std::size_t objectIndex = 0; objectIndex < m_level.objects.size(); ++objectIndex)
    {
        const NS::Game::Level::ObjectInstance& entry = m_level.objects[objectIndex];
        const bool gridAligned = (entry.flags & NS::Game::Level::kObjectFlagGridAligned) != 0;

        // ObjectInstance の transform をそのまま載せる。 grid はセルスナップ済の値、 自由配置物はギズモ編集値
        const auto placeFromEntry = [&](NS::Scene::GameObject& obj) {
            obj.Root().SetPosition(NS::Math::Vector3{entry.positionX, entry.positionY, entry.positionZ});
            obj.Root().SetRotation(
                NS::Math::Quaternion{entry.rotationX, entry.rotationY, entry.rotationZ, entry.rotationW});
            obj.Root().SetScale(NS::Math::Vector3{entry.scaleX, entry.scaleY, entry.scaleZ});
        };
        const auto color = NS::Game::Blocks::GetBaseColor(entry.kind);
        const NS::Math::Vector3 baseColor{color.R(), color.G(), color.B()};

        // 非 gridAligned (自由配置物) は個別描画の Block として扱う。 材質は materialIndex から解決する
        // (v1 で昇格できるのは solid のみなので kind は問わず cube で表現する)
        // collider は Transform と独立した保存値から組む (world では Root の scale が別途乗る)
        if (!gridAligned)
        {
            const NS::Math::Vector3 colliderHalfExtents{
                entry.colliderHalfExtentsX, entry.colliderHalfExtentsY, entry.colliderHalfExtentsZ};
            auto cube = std::make_unique<Block>(cubeMesh, resolveMaterial(entry), colliderHalfExtents);
            cube->AttachScene(this);
            placeFromEntry(*cube);
            cube->MeshComp().SetBaseColor(baseColor);
            // 当たり箱の親 local オフセット / 回転を保存値から復元する (世界では owner の scale / 回転が更に乗る)
            cube->Collider().SetCenterOffset(
                NS::Math::Vector3{entry.colliderOffsetX, entry.colliderOffsetY, entry.colliderOffsetZ});
            cube->Collider().SetLocalRotation(NS::Math::Quaternion{
                entry.colliderRotationX, entry.colliderRotationY, entry.colliderRotationZ, entry.colliderRotationW});
            cube->OnStart();
            // 自由配置物は回転 / scale を潰さない OBB チャネルへ載せる (grid solid は AABB のまま)
            m_collisionObbs.push_back(cube->Collider().WorldOBB());
            m_freeSourceIndices.push_back(objectIndex);
            m_freeObjects.push_back(std::move(cube));
            continue;
        }

        if (entry.kind == NS::Game::Blocks::kBlockIdSolid)
        {
            auto block = std::make_unique<Block>(cubeMesh, blockMat, kCellHalfExtents);
            block->AttachScene(this);
            placeFromEntry(*block);
            block->MeshComp().SetBaseColor(baseColor);
            block->OnStart();
            // OnStart で RegisterRenderable 済のため SetActive(false) で個別 Draw 経路を無効化し InstanceBatcher
            // に委ねる
            block->MeshComp().SetActive(false);

            m_collisionWorld.push_back(block->Collider().WorldAABB());
            m_blockSourceIndices.push_back(objectIndex);
            m_blocks.push_back(std::move(block));
            continue;
        }

        if (NS::Game::Blocks::IsSlopeBlock(entry.kind))
        {
            const float angle = NS::Game::Blocks::GetSlopeAngleDegrees(entry.kind);
            NS::Graphics::StaticMesh* wedge = nullptr;
            if (entry.kind == NS::Game::Blocks::kBlockIdSlope45)
                wedge = wedge45;
            else if (entry.kind == NS::Game::Blocks::kBlockIdSlope30)
                wedge = wedge30;
            else if (entry.kind == NS::Game::Blocks::kBlockIdSlope22)
                wedge = wedge22;
            else if (entry.kind == NS::Game::Blocks::kBlockIdSlope15)
                wedge = wedge15;

            auto slope = std::make_unique<SlopeBlock>(wedge, blockMat, angle, kCellHalfExtents);
            slope->AttachScene(this);
            placeFromEntry(*slope);

            slope->MeshComp().SetBaseColor(baseColor);
            slope->OnStart();

            const auto tris = slope->Collider().WorldTriangles();
            for (const auto& tri : tris)
                m_collisionTriangles.push_back(tri);
            m_slopes.push_back(std::move(slope));
            continue;
        }

        if (NS::Game::Blocks::IsPoleBlock(entry.kind))
        {
            constexpr float kPoleRadius = 0.15f;
            constexpr float kPoleHeight = 1.0f;
            auto pole = std::make_unique<PoleBlock>(poleMesh, blockMat, kPoleRadius, kPoleHeight);
            pole->AttachScene(this);
            placeFromEntry(*pole);

            pole->MeshComp().SetBaseColor(baseColor);
            pole->OnStart();

            m_polePtrs.push_back(&pole->Pole());
            m_poles.push_back(std::move(pole));
            continue;
        }

        if (NS::Game::Blocks::IsHazardBlock(entry.kind))
        {
            auto hazard = std::make_unique<HazardBlock>(cubeMesh, blockMat, kCellHalfExtents);
            hazard->AttachScene(this);
            placeFromEntry(*hazard);

            hazard->MeshComp().SetBaseColor(baseColor);
            hazard->OnStart();

            // 衝突は通常 Block と同じく AABB として登録。 hazard 固有のダメージ trigger は
            // OnUpdate 内で player.position vs AABB を per-frame check する経路を取る
            m_collisionWorld.push_back(hazard->Collider().WorldAABB());
            m_hazards.push_back(std::move(hazard));
            continue;
        }

        if (NS::Game::Blocks::IsWaterBlock(entry.kind))
        {
            auto water = std::make_unique<WaterBlock>(cubeMesh, waterMat);
            water->AttachScene(this);
            placeFromEntry(*water);

            water->MeshComp().SetBaseColor(baseColor);
            water->OnStart();

            // collider なしで m_collisionWorld にも m_collisionTriangles にも入れない (装飾と同じ理由)
            m_waters.push_back(std::move(water));
            continue;
        }

        if (NS::Game::Blocks::IsDecorationBlock(entry.kind))
        {
            auto deco = std::make_unique<DecorationBlock>(cubeMesh, blockMat);
            deco->AttachScene(this);
            placeFromEntry(*deco);

            deco->MeshComp().SetBaseColor(baseColor);
            deco->OnStart();

            m_decorations.push_back(std::move(deco));
            continue;
        }
    }

    // 生成直後は previous PRS が原点/単位回転のため Snapshot で current に揃える
    // 欠かすと InterpolatedWorldMatrix(alpha) が原点→配置先を補間し編集のたびに全ブロックが振れる
    for (auto& block : m_blocks)
        block->Root().Snapshot();
    for (auto& slope : m_slopes)
        slope->Root().Snapshot();
    for (auto& pole : m_poles)
        pole->Root().Snapshot();
    for (auto& hazard : m_hazards)
        hazard->Root().Snapshot();
    for (auto& water : m_waters)
        water->Root().Snapshot();
    for (auto& deco : m_decorations)
        deco->Root().Snapshot();
    for (auto& obj : m_freeObjects)
        obj->Root().Snapshot();

    if (m_player)
    {
        m_player->Movement().SetCollisionWorld(m_collisionWorld);
        m_player->Movement().SetCollisionTriangles(m_collisionTriangles);
        m_player->Movement().SetCollisionObbs(m_collisionObbs);

        // 接地シャドウは grid + 自由物の内包 AABB を下方向 ray で拾う。 blob なので OBB 精度は要らない
        std::vector<NS::Math::AABB> shadowReceivers(m_collisionWorld.begin(), m_collisionWorld.end());
        for (auto& freeCube : m_freeObjects)
            if (freeCube)
                shadowReceivers.push_back(freeCube->Collider().WorldAABB());
        m_player->Shadow().SetCollisionWorld(shadowReceivers);

        m_player->Movement().SetClimbables(std::span<NS::Scene::PoleComponent* const>{m_polePtrs});
    }
}

void LevelPlayScene::RebuildAreaCamerasFromLevelData()
{
    if (m_brain == nullptr)
        return;

    // 旧 area camera を Brain から外してから破棄する (Brain の非所有参照を dangling させない)
    for (auto& area : m_areaCameras)
    {
        if (area.cam)
            m_brain->RemoveVirtualCamera(area.cam);
    }
    m_areaCameras.clear();

    m_areaCameras.reserve(m_level.cameraVolumes.size());
    for (const auto& volume : m_level.cameraVolumes)
    {
        AreaCamera area{};
        area.host = std::make_unique<NS::Scene::GameObject>();
        area.cam = area.host->AddComponent<NS::Scene::PlacedVirtualCamera>();
        area.cam->SetView({volume.cameraPositionX, volume.cameraPositionY, volume.cameraPositionZ},
                          {volume.lookTargetX, volume.lookTargetY, volume.lookTargetZ});
        area.cam->SetTrigger({volume.triggerCenterX, volume.triggerCenterY, volume.triggerCenterZ},
                             {volume.triggerExtentX, volume.triggerExtentY, volume.triggerExtentZ});
        area.cam->SetLookAtPlayer(volume.lookAtPlayer != 0);
        area.cam->SetVcamPriority(volume.priority);
        area.cam->SetActive(false); // エリア外。 play 中の進入判定で vcam が自分を active 化する
        area.host->AttachScene(this);
        area.host->OnStart();
        m_brain->AddVirtualCamera(area.cam);
        m_areaCameras.push_back(std::move(area));
    }
}

void LevelPlayScene::UpdateAnimatedModel()
{
    if (!m_animatedModel)
        return;

    m_animatedModel->HandleDebugInput();
    m_animatedModel->Root().Snapshot();
    m_animatedModel->OnUpdate();
}
