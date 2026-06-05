#include "Game/LevelEditorScene.h"

#include "Game/Block.h"
#include "Game/Blocks/DecorationBlock.h"
#include "Game/Blocks/HazardBlock.h"
#include "Game/Blocks/PoleBlock.h"
#include "Game/Blocks/SlopeBlock.h"
#include "Game/Blocks/WaterBlock.h"
#include "Game/Player.h"

#include "Framework/Scene/GameObject.h"
#include "Framework/Scene/HazardComponent.h"
#include "Framework/Scene/PoleComponent.h"

#include "Framework/App/Application.h"
#include "Framework/Core/Clock.h"
#include "Framework/Core/Filesystem.h"
#include "Framework/Core/LogCategories.h"
#include "Framework/Core/Logger.h"
#include "Framework/Graphics/GltfLoader.h"
#include "Framework/Graphics/InstanceBatcher.h"
#include "Framework/Graphics/Material.h"
#include "Framework/Graphics/MeshPrimitives.h"
#include "Framework/Graphics/Renderer.h"
#include "Framework/Graphics/Shader.h"
#include "Framework/Graphics/SkeletalMesh.h"
#include "Framework/Graphics/Skybox.h"
#include "Framework/Graphics/StaticMesh.h"
#include "Framework/Graphics/Texture.h"
#include "Framework/Graphics/TextureArray.h"
#include "Framework/Physics/Capsule.h"
#include "Framework/Platform/Gamepad.h"
#include "Framework/Platform/Input.h"
#include "Framework/Platform/Keyboard.h"
#include "Framework/Platform/Window.h"
#include "Framework/Scene/IRenderable.h"
#include "Framework/Scene/MeshRendererComponent.h"
#include "Framework/Scene/RenderContext.h"
#include "Framework/Scene/SkeletalAnimationComponent.h"
#include "Framework/Scene/Transform.h"
#include "Framework/UI/ImGuiContext.h"
#include "Game/Editor/AutoTile.h"
#include "Game/Editor/BlockRegistry.h"
#include "Game/Theme/ThemeRegistry.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace
{
    constexpr NS::Math::Vector3 kPlayerColor{0.85f, 0.20f, 0.20f};
    constexpr NS::Math::Vector3 kCellHalfExtents{0.5f, 0.5f, 0.5f};

    // 仮 skinned キャラの接地点と目標身長。 bind 境界から一様スケールを自動算出するので
    // 別キャラ (Mixamo 等) に差し替えてもモデル単位に依らず接地して概ね同じ高さに収まる
    constexpr NS::Math::Vector3 kAnimModelFootAnchor{2.5f, 0.0f, 0.0f};
    constexpr float kAnimModelTargetHeight = 1.8f;

    /// 編集体験の起点となる最小床。 LevelData に block 1 個 + spawn を仕込んでおく
    void SeedInitialLevel(NS::Game::Level::LevelData& level)
    {
        level.blocks.clear();
        level.blocks.push_back({0, 0, 0, NS::Game::Editor::kBlockIdSolid, 0, 0});
        level.spawnX = 0;
        level.spawnY = 1;
        level.spawnZ = 0;
    }
} // namespace

LevelEditorScene::LevelEditorScene() = default;

LevelEditorScene::~LevelEditorScene() = default;

void LevelEditorScene::OnStart()
{
    auto* app = NS::App::Application::Get();
    if (app == nullptr)
    {
        NS_LOG_ERROR(::NS::Core::LogCat::Game, "LevelEditorScene::OnStart: Application::Get()==null");
        return;
    }

    auto& renderer = app->Renderer();
    const auto exeDir = NS::Core::FileSystem::GetExeDirectory();

    auto cubeGeom = NS::Graphics::MakeCube({0.5f, 0.5f, 0.5f});
    NS::Graphics::MeshDesc meshDesc{};
    meshDesc.vertices = cubeGeom.vertices.data();
    meshDesc.vertexCount = cubeGeom.vertices.size();
    meshDesc.indices = cubeGeom.indices.data();
    meshDesc.indexCount = cubeGeom.indices.size();
    m_cubeMesh = std::make_unique<NS::Graphics::StaticMesh>(renderer, meshDesc);

    // 4 種 wedge mesh を 1 度だけ生成して scene 寿命のあいだ共有する
    auto buildWedge = [&renderer](float angleDeg) {
        auto geom = NS::Graphics::MakeWedge(angleDeg, {0.5f, 0.5f, 0.5f});
        NS::Graphics::MeshDesc md{};
        md.vertices = geom.vertices.data();
        md.vertexCount = geom.vertices.size();
        md.indices = geom.indices.data();
        md.indexCount = geom.indices.size();
        return std::make_unique<NS::Graphics::StaticMesh>(renderer, md);
    };
    m_wedgeMesh45 = buildWedge(45.0f);
    m_wedgeMesh30 = buildWedge(30.0f);
    m_wedgeMesh22 = buildWedge(22.5f);
    m_wedgeMesh15 = buildWedge(15.0f);

    {
        auto poleGeom = NS::Graphics::MakeCylinder(0.15f, 1.0f, 12);
        NS::Graphics::MeshDesc poleDesc{};
        poleDesc.vertices = poleGeom.vertices.data();
        poleDesc.vertexCount = poleGeom.vertices.size();
        poleDesc.indices = poleGeom.indices.data();
        poleDesc.indexCount = poleGeom.indices.size();
        m_poleMesh = std::make_unique<NS::Graphics::StaticMesh>(renderer, poleDesc);
    }

    NS::Graphics::TextureDesc texDesc{};
    texDesc.path = exeDir / "Assets" / "Textures" / "cube_test.png";
    texDesc.generateMipmaps = true;
    texDesc.sRGB = false;
    m_texture = std::make_unique<NS::Graphics::Texture>(renderer, texDesc);
    if (m_texture->IsUsingFallback())
        NS_LOG_WARN(::NS::Core::LogCat::Game, "LevelEditorScene: cube_test.png 読込失敗、magenta fallback で続行");

    // Player は単一 Texture2D 流派 (player.ps.hlsl) を維持
    NS::Graphics::ShaderDesc playerShaderDesc{};
    playerShaderDesc.vertexShaderPath = exeDir / "Shaders" / "standard.vs.hlsl";
    playerShaderDesc.pixelShaderPath = exeDir / "Shaders" / "player.ps.hlsl";
    playerShaderDesc.vertexEntryPoint = "VSMain";
    playerShaderDesc.pixelEntryPoint = "PSMain";
    playerShaderDesc.inputLayout = NS::Graphics::StaticMesh::StandardInputLayout();
    m_playerShader = std::make_unique<NS::Graphics::Shader>(renderer, playerShaderDesc);
    if (m_playerShader->IsUsingFallback())
        NS_LOG_WARN(::NS::Core::LogCat::Game,
                    "LevelEditorScene: player 用 HLSL 読込/コンパイル失敗、 magenta fallback で続行");

    // Block 側は instanced.vs.hlsl + standard.ps.hlsl (Texture2DArray) ペアを InstanceBatcher が
    // 内部で組む。 ここで作る m_blockShader は ConstantBuffer の搬入経路として使うだけで、
    // 実際の VS/PS は FlushAll 内で上書きされる
    NS::Graphics::ShaderDesc blockShaderDesc{};
    blockShaderDesc.vertexShaderPath = exeDir / "Shaders" / "standard.vs.hlsl";
    blockShaderDesc.pixelShaderPath = exeDir / "Shaders" / "player.ps.hlsl";
    blockShaderDesc.vertexEntryPoint = "VSMain";
    blockShaderDesc.pixelEntryPoint = "PSMain";
    blockShaderDesc.inputLayout = NS::Graphics::StaticMesh::StandardInputLayout();
    m_blockShader = std::make_unique<NS::Graphics::Shader>(renderer, blockShaderDesc);

    NS::Graphics::MaterialDesc matDesc{};
    matDesc.shader = m_playerShader.get();
    matDesc.constantBufferSize = sizeof(NS::Scene::FrameCB);
    matDesc.cbSlot = 0;
    matDesc.cbStages = NS::Graphics::ShaderStage::Vertex | NS::Graphics::ShaderStage::Pixel;
    m_playerMaterial = std::make_unique<NS::Graphics::Material>(renderer, matDesc);
    m_playerMaterial->SetTexture(0, m_texture.get());

    NS::Graphics::MaterialDesc blockMatDesc = matDesc;
    blockMatDesc.shader = m_blockShader.get();
    m_blockMaterial = std::make_unique<NS::Graphics::Material>(renderer, blockMatDesc);
    // block の slot 0 は外側で TextureArray を bind するので Material 側には SetTexture しない
    // SetTexture すると Material::Bind が slot 0 を上書きしてしまい、 InstanceBatcher 側で
    // ぶら下げた TextureArray SRV が消える

    // 全 theme 用 block texture を 1 つの Texture2DArray に集約する。 現状はアセット未取得なので
    // cube_test.png を代替で 40 slice ぶん詰める (5 theme x 8 variant の枠だけ確保しておく流派)
    // Kenney prototype texture が揃ったら slicePaths をテーマ別に差し替える
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
        m_blockTextures = std::make_unique<NS::Graphics::TextureArray>(renderer, taDesc);
        if (m_blockTextures->IsUsingFallback())
            NS_LOG_WARN(::NS::Core::LogCat::Game,
                        "LevelEditorScene: block 用 TextureArray の slice 読込で失敗あり、 magenta fallback で続行");
    }

    m_instanceBatcher = std::make_unique<NS::Graphics::InstanceBatcher>(renderer);
    if (!m_instanceBatcher->IsValid())
        NS_LOG_WARN(::NS::Core::LogCat::Game,
                    "LevelEditorScene: InstanceBatcher 構築失敗、 block 描画はスキップされる");

    // placeholder skybox。 kurt 6-face PNG をロードし、 取得できなければ
    // 1x1 マゼンタ cubemap fallback で続行する (描画は OnRender 末尾)
    m_skybox = std::make_unique<NS::Graphics::Skybox>(renderer);
    if (m_skybox->IsValid())
    {
        const auto kurtDir = exeDir / "Assets" / "Skybox" / "kurt";
        if (!m_skybox->LoadCubemap(kurtDir))
            NS_LOG_WARN(::NS::Core::LogCat::Game,
                        "LevelEditorScene: kurt cubemap 読込失敗、 magenta fallback で続行: {}",
                        kurtDir.string());
    }
    else
    {
        NS_LOG_ERROR(::NS::Core::LogCat::Game, "LevelEditorScene: Skybox 構築失敗 (Device 不在?)");
    }

    m_player = std::make_unique<Player>(m_cubeMesh.get(), m_playerMaterial.get(), &app->Input());
    m_player->AttachScene(this);
    m_player->Root().SetPosition({0.0f, 1.0f, -4.0f});
    // cube mesh の半サイズは 0.5 だが capsule collider は radius=0.4 / halfHeight=0.5
    // (= AABB 半サイズ 0.4, 0.9, 0.4)。両者が一致するよう scale で mesh を縮める
    m_player->Root().SetScale({0.8f, 1.8f, 0.8f});
    m_player->MeshComp().SetBaseColor(kPlayerColor);
    // Play 中の入力は PlayerInputComponent が担う。 ImGui のテキスト入力中に WASD を取り合わないよう注入
    m_player->InputComp().SetImGui(app->ImGui());

    SeedInitialLevel(m_level);
    RebuildBlocksFromLevelData();

    m_cameraRig = std::make_unique<CameraRig>(&app->Input(), &m_player->Root(), &m_player->Movement());
    m_cameraRig->AttachScene(this);

    auto& camera = m_cameraRig->Camera();
    camera.SetAspectRatioFromRenderer(renderer);
    camera.SetNearPlane(0.1f);
    camera.SetFarPlane(100.0f);
    camera.SetFovY(m_cameraRig->Follow().FovY());
    camera.SetUp({0.0f, 1.0f, 0.0f});

    m_player->OnStart();
    m_cameraRig->OnStart();

    // 編集モード専用の free-fly カメラを Player / follow camera と並列で立ち上げる
    // MB64 の freecam に相当 (mouse + gamepad で Orbit / Pan / Zoom)
    m_editorCameraRig = std::make_unique<EditorCameraRig>();
    m_editorCameraRig->AttachScene(this);
    m_editorCameraRig->EditorCam().SetInput(&app->Input());
    m_editorCameraRig->EditorCam().SetImGui(app->ImGui());

    auto& editorCam = m_editorCameraRig->Camera();
    editorCam.SetAspectRatioFromRenderer(renderer);
    editorCam.SetNearPlane(0.1f);
    editorCam.SetFarPlane(200.0f);
    editorCam.SetFovY(NS::Math::ToRadians(NS::Math::Degrees{60.0f}));
    editorCam.SetUp({0.0f, 1.0f, 0.0f});

    // 初期視点は spawn 位置を中心に少し引いた位置から見下ろす
    const NS::Math::Vector3 spawnPos{
        static_cast<float>(m_level.spawnX), static_cast<float>(m_level.spawnY), static_cast<float>(m_level.spawnZ)};
    m_editorCameraRig->EditorCam().SetCenter(spawnPos);
    // 起動直後は spawn block を真ん中近めに見せる距離。 1m cube が画面の十数 % を占める
    m_editorCameraRig->EditorCam().SetDistance(5.0f);

    m_editorCameraRig->OnStart();

    // EditorMode に依存先を注入する。 mode toggle が入るまでは常時 active
    m_editor.SetLevel(&m_level);
    m_editor.SetInput(&app->Input());
    m_editor.SetImGui(app->ImGui());
    m_editor.SetCameraComponent(&m_editorCameraRig->Camera());
    m_editor.SetEditorCamera(&m_editorCameraRig->EditorCam());
    m_editor.SetActive(true);
    // OnStart で手動 rebuild 済なので、 初回 OnUpdate の二重 rebuild を抑制
    m_editor.ClearLevelDirty();

    // 編集モード起動: Player / follow camera を frozen / invisible に
    // Play モード遷移時に SetActive(true) で再活性化する設計
    m_player->MeshComp().SetActive(false);
    m_player->Movement().SetActive(false);
    m_player->InputComp().SetActive(false);
    m_cameraRig->Camera().SetActive(false);
    m_cameraRig->Follow().SetActive(false);

    // 仮 skinned キャラを編集・プレイ両モードで常時表示し、 アニメ再生を画面で確認できるようにする
    // アセットが無ければ skip して通常進行。 後で同じパスに別キャラ (glTF) を置けば差し替わる
    {
        const auto modelPath = exeDir / "Assets" / "Models" / "CesiumMan.glb";
        auto skinned = NS::Graphics::LoadGltfSkinnedMesh(modelPath.string());
        if (skinned.IsValid())
        {
            NS::Graphics::SkinnedMeshDesc smd{};
            smd.vertices = skinned.vertices.data();
            smd.vertexCount = skinned.vertices.size();
            smd.indices = skinned.indices.data();
            smd.indexCount = skinned.indices.size();
            smd.boneCount = skinned.skeleton.BoneCount();
            m_skinnedMesh = std::make_unique<NS::Graphics::SkeletalMesh>(renderer, smd);

            NS::Graphics::ShaderDesc skinnedShaderDesc{};
            skinnedShaderDesc.vertexShaderPath = exeDir / "Shaders" / "skinned.vs.hlsl";
            skinnedShaderDesc.pixelShaderPath = exeDir / "Shaders" / "player.ps.hlsl";
            skinnedShaderDesc.vertexEntryPoint = "VSMain";
            skinnedShaderDesc.pixelEntryPoint = "PSMain";
            skinnedShaderDesc.inputLayout = NS::Graphics::SkeletalMesh::SkinnedInputLayout();
            m_skinnedShader = std::make_unique<NS::Graphics::Shader>(renderer, skinnedShaderDesc);
            if (m_skinnedShader->IsUsingFallback())
                NS_LOG_WARN(::NS::Core::LogCat::Game,
                            "LevelEditorScene: skinned 用 HLSL 読込/コンパイル失敗、 magenta fallback で続行");

            NS::Graphics::MaterialDesc skinnedMatDesc{};
            skinnedMatDesc.shader = m_skinnedShader.get();
            skinnedMatDesc.constantBufferSize = sizeof(NS::Scene::FrameCB);
            skinnedMatDesc.cbSlot = 0;
            skinnedMatDesc.cbStages = NS::Graphics::ShaderStage::Vertex | NS::Graphics::ShaderStage::Pixel;
            m_skinnedMaterial = std::make_unique<NS::Graphics::Material>(renderer, skinnedMatDesc);
            // 専用テクスチャは未取得なので block と同じ placeholder を貼る (変形が見えれば目的は足りる)
            m_skinnedMaterial->SetTexture(0, m_texture.get());

            // bind ポーズ頂点の境界から目標身長に合わせた一様スケールを出し、 足元を接地点へ寄せる
            NS::Math::Vector3 boundsMin = skinned.vertices.front().position;
            NS::Math::Vector3 boundsMax = boundsMin;
            for (const NS::Graphics::SkinnedVertex& v : skinned.vertices)
            {
                boundsMin = NS::Math::Vector3::Min(boundsMin, v.position);
                boundsMax = NS::Math::Vector3::Max(boundsMax, v.position);
            }
            const float modelHeight = std::max(boundsMax.y - boundsMin.y, 1e-3f);
            const float fitScale = kAnimModelTargetHeight / modelHeight;
            const NS::Math::Vector3 fitPosition{kAnimModelFootAnchor.x - (boundsMin.x + boundsMax.x) * 0.5f * fitScale,
                                                kAnimModelFootAnchor.y - boundsMin.y * fitScale,
                                                kAnimModelFootAnchor.z - (boundsMin.z + boundsMax.z) * 0.5f * fitScale};

            const std::size_t boneCount = skinned.skeleton.BoneCount();
            m_animatedModel = std::make_unique<NS::Scene::GameObject>();
            m_animatedModel->AttachScene(this);
            m_animatedModel->Root().SetPosition(fitPosition);
            m_animatedModel->Root().SetScale({fitScale, fitScale, fitScale});
            m_animMesh = m_animatedModel->AddComponent<NS::Scene::MeshRendererComponent>(m_skinnedMesh.get(),
                                                                                         m_skinnedMaterial.get());
            m_animPlayer = m_animatedModel->AddComponent<NS::Scene::SkeletalAnimationComponent>(
                m_skinnedMesh.get(), std::move(skinned.skeleton), std::move(skinned.animations));
            m_animPlayer->SetSpeed(m_animSpeed);
            m_animatedModel->OnStart();
            m_animatedModel->Root().Snapshot();

            NS_LOG_INFO(::NS::Core::LogCat::Game,
                        "LevelEditorScene: skinned 仮モデル読込 ({} bones, {} clips)",
                        boneCount,
                        m_animPlayer->ClipCount());
        }
        else
        {
            NS_LOG_WARN(::NS::Core::LogCat::Game,
                        "LevelEditorScene: skinned 仮モデル {} 無し/読込失敗、 アニメ表示は skip",
                        modelPath.string());
        }
    }
}

void LevelEditorScene::OnUpdate()
{
    auto* app = NS::App::Application::Get();
    if (app == nullptr)
        return;

    if (app->Input().Keyboard().IsPressed(NS::Platform::Key::Escape))
    {
        NS::App::Application::Quit();
        return;
    }

    const bool editActive = (m_mode == Mode::Edit);

    // Application が Renderer::Resize を排他で握っているため、 Camera の aspect ratio は
    // Renderer の現在 Size から毎フレーム pull する (callback 上書きで競合させない)
    if (editActive)
    {
        if (m_editorCameraRig)
            m_editorCameraRig->Camera().SetAspectRatioFromRenderer(app->Renderer());
    }
    else if (m_cameraRig)
    {
        m_cameraRig->Camera().SetAspectRatioFromRenderer(app->Renderer());
    }

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

    if (editActive)
    {
        // free-fly camera を駆動
        if (m_editorCameraRig)
        {
            m_editorCameraRig->Root().Snapshot();
            m_editorCameraRig->OnUpdate();
        }

        m_editor.Tick();
        if (m_editor.IsLevelDirty())
        {
            RebuildBlocksFromLevelData();
            m_editor.ClearLevelDirty();
        }
    }
    else
    {
        const float dt = NS::Core::FrameTimer::FixedDelta();

        // 入力と物理は Player の Component が担う。 camera 水平 forward を入力 Component に渡してから
        // Player を tick すると、 PlayerInput → CharacterMovement の順 (priority) で desired move /
        // 掴まり入力 / jump が反映され、 結果が Player.Root (Transform) に直接書かれる
        // SSOT は Transform。 ThirdPersonFollow が Player.Root を target にしているため camera も追従する
        if (m_player)
        {
            NS::Math::Vector3 camForward{0.0f, 0.0f, 1.0f};
            if (m_cameraRig)
                camForward = m_cameraRig->Camera().ForwardHorizontal();
            m_player->InputComp().SetCameraForward(camForward);
            m_player->OnUpdate();

            // 落下死 / coin / star / hazard 判定が読む PlayState.playerPosition に Transform をミラーする
            m_play.playerPosition = m_player->Root().Position();
        }

        // Play のゲームルール (落下死 / coin / star)。 物理は持たず player 位置を読むだけ
        m_playMode.Tick(m_level, m_play, dt);

        // ハザード AABB と player capsule の overlap 判定。 接触していれば HazardComponent に
        // 通知して playerHealth を 1 減算する (per-fixed-step accumulating)。 hazard は solid 衝突世界にも
        // 入っており capsule 中心は表面から radius ぶん外に留まるため、 capsule 芯線分から AABB の最近距離で判定する
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
        if (m_play.clearTriggered)
        {
            EnterEdit();
        }

        if (m_player)
            m_player->Root().Snapshot();
        if (m_cameraRig)
        {
            m_cameraRig->Root().Snapshot();
            m_cameraRig->OnUpdate();
        }
    }

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

    UpdateAnimatedModel();
}

void LevelEditorScene::EnterPlay() noexcept
{
    if (m_mode == Mode::Play)
        return;
    m_mode = Mode::Play;
    m_playMode.Enter(m_level, m_play);
    m_playMode.SetActive(true);
    m_editor.SetActive(false);

    if (m_editorCameraRig)
        m_editorCameraRig->EditorCam().SetActive(false);

    if (m_player)
    {
        m_player->MeshComp().SetActive(true);
        // 物理と入力は Player の Component (CharacterMovement / PlayerInput) が担う
        m_player->Movement().SetActive(true);
        m_player->InputComp().SetActive(true);
        // PlayMode.Enter が計算した spawn 位置へ置いてから movement 状態をリセットする
        m_player->Root().SetPosition(m_play.playerPosition);
        m_player->Movement().ResetState();
    }
    if (m_cameraRig)
    {
        m_cameraRig->Camera().SetActive(true);
        m_cameraRig->Follow().SetActive(true);
    }
}

void LevelEditorScene::EnterEdit() noexcept
{
    if (m_mode == Mode::Edit)
        return;
    m_mode = Mode::Edit;
    m_playMode.Exit(m_play);
    m_playMode.SetActive(false);
    m_editor.SetActive(true);

    if (m_editorCameraRig)
        m_editorCameraRig->EditorCam().SetActive(true);

    if (m_player)
    {
        m_player->MeshComp().SetActive(false);
        m_player->Movement().SetActive(false);
        m_player->InputComp().SetActive(false);
    }
    if (m_cameraRig)
    {
        m_cameraRig->Camera().SetActive(false);
        m_cameraRig->Follow().SetActive(false);
    }
}

void LevelEditorScene::OnRender()
{
    auto* app = NS::App::Application::Get();
    if (app == nullptr)
        return;

    NS::Scene::RenderContext ctx{};
    ctx.renderer = &app->Renderer();
    ctx.alpha = NS::Core::FrameTimer::Alpha();

    const bool editActive = (m_mode == Mode::Edit);
    if (editActive)
    {
        if (m_editorCameraRig == nullptr)
            return;
        ctx.viewProjection = m_editorCameraRig->Camera().ViewProjection();
    }
    else
    {
        if (m_cameraRig == nullptr)
            return;
        // Player Mesh の補間と camera を同位相にする。 OnUpdate (fixed step) で
        // SetPosition すると相対位置が discrete に動いて jitter として見える
        m_cameraRig->Follow().ApplyCameraTransform(ctx.alpha);
        ctx.viewProjection = m_cameraRig->Camera().ViewProjection();
    }

    // テーマ swap は同一 frame 内で skybox / block / lighting に同じ ThemeData を反映させる必要がある
    // 範囲外 themeId は ThemeRegistry::Get 側で Grass にフォールバックされる
    const ThemeData& theme = ThemeRegistry::Get(m_level.themeId);

    // Player の赤系 baseColor 等の個体色は MeshRendererComponent::SetBaseColor で別途設定済なので触らない
    if (m_player)
    {
        auto& mesh = m_player->MeshComp();
        mesh.SetLightDirection(theme.lightDirection);
        mesh.SetLightColor(theme.lightColor);
        mesh.SetAmbientColor(theme.ambientColor);
    }

    if (m_animMesh)
    {
        m_animMesh->SetLightDirection(theme.lightDirection);
        m_animMesh->SetLightColor(theme.lightColor);
        m_animMesh->SetAmbientColor(theme.ambientColor);
    }

    // Block 描画は InstanceBatcher の (mesh, material) bucket 経由に統一する
    // block の MeshRendererComponent::IsActive(false) で旧 per-block Draw 経路は短絡されるため、
    // 描画呼出は本フレームの instance VB 1 回 + bucket 数の DrawIndexedInstanced に集約される
    if (m_instanceBatcher && m_instanceBatcher->IsValid())
    {
        // theme tint を block 全体の FrameCB に流す。 baseColor は per-instance で個体色を別途乗算する
        NS::Scene::FrameCB blockCB{};
        blockCB.viewProj = ctx.viewProjection;
        blockCB.lightDir = theme.lightDirection;
        if (blockCB.lightDir.LengthSquared() <= 1e-6f)
            blockCB.lightDir = NS::Math::Vector3{-0.3f, -1.0f, -0.2f};
        blockCB.lightDir.Normalize();
        blockCB.baseColor = NS::Math::Vector3{1.0f, 1.0f, 1.0f}; // per-instance baseColor と乗算するので 1 に固定
        blockCB.lightColor = theme.lightColor;
        blockCB.ambientColor = theme.ambientColor;
        if (m_blockMaterial)
            m_blockMaterial->SetParams(blockCB);

        m_instanceBatcher->BeginFrame();
        for (auto& block : m_blocks)
        {
            if (!block)
                continue;
            const NS::Game::Level::BlockEntry* entry = nullptr;
            // Block は親無しなので local Position == world position。 grid cell に丸めて LevelData と照合する
            const NS::Math::Vector3 wp = block->Root().Position();
            const std::int16_t x = static_cast<std::int16_t>(std::lround(wp.x));
            const std::int16_t y = static_cast<std::int16_t>(std::lround(wp.y));
            const std::int16_t z = static_cast<std::int16_t>(std::lround(wp.z));
            // blockId は LevelData 側にしか無いので、 対応 entry を見つける。 SeedInitialLevel /
            // RebuildBlocksFromLevelData の関係から index 一致は保証されるが、 安全側で線形検索する
            for (const auto& e : m_level.blocks)
            {
                if (e.x == x && e.y == y && e.z == z)
                {
                    entry = &e;
                    break;
                }
            }
            const std::uint16_t blockId =
                entry ? entry->blockId : static_cast<std::uint16_t>(NS::Game::Editor::kBlockIdSolid);
            const std::uint8_t mask = NS::Game::Editor::ComputeNeighborMask(m_level, x, y, z, blockId);
            const std::uint16_t slice =
                NS::Game::Editor::LookupTextureSlice(static_cast<ThemeId>(m_level.themeId), mask, blockId);

            NS::Graphics::BlockInstance inst{};
            inst.worldMatrix = block->Root().InterpolatedWorldMatrix(ctx.alpha);
            // 個体色は GetBaseColor を流し込んでおく (theme tint は FrameCB の lightColor/ambientColor で行う)
            const auto color = NS::Game::Editor::GetBaseColor(blockId);
            inst.baseColor = NS::Math::Vector3{color.R(), color.G(), color.B()};
            inst.textureSlice = static_cast<float>(slice);
            m_instanceBatcher->Submit(m_cubeMesh.get(), m_blockMaterial.get(), inst);
        }

        // TextureArray を t0 に bind してから FlushAll。 Material::Bind では slot 0 を触っていない
        // (SetTexture せず構築した) ため、 ここで bind した SRV が bucket 描画まで残る
        if (m_blockTextures)
            m_blockTextures->Bind(0u, NS::Graphics::ShaderStage::Pixel);
        m_instanceBatcher->FlushAll();
    }

    for (NS::Scene::IRenderable* r : m_renderList)
    {
        if (r != nullptr)
            r->Draw(ctx);
    }

    // Skybox は不透明描画後 + 編集オーバーレイ前に挟む (depth=1 同士の
    // LESS_EQUAL 比較を成立させるため depth buffer 上の遠景 pixel が確定した直後)
    // viewProj から camera 位置を抜くために行列の translation 行 (_41/_42/_43) を 0 化する
    // これで skybox は常に camera 中心に追従し、 player が前進しても同じ星空を見続ける
    if (m_skybox && m_skybox->IsValid())
    {
        // テーマが切替わった (or 起動直後) frame だけ cubemap を再ロードする
        // 毎フレーム LoadCubemap すると DDS / 6-face PNG の I/O が常時走るので、
        // 最後にロードしたパスを記憶して差分が出た時だけ呼ぶ
        if (!theme.skyboxCubemapPath.empty() && theme.skyboxCubemapPath != m_loadedSkyboxPath)
        {
            const auto exeDir = NS::Core::FileSystem::GetExeDirectory();
            const auto absPath =
                theme.skyboxCubemapPath.is_absolute() ? theme.skyboxCubemapPath : exeDir / theme.skyboxCubemapPath;
            if (m_skybox->LoadCubemap(absPath))
            {
                m_loadedSkyboxPath = theme.skyboxCubemapPath;
            }
            else
            {
                NS_LOG_WARN(::NS::Core::LogCat::Game,
                            "LevelEditorScene: テーマ '{}' の cubemap 読込失敗 ({}), 既存を維持",
                            theme.displayName,
                            absPath.string());
                // 失敗時は m_loadedSkyboxPath は更新しないので次フレームで再試行可能
            }
        }

        const auto& cam = editActive ? m_editorCameraRig->Camera().Camera() : m_cameraRig->Camera().Camera();
        NS::Math::Matrix viewNoTranslate = cam.View();
        viewNoTranslate._41 = 0.0f;
        viewNoTranslate._42 = 0.0f;
        viewNoTranslate._43 = 0.0f;
        const NS::Math::Matrix viewProjNoTranslate = viewNoTranslate * cam.Projection();
        m_skybox->Render(viewProjNoTranslate);
    }

    if (editActive)
    {
        m_editor.RenderSpawnMarker();
        m_editor.RenderCursorPreview();
        // Toolbar UI を ImGui 経由で描画 (Debug / Development build のみ実機能)
        m_editor.Palette().Render();
    }
}

void LevelEditorScene::OnShutdown()
{
    if (m_editorCameraRig)
        m_editorCameraRig->OnEndPlay();
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
    if (m_animatedModel)
        m_animatedModel->OnEndPlay();
    if (m_player)
        m_player->OnEndPlay();

    m_renderList.clear();

    if (auto* app = NS::App::Application::Get())
        app->Window().SetResizeCallback({});

    m_editorCameraRig.reset();
    m_cameraRig.reset();
    m_animatedModel.reset();
    m_animMesh = nullptr;
    m_animPlayer = nullptr;
    m_player.reset();
    m_blocks.clear();
    m_slopes.clear();
    m_poles.clear();
    m_hazards.clear();
    m_waters.clear();
    m_decorations.clear();

    // Skybox / InstanceBatcher / TextureArray は Renderer の DeviceContext を ComPtr で握っているため、
    // Renderer (Application) より先に破棄する必要がある。 m_cubeMesh と同階層で reset
    m_instanceBatcher.reset();
    m_skybox.reset();
    m_skinnedMaterial.reset();
    m_skinnedShader.reset();
    m_playerMaterial.reset();
    m_blockMaterial.reset();
    m_blockShader.reset();
    m_playerShader.reset();
    m_blockTextures.reset();
    m_texture.reset();
    m_wedgeMesh45.reset();
    m_wedgeMesh30.reset();
    m_wedgeMesh22.reset();
    m_wedgeMesh15.reset();
    m_poleMesh.reset();
    m_skinnedMesh.reset();
    m_cubeMesh.reset();
}

void LevelEditorScene::RegisterRenderable(NS::Scene::IRenderable* renderable)
{
    if (renderable == nullptr)
        return;
    // 二重登録を防ぐ。Component 側で OnStart が誤って 2 回呼ばれても二重描画にならない
    if (std::find(m_renderList.begin(), m_renderList.end(), renderable) != m_renderList.end())
        return;
    m_renderList.push_back(renderable);
}

void LevelEditorScene::UnregisterRenderable(NS::Scene::IRenderable* renderable)
{
    if (renderable == nullptr)
        return;
    // erase-remove で全要素を消し、不変式 (一意性) と防御的削除を両立する
    m_renderList.erase(std::remove(m_renderList.begin(), m_renderList.end(), renderable), m_renderList.end());
}

void LevelEditorScene::RebuildBlocksFromLevelData()
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
    m_blocks.clear();
    m_slopes.clear();
    m_poles.clear();
    m_hazards.clear();
    m_waters.clear();
    m_decorations.clear();
    m_collisionWorld.clear();
    m_collisionTriangles.clear();
    m_polePtrs.clear();

    m_blocks.reserve(m_level.blocks.size());
    m_collisionWorld.reserve(m_level.blocks.size());

    for (const auto& entry : m_level.blocks)
    {
        const NS::Math::Vector3 cellCenter{
            static_cast<float>(entry.x), static_cast<float>(entry.y), static_cast<float>(entry.z)};

        // 全ブロック共通の配置。 entry.rotation (0-255) を Y 軸 yaw として transform に載せる
        // slope は SlopeColliderComponent が Owner world matrix を掛けるので collider も自動で回る
        const auto placeInCell = [&](NS::Scene::GameObject& obj) {
            obj.Root().SetPosition(cellCenter);
            const float yaw = NS::Game::Editor::BlockRotationToYaw(entry.rotation);
            obj.Root().SetRotation(NS::Math::Quaternion::CreateFromYawPitchRoll(yaw, 0.0f, 0.0f));
        };

        if (entry.blockId == NS::Game::Editor::kBlockIdSolid)
        {
            auto block = std::make_unique<Block>(m_cubeMesh.get(), m_blockMaterial.get(), kCellHalfExtents);
            block->AttachScene(this);
            placeInCell(*block);
            block->Root().SetScale({kCellHalfExtents.x * 2.0f, kCellHalfExtents.y * 2.0f, kCellHalfExtents.z * 2.0f});

            const auto color = NS::Game::Editor::GetBaseColor(entry.blockId);
            block->MeshComp().SetBaseColor(NS::Math::Vector3{color.R(), color.G(), color.B()});
            block->OnStart();
            // block の IRenderable 経路は休止させ、 描画は InstanceBatcher の bucket 集約に任せる
            // OnStart 内で MeshRendererComponent が RegisterRenderable しているため、 ここで SetActive(false) すると
            // Draw(context) が no-op になり 1 block = 1 draw call の旧経路が完全に消える
            block->MeshComp().SetActive(false);

            m_collisionWorld.push_back(block->Collider().WorldAABB());
            m_blocks.push_back(std::move(block));
            continue;
        }

        if (NS::Game::Editor::IsSlopeBlock(entry.blockId))
        {
            const float angle = NS::Game::Editor::GetSlopeAngleDegrees(entry.blockId);
            NS::Graphics::StaticMesh* wedge = nullptr;
            if (entry.blockId == NS::Game::Editor::kBlockIdSlope45)
                wedge = m_wedgeMesh45.get();
            else if (entry.blockId == NS::Game::Editor::kBlockIdSlope30)
                wedge = m_wedgeMesh30.get();
            else if (entry.blockId == NS::Game::Editor::kBlockIdSlope22)
                wedge = m_wedgeMesh22.get();
            else if (entry.blockId == NS::Game::Editor::kBlockIdSlope15)
                wedge = m_wedgeMesh15.get();

            auto slope = std::make_unique<SlopeBlock>(wedge, m_blockMaterial.get(), angle, kCellHalfExtents);
            slope->AttachScene(this);
            placeInCell(*slope);

            const auto color = NS::Game::Editor::GetBaseColor(entry.blockId);
            slope->MeshComp().SetBaseColor(NS::Math::Vector3{color.R(), color.G(), color.B()});
            slope->OnStart();

            const auto tris = slope->Collider().WorldTriangles();
            for (const auto& tri : tris)
                m_collisionTriangles.push_back(tri);
            m_slopes.push_back(std::move(slope));
            continue;
        }

        if (NS::Game::Editor::IsPoleBlock(entry.blockId))
        {
            constexpr float kPoleRadius = 0.15f;
            constexpr float kPoleHeight = 1.0f;
            auto pole = std::make_unique<PoleBlock>(m_poleMesh.get(), m_blockMaterial.get(), kPoleRadius, kPoleHeight);
            pole->AttachScene(this);
            placeInCell(*pole);

            const auto color = NS::Game::Editor::GetBaseColor(entry.blockId);
            pole->MeshComp().SetBaseColor(NS::Math::Vector3{color.R(), color.G(), color.B()});
            pole->OnStart();

            m_polePtrs.push_back(&pole->Pole());
            m_poles.push_back(std::move(pole));
            continue;
        }

        if (NS::Game::Editor::IsHazardBlock(entry.blockId))
        {
            auto hazard = std::make_unique<HazardBlock>(m_cubeMesh.get(), m_blockMaterial.get(), kCellHalfExtents);
            hazard->AttachScene(this);
            placeInCell(*hazard);
            hazard->Root().SetScale({kCellHalfExtents.x * 2.0f, kCellHalfExtents.y * 2.0f, kCellHalfExtents.z * 2.0f});

            const auto color = NS::Game::Editor::GetBaseColor(entry.blockId);
            hazard->MeshComp().SetBaseColor(NS::Math::Vector3{color.R(), color.G(), color.B()});
            hazard->OnStart();

            // 衝突は通常 Block と同じく AABB として登録。 hazard 固有のダメージ trigger は
            // OnUpdate 内で player.position vs AABB を per-frame check する経路を取る
            m_collisionWorld.push_back(hazard->Collider().WorldAABB());
            m_hazards.push_back(std::move(hazard));
            continue;
        }

        if (NS::Game::Editor::IsWaterBlock(entry.blockId))
        {
            auto water = std::make_unique<WaterBlock>(m_cubeMesh.get(), m_blockMaterial.get());
            water->AttachScene(this);
            placeInCell(*water);
            water->Root().SetScale({kCellHalfExtents.x * 2.0f, kCellHalfExtents.y * 2.0f, kCellHalfExtents.z * 2.0f});

            const auto color = NS::Game::Editor::GetBaseColor(entry.blockId);
            water->MeshComp().SetBaseColor(NS::Math::Vector3{color.R(), color.G(), color.B()});
            water->OnStart();

            // collider なしで m_collisionWorld にも m_collisionTriangles にも入れない (装飾と同じ理由)
            m_waters.push_back(std::move(water));
            continue;
        }

        if (NS::Game::Editor::IsDecorationBlock(entry.blockId))
        {
            auto deco = std::make_unique<DecorationBlock>(m_cubeMesh.get(), m_blockMaterial.get());
            deco->AttachScene(this);
            placeInCell(*deco);
            deco->Root().SetScale({kCellHalfExtents.x * 2.0f, kCellHalfExtents.y * 2.0f, kCellHalfExtents.z * 2.0f});

            const auto color = NS::Game::Editor::GetBaseColor(entry.blockId);
            deco->MeshComp().SetBaseColor(NS::Math::Vector3{color.R(), color.G(), color.B()});
            deco->OnStart();

            m_decorations.push_back(std::move(deco));
            continue;
        }
    }

    // 生成直後は previous PRS が default(原点/単位回転)のため、ここで Snapshot して previous==current に揃える
    // これを欠かすと描画の InterpolatedWorldMatrix(alpha) が原点から配置先へ補間し、編集のたびに全ブロックが一瞬振れる
    // 毎フレームの Snapshot ループは Tick/Rebuild より前に走るので、この step では再構築分を拾えない
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

    if (m_player)
    {
        m_player->Movement().SetCollisionWorld(m_collisionWorld);
        m_player->Movement().SetCollisionTriangles(m_collisionTriangles);
        m_player->Movement().SetClimbables(std::span<NS::Scene::PoleComponent* const>{m_polePtrs});
    }
}

void LevelEditorScene::UpdateAnimatedModel()
{
    if (!m_animatedModel)
        return;

    auto* app = NS::App::Application::Get();
    if (app != nullptr && m_animPlayer != nullptr)
    {
        bool wantKb = false;
        if (auto* imgui = app->ImGui())
            wantKb = imgui->WantCaptureKeyboard();

        if (!wantKb)
        {
            auto& kb = app->Input().Keyboard();
            if (kb.IsPressed(NS::Platform::Key::F1))
            {
                if (m_animPlayer->IsPlaying())
                    m_animPlayer->Pause();
                else
                    m_animPlayer->Play();
            }
            if (kb.IsPressed(NS::Platform::Key::F2) && m_animPlayer->ClipCount() > 0)
            {
                const std::size_t next = (m_animPlayer->CurrentClip() + 1) % m_animPlayer->ClipCount();
                m_animPlayer->SelectClip(next);
                m_animPlayer->Play();
            }
            if (kb.IsPressed(NS::Platform::Key::F3))
            {
                m_animSpeed = std::max(0.0f, m_animSpeed - 0.25f);
                m_animPlayer->SetSpeed(m_animSpeed);
            }
            if (kb.IsPressed(NS::Platform::Key::F4))
            {
                m_animSpeed += 0.25f;
                m_animPlayer->SetSpeed(m_animSpeed);
            }
        }
    }

    m_animatedModel->Root().Snapshot();
    m_animatedModel->OnUpdate();
}
