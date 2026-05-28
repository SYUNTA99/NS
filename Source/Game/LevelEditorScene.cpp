#include "Game/LevelEditorScene.h"

#include "Game/Block.h"
#include "Game/Player.h"

#include "Framework/App/Application.h"
#include "Framework/Core/Clock.h"
#include "Framework/Core/Filesystem.h"
#include "Framework/Core/LogCategories.h"
#include "Framework/Core/Logger.h"
#include "Framework/Graphics/Material.h"
#include "Framework/Graphics/Mesh.h"
#include "Framework/Graphics/MeshPrimitives.h"
#include "Framework/Graphics/Renderer.h"
#include "Framework/Graphics/ShaderProgram.h"
#include "Framework/Graphics/Skybox.h"
#include "Framework/Graphics/Texture.h"
#include "Framework/Platform/Gamepad.h"
#include "Framework/Platform/Input.h"
#include "Framework/Platform/Keyboard.h"
#include "Framework/Platform/Window.h"
#include "Framework/Scene/IRenderable.h"
#include "Framework/Scene/MeshComponent.h"
#include "Framework/Scene/RenderContext.h"
#include "Framework/UI/ImGuiContext.h"
#include "Game/Editor/BlockRegistry.h"
#include "Game/Theme/ThemeRegistry.h"

#include <algorithm>
#include <cmath>
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

    //   placeholder skybox。 kurt 6-face PNG をロードし、 取得できなければ
    // 1x1 マゼンタ cubemap fallback で続行する (描画は OnRender 末尾)。
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

namespace
{
    /// WASD / Left Stick の入力を camera 水平 forward 相対の world dir に変換する。
    /// Vector の長さが 1 を超える対角入力は normalise して移動速度の偏りを防ぐ。
    NS::Core::Vector3 BuildPlayDesiredDir(const NS::Platform::Input& input,
                                          NS::Core::Vector3 cameraForward,
                                          float& outSpeedScale)
    {
        float kbX = 0.0f;
        float kbZ = 0.0f;
        const auto& kb = input.Keyboard();
        if (kb.IsHeld(NS::Platform::Key::W))
            kbZ += 1.0f;
        if (kb.IsHeld(NS::Platform::Key::S))
            kbZ -= 1.0f;
        if (kb.IsHeld(NS::Platform::Key::A))
            kbX -= 1.0f;
        if (kb.IsHeld(NS::Platform::Key::D))
            kbX += 1.0f;

        const auto stick = input.Gamepad(0).LeftStick();
        const float ix = kbX + stick.x;
        const float iz = kbZ + stick.y;

        // 平坦化された forward と、 そこから X 軸右側を導く right を作る。
        cameraForward.y = 0.0f;
        const float fLenSq = cameraForward.x * cameraForward.x + cameraForward.z * cameraForward.z;
        if (fLenSq < 1e-6f)
            cameraForward = NS::Core::Vector3{0.0f, 0.0f, 1.0f};
        else
            cameraForward = cameraForward * (1.0f / std::sqrt(fLenSq));
        const NS::Core::Vector3 right{cameraForward.z, 0.0f, -cameraForward.x};

        NS::Core::Vector3 dir{cameraForward.x * iz + right.x * ix, 0.0f, cameraForward.z * iz + right.z * ix};
        const float lenSq = dir.x * dir.x + dir.z * dir.z;
        if (lenSq > 1.0f)
        {
            const float inv = 1.0f / std::sqrt(lenSq);
            dir.x *= inv;
            dir.z *= inv;
            outSpeedScale = 1.0f;
        }
        else
        {
            outSpeedScale = std::sqrt(lenSq);
        }
        return dir;
    }
} // namespace

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
        const float dt = NS::Core::FrameTimer::FixedDelta();

        // 入力 → PlayMode への希望移動。 ImGui の text field がキーボードを掴んでいる時は無視する。
        bool wantKb = false;
        if (auto* imgui = app->ImGui())
            wantKb = imgui->WantCaptureKeyboard();

        NS::Core::Vector3 camForward{0.0f, 0.0f, 1.0f};
        if (m_cameraRig)
            camForward = m_cameraRig->Camera().ForwardHorizontal();
        float speedScale = 0.0f;
        const NS::Core::Vector3 desired =
            wantKb ? NS::Core::Vector3{0.0f, 0.0f, 0.0f} : BuildPlayDesiredDir(app->Input(), camForward, speedScale);
        m_playMode.SetDesiredMove(desired, wantKb ? 0.0f : speedScale);

        if (!wantKb && app->Input().Keyboard().IsPressed(NS::Platform::Key::Space))
            m_playMode.SetJumpPressed();
        if (app->Input().Gamepad(0).IsPressed(NS::Platform::GamepadButton::A))
            m_playMode.SetJumpPressed();

        m_playMode.Tick(m_level, m_play, dt);

        // PlayState (SSOT) → Player.Transform の一方向同期。 ThirdPersonFollow が Player.Root
        // を target にしているため、 これで camera も自動追従する。
        if (m_player)
            m_player->Root().SetPosition(m_play.playerPosition);

        if (m_play.deathTriggered)
        {
            m_play.deathTriggered = false;
            m_playMode.Enter(m_level, m_play);
            if (m_player)
                m_player->Root().SetPosition(m_play.playerPosition);
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
        // Movement / InputComp は PlayMode が物理 / 入力を担うため休止のまま。
        m_player->Movement().SetActive(false);
        m_player->InputComp().SetActive(false);
        m_player->Root().SetPosition(m_play.playerPosition);
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
        // SetPosition すると相対位置が discrete に動いて jitter として見える。
        m_cameraRig->Follow().ApplyCameraTransform(ctx.alpha);
        ctx.viewProjection = m_cameraRig->Camera().ViewProjection();
    }

    // テーマ swap は同一 frame 内で skybox / block / lighting に同じ ThemeData を反映させる必要がある。
    // 範囲外 themeId は ThemeRegistry::Get 側で Grass にフォールバックされる。
    const ThemeData& theme = ThemeRegistry::Get(m_level.themeId);

    // 全 IRenderable に同じ theme の sun direction / lightColor / ambientColor を反映する。
    // Player の赤系 baseColor 等の個体色は MeshComponent::SetBaseColor で別途設定済なので触らない。
    for (auto& block : m_blocks)
    {
        auto& mesh = block->MeshComp();
        mesh.SetLightDirection(theme.lightDirection);
        mesh.SetLightColor(theme.lightColor);
        mesh.SetAmbientColor(theme.ambientColor);
    }
    if (m_player)
    {
        auto& mesh = m_player->MeshComp();
        mesh.SetLightDirection(theme.lightDirection);
        mesh.SetLightColor(theme.lightColor);
        mesh.SetAmbientColor(theme.ambientColor);
    }

    for (NS::Scene::IRenderable* r : m_renderList)
    {
        if (r != nullptr)
            r->Draw(ctx);
    }

    // Skybox は不透明描画後 + 編集オーバーレイ前に挟む (: depth=1 同士の
    // LESS_EQUAL 比較を成立させるため depth buffer 上の遠景 pixel が確定した直後)。
    // viewProj から camera 位置を抜くために行列の translation 行 (_41/_42/_43) を 0 化する。
    // これで skybox は常に camera 中心に追従し、 player が前進しても同じ星空を見続ける。
    if (m_skybox && m_skybox->IsValid())
    {
        // テーマが切替わった (or 起動直後) frame だけ cubemap を再ロードする。
        // 毎フレーム LoadCubemap すると DDS / 6-face PNG の I/O が常時走るので、
        // 最後にロードしたパスを記憶して差分が出た時だけ呼ぶ。
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
                // 失敗時は m_loadedSkyboxPath は更新しないので次フレームで再試行可能。
            }
        }

        const auto& cam = editActive ? m_editorCameraRig->Camera().Camera() : m_cameraRig->Camera().Camera();
        NS::Core::Matrix viewNoTranslate = cam.View();
        viewNoTranslate._41 = 0.0f;
        viewNoTranslate._42 = 0.0f;
        viewNoTranslate._43 = 0.0f;
        const NS::Core::Matrix viewProjNoTranslate = viewNoTranslate * cam.Projection();
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
    if (m_player)
        m_player->OnEndPlay();

    m_renderList.clear();

    if (auto* app = NS::App::Application::Get())
        app->Window().SetResizeCallback({});

    m_editorCameraRig.reset();
    m_cameraRig.reset();
    m_player.reset();
    m_blocks.clear();

    // Skybox は Renderer の DeviceContext を ComPtr で握っているので、
    // Renderer (Application) より先に破棄する必要がある。 m_cubeMesh と同階層で reset。
    m_skybox.reset();
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
