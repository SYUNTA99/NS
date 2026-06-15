#include "Game/LevelEditorScene.h"

#include "Game/Block.h"
#include "Game/Blocks/DecorationBlock.h"
#include "Game/Blocks/HazardBlock.h"
#include "Game/Blocks/PoleBlock.h"
#include "Game/Blocks/SlopeBlock.h"
#include "Game/Blocks/WaterBlock.h"
#include "Game/Player.h"

#include "Framework/Scene/Components/HazardComponent.h"
#include "Framework/Scene/Components/PoleComponent.h"
#include "Framework/Scene/GameObject.h"

#include "Framework/App/Application.h"
#include "Framework/Core/Clock.h"
#include "Framework/Core/Filesystem.h"
#include "Framework/Core/LogCategories.h"
#include "Framework/Core/Logger.h"
#include "Framework/Graphics/CommandList.h"
#include "Framework/Graphics/GltfLoader.h"
#include "Framework/Graphics/InstanceBatcher.h"
#include "Framework/Graphics/Material.h"
#include "Framework/Graphics/MeshPrimitives.h"
#include "Framework/Graphics/Renderer.h"
#include "Framework/Graphics/Retarget.h"
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
#include "Framework/Scene/Components/MeshRendererComponent.h"
#include "Framework/Scene/Components/SkeletalAnimationComponent.h"
#include "Framework/Scene/IRenderable.h"
#include "Framework/Scene/RenderContext.h"
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

    // bind 境界から一様スケールを自動算出するためモデル単位に依らず接地・同身長に収まる
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
    m_cubeMesh = NS::Graphics::StaticMesh::Create(meshDesc);

    // 4 種 wedge mesh を 1 度だけ生成して scene 寿命のあいだ共有する
    auto buildWedge = [&renderer](float angleDeg) {
        auto geom = NS::Graphics::MakeWedge(angleDeg, {0.5f, 0.5f, 0.5f});
        NS::Graphics::MeshDesc md{};
        md.vertices = geom.vertices.data();
        md.vertexCount = geom.vertices.size();
        md.indices = geom.indices.data();
        md.indexCount = geom.indices.size();
        return NS::Graphics::StaticMesh::Create(md);
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
        m_poleMesh = NS::Graphics::StaticMesh::Create(poleDesc);
    }

    NS::Graphics::TextureDesc texDesc{};
    texDesc.path = exeDir / "Assets" / "Textures" / "cube_test.png";
    texDesc.generateMipmaps = true;
    texDesc.sRGB = false;
    m_texture = NS::Graphics::Texture::Create(texDesc);
    if (m_texture->IsUsingFallback())
        NS_LOG_WARN(::NS::Core::LogCat::Game, "LevelEditorScene: cube_test.png 読込失敗、magenta fallback で続行");

    // Player は単一 Texture2D 流派 (player.ps.hlsl)。 standard.vs は block と、 player.ps は block / skinned と共有する
    m_standardVS = NS::Graphics::Shader::Create(exeDir / "Shaders" / "standard.vs.hlsl");
    m_playerPS = NS::Graphics::Shader::Create(exeDir / "Shaders" / "player.ps.hlsl");
    if (m_standardVS->IsUsingFallback() || m_playerPS->IsUsingFallback())
        NS_LOG_WARN(::NS::Core::LogCat::Game,
                    "LevelEditorScene: player 用 HLSL 読込/コンパイル失敗、 magenta fallback で続行");

    NS::Graphics::MaterialDesc matDesc{};
    matDesc.vertexShader = m_standardVS.get();
    matDesc.pixelShader = m_playerPS.get();
    matDesc.constantBufferSize = sizeof(NS::Scene::FrameCB);
    matDesc.cbSlot = 0;
    m_playerMaterial = NS::Graphics::Material::Create(matDesc);
    m_playerMaterial->SetTexture(0, m_texture.get());

    // Block は player と同じ VS/PS を共有。 実際の VS/PS/Texture は InstanceBatcher が FlushAll で
    // 上書きするので、 ここの Material は ConstantBuffer 搬入路として使うだけ
    NS::Graphics::MaterialDesc blockMatDesc = matDesc;
    m_blockMaterial = NS::Graphics::Material::Create(blockMatDesc);
    // slot 0 は外側で TextureArray を bind するため SetTexture 禁止 — 呼ぶと Material::Bind が SRV を上書きする

    // 水は個別描画 (WaterBlock + MeshRenderer)。alpha<1 を出す water.ps + Alpha ブレンドの専用 Material にする
    m_waterPS = NS::Graphics::Shader::Create(exeDir / "Shaders" / "water.ps.hlsl");
    if (m_waterPS->IsUsingFallback())
        NS_LOG_WARN(::NS::Core::LogCat::Game,
                    "LevelEditorScene: water.ps 読込/コンパイル失敗、 magenta fallback で続行");
    NS::Graphics::MaterialDesc waterMatDesc = matDesc;
    waterMatDesc.pixelShader = m_waterPS.get();
    waterMatDesc.blend = NS::Graphics::BlendMode::Alpha;
    m_waterMaterial = NS::Graphics::Material::Create(waterMatDesc);
    m_waterMaterial->SetTexture(0, m_texture.get());

    // 接地シャドウ: 共有 quad mesh + shadow.ps + Alpha Material (テクスチャ不要、PS が放射状アルファを生成)
    m_shadowPS = NS::Graphics::Shader::Create(exeDir / "Shaders" / "shadow.ps.hlsl");
    if (m_shadowPS->IsUsingFallback())
        NS_LOG_WARN(::NS::Core::LogCat::Game,
                    "LevelEditorScene: shadow.ps 読込/コンパイル失敗、 magenta fallback で続行");
    {
        const auto planeGeom = NS::Graphics::MakePlane({0.5f, 0.5f});
        NS::Graphics::MeshDesc planeDesc{};
        planeDesc.vertices = planeGeom.vertices.data();
        planeDesc.vertexCount = planeGeom.vertices.size();
        planeDesc.indices = planeGeom.indices.data();
        planeDesc.indexCount = planeGeom.indices.size();
        m_shadowMesh = NS::Graphics::StaticMesh::Create(planeDesc);
    }
    NS::Graphics::MaterialDesc shadowMatDesc = matDesc;
    shadowMatDesc.pixelShader = m_shadowPS.get();
    shadowMatDesc.blend = NS::Graphics::BlendMode::Alpha;
    m_shadowMaterial = NS::Graphics::Material::Create(shadowMatDesc);

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
        m_blockTextures = NS::Graphics::TextureArray::Create(taDesc);
        if (m_blockTextures->IsUsingFallback())
            NS_LOG_WARN(::NS::Core::LogCat::Game,
                        "LevelEditorScene: block 用 TextureArray の slice 読込で失敗あり、 magenta fallback で続行");
    }

    m_instanceBatcher = NS::Graphics::InstanceBatcher::Create();
    if (!m_instanceBatcher->IsValid())
        NS_LOG_WARN(::NS::Core::LogCat::Game,
                    "LevelEditorScene: InstanceBatcher 構築失敗、 block 描画はスキップされる");

    // placeholder skybox。 kurt 6-face PNG をロードし、 取得できなければ
    // 1x1 マゼンタ cubemap fallback で続行する (描画は OnRenderScene 末尾)
    m_skybox = NS::Graphics::Skybox::Create();
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
    m_player->Shadow().SetResources(m_shadowMesh.get(), m_shadowMaterial.get());

    SeedInitialLevel(m_level);
    RebuildBlocksFromLevelData();

    m_cameraRig = std::make_unique<CameraRig>(&app->Input(), &m_player->Root(), &m_player->Movement());
    m_cameraRig->AttachScene(this);

    auto& camera = m_cameraRig->Camera();
    camera.SetAspectRatioFromRenderer(renderer);
    camera.SetNearPlane(0.1f);
    camera.SetFarPlane(100.0f);
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

    // Object ツールモードでギズモ変形を試せる自由 Transform オブジェクトをテスト用に数個置く
    // grid ブロックと違い LevelData に属さず、 texture 付き m_playerMaterial で個別 (DrawOpaque) 描画する
    {
        constexpr NS::Math::Vector3 kFreeHalfExtents{0.5f, 0.5f, 0.5f};
        const NS::Math::Vector3 seedPositions[3] = {
            {2.0f, 2.0f, 0.0f},
            {-2.0f, 2.0f, 1.0f},
            {0.0f, 3.0f, -2.0f},
        };
        for (const auto& pos : seedPositions)
        {
            auto cube = std::make_unique<Block>(m_cubeMesh.get(), m_playerMaterial.get(), kFreeHalfExtents);
            cube->AttachScene(this);
            cube->Root().SetPosition(pos);
            // grid ブロックと一目で区別できるよう橙色に着色する (テスト seed の目印)
            cube->MeshComp().SetBaseColor(NS::Math::Vector3{1.0f, 0.5f, 0.1f});
            cube->OnStart();
            m_freeObjectPtrs.push_back(cube.get());
            m_freeHalfExtents.push_back(kFreeHalfExtents);
            m_freeObjects.push_back(std::move(cube));
        }
    }

    // .mat から読んだ別々のマテリアルを並べて見た目の差を実証する (lit テクスチャ / flat unlit / 手続き grid)
    m_materialLibrary = std::make_unique<NS::Scene::MaterialLibrary>(exeDir);
    {
        constexpr NS::Math::Vector3 kFreeHalfExtents{0.5f, 0.5f, 0.5f};
        const struct
        {
            const char* file;
            NS::Math::Vector3 position;
        } demoMaterials[] = {
            {"stone.mat", {-2.0f, 2.0f, 3.0f}},
            {"flat.mat", {0.0f, 2.0f, 3.0f}},
            {"grid.mat", {2.0f, 2.0f, 3.0f}},
        };
        for (const auto& demo : demoMaterials)
        {
            const auto loaded = m_materialLibrary->Load(exeDir / "Assets" / "Materials" / demo.file);
            if (loaded.material == nullptr)
            {
                NS_LOG_WARN(::NS::Core::LogCat::Game, "LevelEditorScene: {} 読込失敗、 実証キューブは skip", demo.file);
                continue;
            }
            auto cube = std::make_unique<Block>(m_cubeMesh.get(), loaded.material, kFreeHalfExtents);
            cube->AttachScene(this);
            cube->Root().SetPosition(demo.position);
            cube->MeshComp().SetBaseColor(loaded.baseColor);
            cube->OnStart();
            cube->Root().Snapshot();
            m_freeObjectPtrs.push_back(cube.get());
            m_freeHalfExtents.push_back(kFreeHalfExtents);
            m_freeObjects.push_back(std::move(cube));
        }
    }

    // ギズモに依存先を注入する。 選択候補は自由オブジェクト + grid solid ブロックを連結して渡す
    m_gizmo.SetInput(&app->Input());
    m_gizmo.SetImGui(app->ImGui());
    RefreshGizmoSelectables();

    // 仮 skinned キャラを編集・プレイ両モードで常時表示し、 アニメ再生を画面で確認できるようにする
    // アセットが無ければ skip して通常進行。 後で同じパスに別キャラ (glTF) を置けば差し替わる
    {
        // Xbot は人型 profile 骨名と一致するため優先。 CesiumMan は骨名が違うので外部アニメ流用不可
        std::filesystem::path modelPath = exeDir / "Assets" / "Models" / "CesiumMan.glb";
        for (const char* name : {"Xbot.glb", "CesiumMan.glb"})
        {
            const auto candidate = exeDir / "Assets" / "Models" / name;
            if (NS::Core::FileSystem::Exists(candidate))
            {
                modelPath = candidate;
                break;
            }
        }
        auto skinned = NS::Graphics::LoadGltfSkinnedMesh(modelPath.string());
        if (skinned.IsValid())
        {
            NS::Graphics::SkinnedMeshDesc smd{};
            smd.vertices = skinned.vertices.data();
            smd.vertexCount = skinned.vertices.size();
            smd.indices = skinned.indices.data();
            smd.indexCount = skinned.indices.size();
            smd.boneCount = skinned.skeleton.BoneCount();
            m_skinnedMesh = NS::Graphics::SkeletalMesh::Create(smd);

            m_skinnedVS = NS::Graphics::Shader::Create(exeDir / "Shaders" / "skinned.vs.hlsl");
            if (m_skinnedVS->IsUsingFallback())
                NS_LOG_WARN(::NS::Core::LogCat::Game,
                            "LevelEditorScene: skinned 用 HLSL 読込/コンパイル失敗、 magenta fallback で続行");

            NS::Graphics::MaterialDesc skinnedMatDesc{};
            skinnedMatDesc.vertexShader = m_skinnedVS.get();
            skinnedMatDesc.pixelShader = m_playerPS.get();
            skinnedMatDesc.constantBufferSize = sizeof(NS::Scene::FrameCB);
            skinnedMatDesc.cbSlot = 0;
            m_skinnedMaterial = NS::Graphics::Material::Create(skinnedMatDesc);
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

            // モデル同梱クリップに Assets/Models/Anims/ の追加アニメ glTF を合体する
            // Mixamo 等の別ファイルを後から足せる (同一リグは骨名一致で再 index される)
            std::vector<NS::Graphics::AnimationClip> clips = std::move(skinned.animations);
            const auto animDir = exeDir / "Assets" / "Models" / "Anims";
            if (NS::Core::FileSystem::Exists(animDir))
            {
                for (const auto& animPath : NS::Core::FileSystem::ListFiles(animDir))
                {
                    const auto ext = animPath.extension();
                    if (ext != ".glb" && ext != ".gltf")
                        continue;
                    auto extra = NS::Graphics::LoadAnimationsForSkeleton(animPath.string(), skinned.skeleton);
                    for (NS::Graphics::AnimationClip& clip : extra)
                        clips.push_back(std::move(clip));
                }
            }

            // 別キャラのファイルからアニメだけ借りる (人型なら別リグでもリターゲットして合体)
            const auto borrowPath = exeDir / "Assets" / "Models" / "Soldier.glb";
            if (NS::Core::FileSystem::Exists(borrowPath) && borrowPath != modelPath)
            {
                auto borrowed = NS::Graphics::LoadAnimationsForSkeleton(borrowPath.string(), skinned.skeleton);
                for (NS::Graphics::AnimationClip& clip : borrowed)
                    clips.push_back(std::move(clip));
            }

            m_animatedModel = std::make_unique<NS::Scene::GameObject>();
            m_animatedModel->AttachScene(this);
            m_animatedModel->Root().SetPosition(fitPosition);
            m_animatedModel->Root().SetScale({fitScale, fitScale, fitScale});
            m_animMesh = m_animatedModel->AddComponent<NS::Scene::MeshRendererComponent>(m_skinnedMesh.get(),
                                                                                         m_skinnedMaterial.get());
            m_animPlayer = m_animatedModel->AddComponent<NS::Scene::SkeletalAnimationComponent>(
                m_skinnedMesh.get(), std::move(skinned.skeleton), std::move(clips));
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
        // Object モードで選択中なら、 Esc はまず選択解除に使い終了させない
        if (m_mode == Mode::Edit && m_editorToolMode == EditorToolMode::Object && m_gizmo.Selected() != nullptr)
        {
            m_gizmo.ClearSelection();
            return;
        }
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
    for (auto& obj : m_freeObjects)
        obj->Root().Snapshot();

    if (editActive)
    {
        // free-fly camera を駆動
        if (m_editorCameraRig)
        {
            m_editorCameraRig->Root().Snapshot();
            m_editorCameraRig->OnUpdate();
        }

        // 同時に 1 モードだけが LMB/R/Ctrl+Z を消費する。 Object 中は grid 入力を抑制しギズモへ回す
        // モード切替は EditorLayer の UI ボタン (SetObjectToolActive) から行う (Tab は Edit↔Play 専用)
        const bool objectMode = (m_editorToolMode == EditorToolMode::Object);
        m_gizmo.SetActive(objectMode);
        m_editor.SetInputSuppressed(objectMode);

        if (objectMode && m_editorCameraRig)
        {
            const auto vp = m_editorCameraRig->Camera().ViewProjection();
            const auto viewport = app->Window().Size();
            m_gizmo.Tick(vp, viewport);

            // 掴んだら自由化: 選択が grid solid ブロックなら自由 Transform オブジェクトへ昇格する
            if (NS::Scene::Transform* selected = m_gizmo.Selected())
            {
                for (std::size_t bi = 0; bi < m_blocks.size(); ++bi)
                {
                    if (m_blocks[bi] && &m_blocks[bi]->Root() == selected)
                    {
                        PromoteGridBlockToFree(bi);
                        break;
                    }
                }
            }
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

        // camera 水平 forward を先に渡してから tick。 priority 順 PlayerInput→CharacterMovement で入力→物理が確定し
        // Transform に書かれる
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

NS::Graphics::RenderSettingsOverride LevelEditorScene::BuildSceneOverride()
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
                        "LevelEditorScene: テーマの lightDirection が zero のため既定 lightDir で描画する");
            s_warnedZeroLightDir = true;
        }
    }
    over.lightColor = theme.lightColor;
    over.ambientColor = theme.ambientColor;
    return over;
}

void LevelEditorScene::OnRenderScene()
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
        // fixed step で SetPosition すると相対位置が discrete になり jitter するため補間を先に適用する
        m_cameraRig->Follow().ApplyCameraTransform(ctx.alpha);
        ctx.viewProjection = m_cameraRig->Camera().ViewProjection();
    }

    // 半透明 back-to-front ソート用に active camera の world 座標を渡す (view 行列の逆変換の平行移動成分)
    const NS::Math::Matrix camView =
        editActive ? m_editorCameraRig->Camera().Camera().View() : m_cameraRig->Camera().Camera().View();
    ctx.cameraPosition = camView.Invert().Translation();

    // 基底が BuildSceneOverride() を Resolve するので、 theme override が scene 解決値として ctx に載る
    // mesh 経路は ctx 経由で pull、 block 経路はこの解決値を FrameCB に詰めて同一値を流す
    ctx.resolvedSettings = ResolveSceneSettings(ctx.renderer->Settings());

    // Debug provenance パネルの入力を毎フレーム退避する。出所は has_value の突き合わせで逆算するので
    // Resolve のホットパスに追跡を入れず、 scene override と代表 object override をそのまま保持する
    m_debugSceneOverride = BuildSceneOverride();
    m_debugResolvedSettings = ctx.resolvedSettings;
    if (m_player)
        m_debugPlayerObjectOverride = m_player->MeshComp().RenderOverride();

    // テーマ swap は同一 frame 内で skybox / block / lighting に同じ ThemeData を反映させる必要がある
    // 範囲外 themeId は ThemeRegistry::Get 側で Grass にフォールバックされる
    const ThemeData& theme = ThemeRegistry::Get(m_level.themeId);

    // Block 描画は InstanceBatcher bucket 経由に統一。 MeshRendererComponent が非アクティブなので旧 per-block
    // 経路は通らない
    if (m_instanceBatcher && m_instanceBatcher->IsValid())
    {
        // scene 解決値を block 全体の FrameCB に流す。 baseColor は per-instance で個体色を別途乗算する
        NS::Scene::FrameCB blockCB{};
        blockCB.viewProj = ctx.viewProjection;
        blockCB.lightDir = ctx.resolvedSettings.lightDir;
        blockCB.lightDir.Normalize();
        blockCB.baseColor = NS::Math::Vector3{1.0f, 1.0f, 1.0f}; // per-instance baseColor と乗算するので 1 に固定
        blockCB.lightColor = ctx.resolvedSettings.lightColor;
        blockCB.ambientColor = ctx.resolvedSettings.ambientColor;
        if (m_blockMaterial)
            m_blockMaterial->SetParams(*ctx.renderer, blockCB);

        m_instanceBatcher->BeginFrame();
        for (std::size_t bi = 0; bi < m_blocks.size(); ++bi)
        {
            const auto& block = m_blocks[bi];
            if (!block)
                continue;
            const NS::Game::Level::BlockEntry* entry = nullptr;
            // Block は親無しなので local Position == world position。 grid cell に丸めて LevelData と照合する
            const NS::Math::Vector3 wp = block->Root().Position();
            const std::int16_t x = static_cast<std::int16_t>(std::lround(wp.x));
            const std::int16_t y = static_cast<std::int16_t>(std::lround(wp.y));
            const std::int16_t z = static_cast<std::int16_t>(std::lround(wp.z));
            // blockId は LevelData 側にしか無い。 SeedInitialLevel / RebuildBlocksFromLevelData が
            // index 一致を保証するため同 index を先に照合し、 不一致時のみ線形検索へフォールバック
            if (bi < m_level.blocks.size())
            {
                const auto& same = m_level.blocks[bi];
                if (same.x == x && same.y == y && same.z == z)
                    entry = &same;
            }
            if (entry == nullptr)
            {
                for (const auto& e : m_level.blocks)
                {
                    if (e.x == x && e.y == y && e.z == z)
                    {
                        entry = &e;
                        break;
                    }
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
            ctx.renderer->Commands().SetTextureArray(*m_blockTextures, 0u, NS::Graphics::ShaderType::Pixel);
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
        m_skybox->Render(*ctx.renderer, viewProjNoTranslate);
    }

    // 半透明 IRenderable は不透明 + skybox の後。カメラから遠い順に各 Draw が alpha/additive Pipeline を set する
    DrawTransparent(ctx);

    if (editActive)
    {
        m_editor.RenderSpawnMarker();
        m_editor.RenderCursorPreview();
        // Toolbar UI を ImGui 経由で描画 (Debug / Development build のみ実機能)
        m_editor.Palette().Render();
        // Object モードのギズモは最前面 (drawlist) に重ねる
        if (m_gizmo.IsActive())
            m_gizmo.Render(ctx.viewProjection, app->Window().Size());
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
    for (auto it = m_freeObjects.rbegin(); it != m_freeObjects.rend(); ++it)
        (*it)->OnEndPlay();
    if (m_animatedModel)
        m_animatedModel->OnEndPlay();
    if (m_player)
        m_player->OnEndPlay();

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
    m_gizmo.ClearSelection();
    m_freeObjects.clear();
    m_freeObjectPtrs.clear();
    m_freeHalfExtents.clear();
    m_selectablePtrs.clear();
    m_selectableHalfExtents.clear();

    // MaterialLibrary の Material / Shader / Texture も device リソースを握るため Renderer より先に破棄する
    // (参照する free オブジェクトは上で破棄済)
    m_materialLibrary.reset();

    // Skybox / InstanceBatcher / TextureArray は Renderer の DeviceContext を ComPtr で握っているため、
    // Renderer (Application) より先に破棄する必要がある。 m_cubeMesh と同階層で reset
    m_instanceBatcher.reset();
    m_skybox.reset();
    m_skinnedMaterial.reset();
    m_playerMaterial.reset();
    m_blockMaterial.reset();
    m_waterMaterial.reset();
    m_shadowMaterial.reset();
    m_skinnedVS.reset();
    m_playerPS.reset();
    m_waterPS.reset();
    m_shadowPS.reset();
    m_standardVS.reset();
    m_blockTextures.reset();
    m_texture.reset();
    m_wedgeMesh45.reset();
    m_wedgeMesh30.reset();
    m_wedgeMesh22.reset();
    m_wedgeMesh15.reset();
    m_poleMesh.reset();
    m_shadowMesh.reset();
    m_skinnedMesh.reset();
    m_cubeMesh.reset();
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
            // OnStart で RegisterRenderable 済のため SetActive(false) で旧 per-block Draw 経路を無効化し
            // InstanceBatcher に委ねる
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
            auto water = std::make_unique<WaterBlock>(m_cubeMesh.get(), m_waterMaterial.get());
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

    if (m_player)
    {
        m_player->Movement().SetCollisionWorld(m_collisionWorld);
        m_player->Movement().SetCollisionTriangles(m_collisionTriangles);
        m_player->Shadow().SetCollisionWorld(m_collisionWorld);
        m_player->Movement().SetClimbables(std::span<NS::Scene::PoleComponent* const>{m_polePtrs});
    }

    // m_blocks の pointer が作り直されたので、 ギズモの選択候補 span を必ず貼り直す (dangling 防止)
    RefreshGizmoSelectables();
}

void LevelEditorScene::RefreshGizmoSelectables()
{
    m_selectablePtrs.clear();
    m_selectableHalfExtents.clear();
    m_selectablePtrs.reserve(m_freeObjectPtrs.size() + m_blocks.size());
    m_selectableHalfExtents.reserve(m_freeHalfExtents.size() + m_blocks.size());

    for (std::size_t i = 0; i < m_freeObjectPtrs.size(); ++i)
    {
        m_selectablePtrs.push_back(m_freeObjectPtrs[i]);
        m_selectableHalfExtents.push_back(m_freeHalfExtents[i]);
    }

    // grid solid ブロックも掴める。 掴むと PromoteGridBlockToFree で自由オブジェクトに変わる
    for (const auto& block : m_blocks)
    {
        if (!block)
            continue;
        m_selectablePtrs.push_back(block.get());
        m_selectableHalfExtents.push_back(kCellHalfExtents);
    }

    m_gizmo.SetSelectableObjects(m_selectablePtrs, m_selectableHalfExtents);
}

void LevelEditorScene::PromoteGridBlockToFree(std::size_t blockIndex)
{
    if (blockIndex >= m_blocks.size() || !m_blocks[blockIndex])
        return;

    // Block は親無しなので local Position == world position。 grid cell に丸めて LevelData と照合する
    const NS::Math::Vector3 worldPos = m_blocks[blockIndex]->Root().Position();
    const std::int16_t cx = static_cast<std::int16_t>(std::lround(worldPos.x));
    const std::int16_t cy = static_cast<std::int16_t>(std::lround(worldPos.y));
    const std::int16_t cz = static_cast<std::int16_t>(std::lround(worldPos.z));

    // 対応する grid entry の blockId を控えて LevelData から外す。 これで rebuild 後に grid から消える
    std::uint16_t blockId = NS::Game::Editor::kBlockIdSolid;
    for (auto it = m_level.blocks.begin(); it != m_level.blocks.end(); ++it)
    {
        if (it->x == cx && it->y == cy && it->z == cz)
        {
            blockId = it->blockId;
            m_level.blocks.erase(it);
            break;
        }
    }

    // 同位置・同色の自由 Block を作る。 grid と違い texture 付き m_playerMaterial で個別 (DrawOpaque) 描画する
    const auto color = NS::Game::Editor::GetBaseColor(blockId);
    auto cube = std::make_unique<Block>(m_cubeMesh.get(), m_playerMaterial.get(), kCellHalfExtents);
    cube->AttachScene(this);
    cube->Root().SetPosition(worldPos);
    cube->MeshComp().SetBaseColor(NS::Math::Vector3{color.R(), color.G(), color.B()});
    cube->OnStart();
    // previous==current に揃えて昇格初フレームの補間飛びを防ぐ
    cube->Root().Snapshot();
    NS::Scene::Transform* newSelected = &cube->Root();
    m_freeObjectPtrs.push_back(cube.get());
    m_freeHalfExtents.push_back(kCellHalfExtents);
    m_freeObjects.push_back(std::move(cube));

    // grid を作り直して昇格 cell を消す (rebuild 末尾が選択候補 span を貼り直す)。 選択は新オブジェクトへ移す
    RebuildBlocksFromLevelData();
    m_gizmo.SetSelected(newSelected);
}

bool LevelEditorScene::ApplyMaterialToSelected(const std::filesystem::path& matPath)
{
    if (m_editorToolMode != EditorToolMode::Object || !m_materialLibrary)
        return false;

    NS::Scene::Transform* selected = m_gizmo.Selected();
    if (selected == nullptr)
        return false;

    // 選択中の Transform を持つ自由オブジェクトを探す (ギズモ選択は常に free オブジェクトを指す)
    Block* target = nullptr;
    for (auto& obj : m_freeObjects)
    {
        if (obj && &obj->Root() == selected)
        {
            target = obj.get();
            break;
        }
    }
    if (target == nullptr)
        return false;

    const auto loaded = m_materialLibrary->Load(matPath);
    if (loaded.material == nullptr)
        return false;

    target->MeshComp().SetMaterial(loaded.material);
    target->MeshComp().SetBaseColor(loaded.baseColor);
    return true;
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
