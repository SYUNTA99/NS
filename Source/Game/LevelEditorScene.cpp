#include "Game/LevelEditorScene.h"

#include "Game/Block.h"
#include "Game/Player.h"

#include "Framework/App/Application.h"
#include "Framework/Core/Filesystem.h"
#include "Framework/Core/LogCategories.h"
#include "Framework/Core/Logger.h"
#include "Framework/Graphics/Material.h"
#include "Framework/Graphics/Mesh.h"
#include "Framework/Graphics/MeshPrimitives.h"
#include "Framework/Graphics/Renderer.h"
#include "Framework/Graphics/ShaderProgram.h"
#include "Framework/Graphics/Texture.h"
#include "Framework/Platform/Input.h"
#include "Framework/Platform/Keyboard.h"
#include "Framework/Platform/Window.h"
#include "Framework/Scene/IRenderable.h"
#include "Framework/Scene/MeshComponent.h"
#include "Framework/Scene/RenderContext.h"
#include "Game/Editor/BlockRegistry.h"

#include <algorithm>
#include <iterator>

namespace
{
    constexpr NS::Core::Vector3 kPlayerColor{0.85f, 0.20f, 0.20f};
    constexpr NS::Core::Vector3 kCellHalfExtents{0.5f, 0.5f, 0.5f};

    /// 編集体験の起点となる最小床。 LevelData に block 1 個 + spawn を仕込んでおく。
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
    m_cubeMesh = std::make_unique<NS::Graphics::Mesh>(renderer, meshDesc);

    NS::Graphics::TextureDesc texDesc{};
    texDesc.path = exeDir / "Assets" / "Textures" / "cube_test.png";
    texDesc.generateMipmaps = true;
    texDesc.sRGB = false;
    m_texture = std::make_unique<NS::Graphics::Texture>(renderer, texDesc);
    if (m_texture->IsUsingFallback())
        NS_LOG_WARN(::NS::Core::LogCat::Game, "LevelEditorScene: cube_test.png 読込失敗、magenta fallback で続行");

    NS::Graphics::ShaderProgramDesc shaderDesc{};
    shaderDesc.vertexShaderPath = exeDir / "Shaders" / "standard.vs.hlsl";
    shaderDesc.pixelShaderPath = exeDir / "Shaders" / "standard.ps.hlsl";
    shaderDesc.vertexEntryPoint = "VSMain";
    shaderDesc.pixelEntryPoint = "PSMain";
    shaderDesc.inputLayout = NS::Graphics::Mesh::StandardInputLayout();
    m_shader = std::make_unique<NS::Graphics::ShaderProgram>(renderer, shaderDesc);
    if (m_shader->IsUsingFallback())
        NS_LOG_WARN(::NS::Core::LogCat::Game,
                    "LevelEditorScene: standard HLSL 読込/コンパイル失敗、magenta fallback で続行");

    NS::Graphics::MaterialDesc matDesc{};
    matDesc.shader = m_shader.get();
    matDesc.constantBufferSize = sizeof(NS::Scene::FrameCB);
    matDesc.cbSlot = 0;
    matDesc.cbStages = NS::Graphics::ShaderStage::Vertex | NS::Graphics::ShaderStage::Pixel;
    m_playerMaterial = std::make_unique<NS::Graphics::Material>(renderer, matDesc);
    m_playerMaterial->SetTexture(0, m_texture.get());
    m_blockMaterial = std::make_unique<NS::Graphics::Material>(renderer, matDesc);
    m_blockMaterial->SetTexture(0, m_texture.get());

    m_player = std::make_unique<Player>(m_cubeMesh.get(), m_playerMaterial.get(), &app->Input());
    m_player->AttachScene(this);
    m_player->Root().SetPosition({0.0f, 1.0f, -4.0f});
    // cube mesh の半サイズは 0.5 だが capsule collider は radius=0.4 / halfHeight=0.5
    // (= AABB 半サイズ 0.4, 0.9, 0.4)。両者が一致するよう scale で mesh を縮める。
    m_player->Root().SetScale({0.8f, 1.8f, 0.8f});
    m_player->MeshComp().SetBaseColor(kPlayerColor);

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

    // 編集モード専用の free-fly カメラを Player /  follow camera と並列で立ち上げる。
    // MB64 の freecam に相当 (mouse + gamepad で Orbit / Pan / Zoom)。
    m_editorCameraRig = std::make_unique<EditorCameraRig>();
    m_editorCameraRig->AttachScene(this);
    m_editorCameraRig->EditorCam().SetInput(&app->Input());
    m_editorCameraRig->EditorCam().SetImGui(app->ImGui());

    auto& editorCam = m_editorCameraRig->Camera();
    editorCam.SetAspectRatioFromRenderer(renderer);
    editorCam.SetNearPlane(0.1f);
    editorCam.SetFarPlane(200.0f);
    editorCam.SetFovY(NS::Core::ToRadians(NS::Core::Degrees{60.0f}));
    editorCam.SetUp({0.0f, 1.0f, 0.0f});

    // 初期視点は spawn 位置を中心に少し引いた位置から見下ろす
    const NS::Core::Vector3 spawnPos{
        static_cast<float>(m_level.spawnX), static_cast<float>(m_level.spawnY), static_cast<float>(m_level.spawnZ)};
    m_editorCameraRig->EditorCam().SetCenter(spawnPos);
    // 起動直後は spawn block を真ん中近めに見せる距離。 1m cube が画面の十数 % を占める。
    m_editorCameraRig->EditorCam().SetDistance(5.0f);

    m_editorCameraRig->OnStart();

    // EditorMode に依存先を注入する。 mode toggle が入るまでは常時 active。
    m_editor.SetLevel(&m_level);
    m_editor.SetInput(&app->Input());
    m_editor.SetImGui(app->ImGui());
    m_editor.SetCameraComponent(&m_editorCameraRig->Camera());
    m_editor.SetEditorCamera(&m_editorCameraRig->EditorCam());
    m_editor.SetActive(true);
    // OnStart で手動 rebuild 済なので、 初回 OnUpdate の二重 rebuild を抑制
    m_editor.ClearLevelDirty();

    // 編集モード起動: Player /  follow camera を frozen / invisible に。
    // 03-06 で Play モード遷移時に SetActive(true) で再活性化する設計。
    m_player->MeshComp().SetActive(false);
    m_player->Movement().SetActive(false);
    m_player->InputComp().SetActive(false);
    m_cameraRig->Camera().SetActive(false);
    m_cameraRig->Follow().SetActive(false);
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

    const bool editActive = m_editor.IsActive();

    // Application が Renderer::Resize を排他で握っているため、 Camera の aspect ratio は
    // Renderer の現在 Size から毎フレーム pull する (callback 上書きで競合させない)。
    if (editActive)
    {
        if (m_editorCameraRig)
            m_editorCameraRig->Camera().SetAspectRatioFromRenderer(app->Renderer());
    }
    else if (m_cameraRig)
    {
        m_cameraRig->Camera().SetAspectRatioFromRenderer(app->Renderer());
    }

    // Block の Snapshot は edit / play 共通 (静的 display object なので常時)
    for (auto& block : m_blocks)
        block->Root().Snapshot();

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
        // Play モード時の Player / follow camera 駆動 (03-06 で本格 PlayMode 配線)
        if (m_player && m_cameraRig)
            m_player->InputComp().SetCameraForward(m_cameraRig->Camera().ForwardHorizontal());

        if (m_player && m_player->Root().Position().y < -10.0f)
        {
            m_player->Root().SetPosition({0.0f, 1.0f, -4.0f});
            m_player->Movement().ResetState();
        }

        if (m_player)
            m_player->Root().Snapshot();
        if (m_cameraRig)
            m_cameraRig->Root().Snapshot();

        if (m_player)
            m_player->OnUpdate();
        if (m_cameraRig)
            m_cameraRig->OnUpdate();
    }

    for (auto& block : m_blocks)
        block->OnUpdate();
}

void LevelEditorScene::OnRender()
{
    auto* app = NS::App::Application::Get();
    if (app == nullptr)
        return;

    NS::Scene::RenderContext ctx{};
    ctx.renderer = &app->Renderer();
    ctx.alpha = NS::App::Application::Alpha();

    const bool editActive = m_editor.IsActive();
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
        // SetPosition すると相対位置が discrete に動いて jitter として見える。
        m_cameraRig->Follow().ApplyCameraTransform(ctx.alpha);
        ctx.viewProjection = m_cameraRig->Camera().ViewProjection();
    }

    for (NS::Scene::IRenderable* r : m_renderList)
    {
        if (r != nullptr)
            r->Draw(ctx);
    }

    if (editActive)
    {
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
    if (m_player)
        m_player->OnEndPlay();

    m_renderList.clear();

    if (auto* app = NS::App::Application::Get())
        app->Window().SetResizeCallback({});

    m_editorCameraRig.reset();
    m_cameraRig.reset();
    m_player.reset();
    m_blocks.clear();

    m_playerMaterial.reset();
    m_blockMaterial.reset();
    m_shader.reset();
    m_texture.reset();
    m_cubeMesh.reset();
}

void LevelEditorScene::RegisterRenderable(NS::Scene::IRenderable* renderable)
{
    if (renderable == nullptr)
        return;
    // 二重登録を防ぐ。Component 側で OnStart が誤って 2 回呼ばれても二重描画にならない。
    if (std::find(m_renderList.begin(), m_renderList.end(), renderable) != m_renderList.end())
        return;
    m_renderList.push_back(renderable);
}

void LevelEditorScene::UnregisterRenderable(NS::Scene::IRenderable* renderable)
{
    if (renderable == nullptr)
        return;
    // erase-remove で全要素を消し、不変式 (一意性) と防御的削除を両立する。
    m_renderList.erase(std::remove(m_renderList.begin(), m_renderList.end(), renderable), m_renderList.end());
}

void LevelEditorScene::RebuildBlocksFromLevelData()
{
    for (auto it = m_blocks.rbegin(); it != m_blocks.rend(); ++it)
        (*it)->OnEndPlay();
    m_blocks.clear();
    m_collisionWorld.clear();

    m_blocks.reserve(m_level.blocks.size());
    m_collisionWorld.reserve(m_level.blocks.size());

    for (const auto& entry : m_level.blocks)
    {
        if (entry.blockId != NS::Game::Editor::kBlockIdSolid)
            continue;

        auto block = std::make_unique<Block>(m_cubeMesh.get(), m_blockMaterial.get(), kCellHalfExtents);
        block->AttachScene(this);
        block->Root().SetPosition(
            {static_cast<float>(entry.x), static_cast<float>(entry.y), static_cast<float>(entry.z)});
        block->Root().SetScale({kCellHalfExtents.x * 2.0f, kCellHalfExtents.y * 2.0f, kCellHalfExtents.z * 2.0f});

        const auto color = NS::Game::Editor::GetBaseColor(entry.blockId);
        block->MeshComp().SetBaseColor(NS::Core::Vector3{color.R(), color.G(), color.B()});
        block->OnStart();

        m_collisionWorld.push_back(block->Collider().WorldAABB());
        m_blocks.push_back(std::move(block));
    }

    if (m_player)
        m_player->Movement().SetCollisionWorld(m_collisionWorld);
}
