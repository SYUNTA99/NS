#include "Game/MainScene.h"

#include "Game/Block.h"
#include "Game/Player.h"

#include "ns/app/application.h"
#include "ns/core/filesystem.h"
#include "ns/core/log_categories.h"
#include "ns/core/logger.h"
#include "ns/graphics/material.h"
#include "ns/graphics/mesh.h"
#include "ns/graphics/mesh_primitives.h"
#include "ns/graphics/renderer.h"
#include "ns/graphics/shader_program.h"
#include "ns/graphics/texture.h"
#include "ns/platform/input.h"
#include "ns/platform/keyboard.h"
#include "ns/platform/window.h"
#include "ns/scene/components/mesh_component.h"
#include "ns/scene/i_renderable.h"
#include "ns/scene/render_context.h"

#include <algorithm>
#include <iterator>

namespace
{
    struct BlockDef
    {
        ns::core::Vector3 position;
        ns::core::Vector3 halfExtents;
    };

    constexpr BlockDef kInitialLevel[] = {
        {{0.0f, -0.5f, 0.0f}, {8.0f, 0.5f, 8.0f}},
        {{-4.0f, 1.0f, 6.0f}, {0.5f, 1.5f, 0.5f}},
        {{-2.0f, 1.0f, 6.0f}, {0.5f, 1.5f, 0.5f}},
        {{0.0f, 1.0f, 6.0f}, {0.5f, 1.5f, 0.5f}},
        {{2.0f, 1.0f, 6.0f}, {0.5f, 1.5f, 0.5f}},
        {{4.0f, 1.0f, 6.0f}, {0.5f, 1.5f, 0.5f}},
        {{-3.0f, 0.5f, 0.0f}, {1.0f, 0.5f, 1.0f}},
        {{3.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 1.0f}},
    };

    constexpr ns::core::Vector3 kPlayerColor{0.85f, 0.20f, 0.20f};
    constexpr ns::core::Vector3 kBlockColor{0.70f, 0.70f, 0.75f};
} // namespace

MainScene::MainScene() : m_camera(), m_follow(nullptr) {}

MainScene::~MainScene() = default;

void MainScene::OnStart()
{
    auto* app = ns::app::Application::Get();
    if (app == nullptr)
    {
        NS_LOG_ERROR(::ns::core::LogCat::Game, "MainScene::OnStart: Application::Get()==null");
        return;
    }

    auto& renderer = app->Renderer();
    const auto exeDir = ns::core::FileSystem::GetExeDirectory();

    auto cubeGeom = ns::graphics::MakeCube({0.5f, 0.5f, 0.5f});
    ns::graphics::MeshDesc meshDesc{};
    meshDesc.vertices = cubeGeom.vertices.data();
    meshDesc.vertexCount = cubeGeom.vertices.size();
    meshDesc.indices = cubeGeom.indices.data();
    meshDesc.indexCount = cubeGeom.indices.size();
    m_cubeMesh = std::make_unique<ns::graphics::Mesh>(renderer, meshDesc);

    ns::graphics::TextureDesc texDesc{};
    texDesc.path = exeDir / "Assets" / "Textures" / "cube_test.png";
    texDesc.generateMipmaps = true;
    texDesc.sRGB = false;
    m_texture = std::make_unique<ns::graphics::Texture>(renderer, texDesc);
    if (m_texture->IsUsingFallback())
        NS_LOG_WARN(::ns::core::LogCat::Game, "MainScene: cube_test.png 読込失敗、magenta fallback で続行");

    ns::graphics::ShaderProgramDesc shaderDesc{};
    shaderDesc.vertexShaderPath = exeDir / "Shaders" / "standard.vs.hlsl";
    shaderDesc.pixelShaderPath = exeDir / "Shaders" / "standard.ps.hlsl";
    shaderDesc.vertexEntryPoint = "VSMain";
    shaderDesc.pixelEntryPoint = "PSMain";
    shaderDesc.inputLayout = ns::graphics::Mesh::StandardInputLayout();
    m_shader = std::make_unique<ns::graphics::ShaderProgram>(renderer, shaderDesc);
    if (m_shader->IsUsingFallback())
        NS_LOG_WARN(::ns::core::LogCat::Game, "MainScene: standard HLSL 読込/コンパイル失敗、magenta fallback で続行");

    ns::graphics::MaterialDesc matDesc{};
    matDesc.shader = m_shader.get();
    matDesc.constantBufferSize = sizeof(ns::scene::FrameCB);
    matDesc.cbSlot = 0;
    matDesc.cbStages = ns::graphics::ShaderStage::Vertex | ns::graphics::ShaderStage::Pixel;
    m_playerMaterial = std::make_unique<ns::graphics::Material>(renderer, matDesc);
    m_playerMaterial->SetTexture(0, m_texture.get());
    m_blockMaterial = std::make_unique<ns::graphics::Material>(renderer, matDesc);
    m_blockMaterial->SetTexture(0, m_texture.get());

    m_player = std::make_unique<Player>(m_cubeMesh.get(), m_playerMaterial.get(), &app->Input());
    m_player->AttachScene(this);
    m_player->Root().SetPosition({0.0f, 1.0f, -4.0f});
    m_player->MeshComp().SetBaseColor(kPlayerColor);

    m_collisionWorld.clear();
    m_collisionWorld.reserve(std::size(kInitialLevel));
    m_blocks.reserve(std::size(kInitialLevel));
    for (const auto& def : kInitialLevel)
    {
        auto block = std::make_unique<Block>(m_cubeMesh.get(), m_blockMaterial.get(), def.halfExtents);
        block->AttachScene(this);
        block->Root().SetPosition(def.position);
        block->Root().SetScale({def.halfExtents.x * 2.0f, def.halfExtents.y * 2.0f, def.halfExtents.z * 2.0f});
        block->MeshComp().SetBaseColor(kBlockColor);

        m_collisionWorld.push_back(block->Collider().WorldAABB());
        m_blocks.push_back(std::move(block));
    }
    m_player->Movement().SetCollisionWorld(m_collisionWorld);

    m_cameraActor = std::make_unique<ns::scene::GameObject>();
    m_cameraActor->AttachScene(this);
    m_cameraActor->RegisterComponent(&m_camera);
    m_cameraActor->RegisterComponent(&m_follow);
    m_follow.SetTarget(&m_player->Root());
    m_follow.SetCamera(&m_camera);
    m_follow.SetInput(&app->Input());
    m_follow.SetMovement(&m_player->Movement());

    m_camera.SetAspectRatioFromRenderer(renderer);
    m_camera.SetNearPlane(0.1f);
    m_camera.SetFarPlane(100.0f);
    m_camera.SetFovY(m_follow.FovY());
    m_camera.SetUp({0.0f, 1.0f, 0.0f});

    m_player->OnStart();
    for (auto& block : m_blocks)
        block->OnStart();
    m_cameraActor->OnStart();

    app->Window().SetResizeCallback([this](int w, int h) {
        if (w <= 0 || h <= 0)
            return;
        m_camera.SetAspectRatio(static_cast<float>(w) / static_cast<float>(h));
    });
}

void MainScene::OnUpdate(float dt)
{
    auto* app = ns::app::Application::Get();
    if (app == nullptr)
        return;

    if (app->Input().Keyboard().IsPressed(ns::platform::Key::Escape))
    {
        ns::app::Application::Quit();
        return;
    }

    if (m_player)
        m_player->InputComp().SetCameraForward(m_camera.ForwardHorizontal());

    if (m_player)
        m_player->Root().Snapshot();
    for (auto& block : m_blocks)
        block->Root().Snapshot();
    if (m_cameraActor)
        m_cameraActor->Root().Snapshot();

    if (m_player)
        m_player->OnUpdate(dt);
    for (auto& block : m_blocks)
        block->OnUpdate(dt);
    if (m_cameraActor)
        m_cameraActor->OnUpdate(dt);
}

void MainScene::OnRender()
{
    auto* app = ns::app::Application::Get();
    if (app == nullptr)
        return;

    ns::scene::RenderContext ctx{};
    ctx.renderer = &app->Renderer();
    ctx.viewProjection = m_camera.ViewProjection();
    ctx.alpha = ns::app::Application::Alpha();
    for (ns::scene::IRenderable* r : m_renderList)
    {
        if (r != nullptr)
            r->Draw(ctx);
    }
}

void MainScene::OnShutdown()
{
    if (m_cameraActor)
        m_cameraActor->OnEndPlay();
    for (auto it = m_blocks.rbegin(); it != m_blocks.rend(); ++it)
        (*it)->OnEndPlay();
    if (m_player)
        m_player->OnEndPlay();

    m_renderList.clear();

    if (auto* app = ns::app::Application::Get())
        app->Window().SetResizeCallback({});

    m_cameraActor.reset();
    m_player.reset();
    m_blocks.clear();

    m_playerMaterial.reset();
    m_blockMaterial.reset();
    m_shader.reset();
    m_texture.reset();
    m_cubeMesh.reset();
}

void MainScene::RegisterRenderable(ns::scene::IRenderable* renderable)
{
    if (renderable == nullptr)
        return;
    m_renderList.push_back(renderable);
}

void MainScene::UnregisterRenderable(ns::scene::IRenderable* renderable)
{
    if (renderable == nullptr)
        return;
    auto it = std::find(m_renderList.begin(), m_renderList.end(), renderable);
    if (it != m_renderList.end())
        m_renderList.erase(it);
}
