#include "Game/CubeScene.h"

#include "ns/app/application.h"
#include "ns/core/filesystem.h"
#include "ns/core/log_categories.h"
#include "ns/core/logger.h"
#include "ns/core/math.h"
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
#include "ns/scene/render_context.h"

#include <DirectXMath.h>

namespace
{
    /// Width/Height が 0 (最小化 / 不正サイズ) のとき aspect 0/inf 回避で 16:9 にフォールバック。
    [[nodiscard]] float ComputeAspectRatio(int width, int height) noexcept
    {
        if (width <= 0 || height <= 0)
            return 16.0f / 9.0f;
        return static_cast<float>(width) / static_cast<float>(height);
    }
} // namespace

CubeScene::CubeScene() = default;
CubeScene::~CubeScene() = default;

void CubeScene::OnStart()
{
    auto* app = ns::app::Application::Get();
    if (app == nullptr)
    {
        NS_LOG_ERROR(::ns::core::LogCat::Game, "CubeScene::OnStart: Application::Get()==null");
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
    m_mesh = std::make_unique<ns::graphics::Mesh>(renderer, meshDesc);

    ns::graphics::TextureDesc texDesc{};
    texDesc.path = exeDir / "Assets" / "Textures" / "cube_test.png";
    texDesc.generateMipmaps = true;
    texDesc.sRGB = false;
    m_texture = std::make_unique<ns::graphics::Texture>(renderer, texDesc);
    if (m_texture->IsUsingFallback())
        NS_LOG_WARN(::ns::core::LogCat::Game, "CubeScene: cube_test.png 読込失敗、magenta fallback で続行");

    ns::graphics::ShaderProgramDesc shaderDesc{};
    shaderDesc.vertexShaderPath = exeDir / "Shaders" / "standard.vs.hlsl";
    shaderDesc.pixelShaderPath = exeDir / "Shaders" / "standard.ps.hlsl";
    shaderDesc.vertexEntryPoint = "VSMain";
    shaderDesc.pixelEntryPoint = "PSMain";
    shaderDesc.inputLayout = ns::graphics::Mesh::StandardInputLayout();
    m_shader = std::make_unique<ns::graphics::ShaderProgram>(renderer, shaderDesc);
    if (m_shader->IsUsingFallback())
        NS_LOG_WARN(::ns::core::LogCat::Game, "CubeScene: standard HLSL 読込/コンパイル失敗、magenta fallback で続行");

    ns::graphics::MaterialDesc matDesc{};
    matDesc.shader = m_shader.get();
    matDesc.constantBufferSize = sizeof(ns::scene::FrameCB);
    matDesc.cbSlot = 0;
    matDesc.cbStages = ns::graphics::ShaderStage::Vertex | ns::graphics::ShaderStage::Pixel;
    m_material = std::make_unique<ns::graphics::Material>(renderer, matDesc);
    m_material->SetTexture(0, m_texture.get());

    m_meshComponent = std::make_unique<ns::scene::MeshComponent>(m_mesh.get(), m_material.get());
    m_cubeActor.RegisterComponent(m_meshComponent.get());

    m_camera.SetPosition({2.5f, 2.0f, -4.0f});
    m_camera.SetTarget({0.0f, 0.0f, 0.0f});
    m_camera.SetUp({0.0f, 1.0f, 0.0f});
    m_camera.SetFovY(ns::core::Deg2Rad(60.0f));
    m_camera.SetAspectRatio(ComputeAspectRatio(renderer.Width(), renderer.Height()));
    m_camera.SetNearPlane(0.1f);
    m_camera.SetFarPlane(100.0f);
}

void CubeScene::OnUpdate(float dt)
{
    auto* app = ns::app::Application::Get();
    if (app == nullptr)
        return;

    if (app->Input().Keyboard().IsPressed(ns::platform::Key::Escape))
    {
        ns::app::Application::Quit();
        return;
    }

    m_rotationY += dt;
    ns::core::Quaternion rot;
    DirectX::XMStoreFloat4(&rot, DirectX::XMQuaternionRotationRollPitchYaw(0.0f, m_rotationY, 0.0f));
    m_cubeActor.Root().SetRotation(rot);

    m_cubeActor.OnUpdate(dt);

    // Glenn Fiedler accumulator パターン: fixed step 完了直後に previous 退避 ()
    m_cubeActor.Root().Snapshot();
}

void CubeScene::OnRender()
{
    if (!m_meshComponent)
        return;

    auto* app = ns::app::Application::Get();
    if (app == nullptr)
        return;

    auto& renderer = app->Renderer();
    m_camera.SetAspectRatio(ComputeAspectRatio(renderer.Width(), renderer.Height()));

    ns::scene::RenderContext ctx{};
    ctx.renderer = &renderer;
    ctx.viewProjection = m_camera.ViewProjection();
    ctx.alpha = ns::app::Application::Alpha();

    m_meshComponent->Draw(ctx);
}

void CubeScene::OnShutdown()
{
    m_meshComponent.reset();
    m_material.reset();
    m_shader.reset();
    m_texture.reset();
    m_mesh.reset();
}
