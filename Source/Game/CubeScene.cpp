#include "Game/CubeScene.h"

#include "ns/app/application.h"
#include "ns/core/filesystem.h"
#include "ns/core/log_categories.h"
#include "ns/core/logger.h"
#include "ns/core/math.h"
#include "ns/graphics/material.h"
#include "ns/graphics/mesh.h"
#include "ns/graphics/renderer.h"
#include "ns/graphics/shader_program.h"
#include "ns/graphics/texture.h"
#include "ns/platform/input.h"
#include "ns/platform/keyboard.h"
#include "ns/platform/window.h"

#include <DirectXMath.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace
{
    /// HLSL FrameCB と完全一致 (sizeof=160、16 byte 倍数)。
    /// SimpleMath::Matrix が row-major のため HLSL 側も row_major で揃え、転置せず転送する。
    struct alignas(16) FrameCB
    {
        ns::core::Matrix world;
        ns::core::Matrix viewProj;
        ns::core::Vector3 lightDir;
        float pad0;
        ns::core::Vector3 baseColor;
        float pad1;
    };
    static_assert(sizeof(FrameCB) % 16 == 0, "FrameCB は 16 byte 倍数 ()");

    using ns::core::Vector2;
    using ns::core::Vector3;
    using ns::graphics::MeshVertex;

    constexpr std::array<MeshVertex, 24> kCubeVertices = {{
        // +X (right)
        {{0.5f, -0.5f, 0.5f}, {0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}},
        {{0.5f, 0.5f, 0.5f}, {0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}},
        {{0.5f, 0.5f, -0.5f}, {1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}},
        {{0.5f, -0.5f, -0.5f}, {1.0f, 1.0f}, {1.0f, 0.0f, 0.0f}},
        // -X (left)
        {{-0.5f, -0.5f, -0.5f}, {0.0f, 1.0f}, {-1.0f, 0.0f, 0.0f}},
        {{-0.5f, 0.5f, -0.5f}, {0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}},
        {{-0.5f, 0.5f, 0.5f}, {1.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}},
        {{-0.5f, -0.5f, 0.5f}, {1.0f, 1.0f}, {-1.0f, 0.0f, 0.0f}},
        // +Y (top)
        {{-0.5f, 0.5f, 0.5f}, {0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}},
        {{-0.5f, 0.5f, -0.5f}, {0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},
        {{0.5f, 0.5f, -0.5f}, {1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},
        {{0.5f, 0.5f, 0.5f}, {1.0f, 1.0f}, {0.0f, 1.0f, 0.0f}},
        // -Y (bottom)
        {{-0.5f, -0.5f, -0.5f}, {0.0f, 1.0f}, {0.0f, -1.0f, 0.0f}},
        {{-0.5f, -0.5f, 0.5f}, {0.0f, 0.0f}, {0.0f, -1.0f, 0.0f}},
        {{0.5f, -0.5f, 0.5f}, {1.0f, 0.0f}, {0.0f, -1.0f, 0.0f}},
        {{0.5f, -0.5f, -0.5f}, {1.0f, 1.0f}, {0.0f, -1.0f, 0.0f}},
        // +Z (front in LH)
        {{0.5f, -0.5f, 0.5f}, {0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}},
        {{0.5f, 0.5f, 0.5f}, {0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},
        {{-0.5f, 0.5f, 0.5f}, {1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},
        {{-0.5f, -0.5f, 0.5f}, {1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}},
        // -Z (back in LH)
        {{-0.5f, -0.5f, -0.5f}, {0.0f, 1.0f}, {0.0f, 0.0f, -1.0f}},
        {{-0.5f, 0.5f, -0.5f}, {0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}},
        {{0.5f, 0.5f, -0.5f}, {1.0f, 0.0f}, {0.0f, 0.0f, -1.0f}},
        {{0.5f, -0.5f, -0.5f}, {1.0f, 1.0f}, {0.0f, 0.0f, -1.0f}},
    }};

    /// Width/Height のいずれかが 0 (最小化 / 不正サイズ) のとき aspect が 0 や inf に
    /// 落ちないよう既定の 16:9 にフォールバックする。
    [[nodiscard]] float ComputeAspectRatio(int width, int height) noexcept
    {
        if (width <= 0 || height <= 0)
            return 16.0f / 9.0f;
        return static_cast<float>(width) / static_cast<float>(height);
    }

    constexpr std::array<std::uint16_t, 36> kCubeIndices = {{
        0,  1,  2,  0,  2,  3,  // +X
        4,  5,  6,  4,  6,  7,  // -X
        8,  9,  10, 8,  10, 11, // +Y
        12, 13, 14, 12, 14, 15, // -Y
        16, 17, 18, 16, 18, 19, // +Z
        20, 21, 22, 20, 22, 23, // -Z
    }};
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

    ns::graphics::MeshDesc meshDesc{};
    meshDesc.vertices = kCubeVertices.data();
    meshDesc.vertexCount = kCubeVertices.size();
    meshDesc.indices = kCubeIndices.data();
    meshDesc.indexCount = kCubeIndices.size();
    m_mesh = std::make_unique<ns::graphics::Mesh>(renderer, meshDesc);

    ns::graphics::TextureDesc texDesc{};
    texDesc.path = exeDir / "Assets" / "Textures" / "cube_test.png";
    texDesc.generateMipmaps = true;
    texDesc.sRGB = false;
    m_texture = std::make_unique<ns::graphics::Texture>(renderer, texDesc);
    if (m_texture->IsUsingFallback())
    {
        NS_LOG_WARN(::ns::core::LogCat::Game, "CubeScene: cube_test.png 読込失敗、magenta fallback で続行");
    }

    ns::graphics::ShaderProgramDesc shaderDesc{};
    shaderDesc.vertexShaderPath = exeDir / "Shaders" / "standard.vs.hlsl";
    shaderDesc.pixelShaderPath = exeDir / "Shaders" / "standard.ps.hlsl";
    shaderDesc.vertexEntryPoint = "VSMain";
    shaderDesc.pixelEntryPoint = "PSMain";
    shaderDesc.inputLayout = ns::graphics::Mesh::StandardInputLayout();
    m_shader = std::make_unique<ns::graphics::ShaderProgram>(renderer, shaderDesc);
    if (m_shader->IsUsingFallback())
    {
        NS_LOG_WARN(::ns::core::LogCat::Game, "CubeScene: standard HLSL 読込/コンパイル失敗、magenta fallback で続行");
    }

    ns::graphics::MaterialDesc matDesc{};
    matDesc.shader = m_shader.get();
    matDesc.constantBufferSize = sizeof(FrameCB);
    matDesc.cbSlot = 0;
    matDesc.cbStages = ns::graphics::ShaderStage::Vertex | ns::graphics::ShaderStage::Pixel;
    m_material = std::make_unique<ns::graphics::Material>(renderer, matDesc);
    m_material->SetTexture(0, m_texture.get());

    m_camera.SetPosition({0.0f, 1.5f, -3.5f});
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
}

void CubeScene::OnRender()
{
    if (!m_mesh || !m_material)
        return;

    auto* app = ns::app::Application::Get();
    if (app == nullptr)
        return;

    auto& renderer = app->Renderer();
    m_camera.SetAspectRatio(ComputeAspectRatio(renderer.Width(), renderer.Height()));

    FrameCB cb{};
    ns::core::Matrix world;
    DirectX::XMStoreFloat4x4(&world, DirectX::XMMatrixRotationY(m_rotationY));
    cb.world = world;
    cb.viewProj = m_camera.ViewProjection();
    cb.lightDir = ns::core::Vector3(-0.3f, -1.0f, -0.2f);
    cb.lightDir.Normalize();
    cb.baseColor = ns::core::Vector3(1.0f, 1.0f, 1.0f);

    m_material->SetParams(cb);
    m_material->Bind();
    m_mesh->Draw();
}

void CubeScene::OnShutdown()
{
    m_material.reset();
    m_shader.reset();
    m_texture.reset();
    m_mesh.reset();
}
