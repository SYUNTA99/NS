#include "NSlib/CoreObject/Asset/AssetManager.h"
#include "NSlib/CoreObject/ClassObjs/Model.h"
#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/Gfx/D3DCommandList.h"
#include "NSlib/Gfx/D3DRenderTarget.h"
#include "NSlib/Gfx/Render/ScreenPasses.h"
#include "NSlib/Gfx/D3DTexture.h"
#include "NSlib/Gfx/D3DGlobal.h"
#include "NSlib/Gfx/D3DDevice.h"
#include "NSlib/Windows/Filesystem.h"
#include "NSlib/Windows/Window.h"

#include <gtest/gtest.h>

#include <d3d11.h>
#include <wrl/client.h>

#include "NSlib/Core/Types.h"
#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

// 隠したウィンドウで描画装置を作り、シーンを 1 回描いて型の画素を読む

namespace
{
    constexpr int k_Width = 160;
    constexpr int k_Height = 90;

    class BoxActor : public NS::SceneActor
    {
    public:
        explicit BoxActor(const NS::Vector3& position) : m_position(position) {}

        [[nodiscard]] NS::Model* Body() const { return m_body; }

    protected:
        void OnInit() override
        {
            m_body = CreateClassObj(m_model);
            NS::InitDraw(this, m_body, &Root());
            m_body->SetMeshRef("cube");
            m_body->SetMaterialRef("Assets/Materials/player.mat");
            Root().SetPosition(m_position);
        }

    private:
        NS::Vector3 m_position{};
        NS::Model* m_body = nullptr;
    };

    // 型を行の順に 1 画素 1 バイトで読む。読めなければ空
    std::vector<NS::uint8> ReadMask(const NS::Gfx::D3DTexture& mask)
    {
        ID3D11Texture2D* texture = mask.Native();
        D3D11_TEXTURE2D_DESC desc{};
        texture->GetDesc(&desc);
        if (desc.Format != DXGI_FORMAT_R8_UNORM)
        {
            ADD_FAILURE() << "型の画素形式が 1 色 8 ビットでない";
            return {};
        }
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        desc.MiscFlags = 0;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
        if (FAILED(NS::Gfx::Gpu().device->CreateTexture2D(&desc, nullptr, staging.GetAddressOf())))
        {
            return {};
        }
        NS::Gfx::Gpu().context->CopyResource(staging.Get(), texture);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(NS::Gfx::Gpu().context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
        {
            return {};
        }
        std::vector<NS::uint8> pixels;
        for (UINT y = 0; y < desc.Height; ++y)
        {
            const NS::uint8* row = static_cast<const NS::uint8*>(mapped.pData) + y * mapped.RowPitch;
            pixels.insert(pixels.end(), row, row + desc.Width);
        }
        NS::Gfx::Gpu().context->Unmap(staging.Get(), 0);
        return pixels;
    }

    class BodyMaskRig
    {
    public:
        BodyMaskRig()
            : m_window(NS::OS::WindowDesc{.title = "型抜きの試し", .size = {k_Width, k_Height}, .visible = false}),
              m_device(NS::Gfx::D3DDeviceDesc{}, m_window), m_assets(NS::OS::FileSystem::ContentRoot())
        {
            m_target = NS::Gfx::D3DRenderTarget::Create({k_Width, k_Height});
        }

        ~BodyMaskRig()
        {
            m_device.SetSceneTarget(nullptr);
            m_device.BindBackbuffer();
        }

        [[nodiscard]] bool Valid() const { return m_device.IsValid() && m_target != nullptr && m_target->IsValid(); }

        // 1 回描いて型を返す。型が無ければ空
        std::vector<NS::uint8> Render(const NS::Vector3& masked, const std::optional<NS::Vector3>& other)
        {
            NS::Scene scene;
            const NS::AssetManagerScope useAssets{m_assets};
            scene.SetDevice(&m_device);
            BoxActor* maskedBox = scene.SpawnActor<BoxActor>("BoxActor", masked);
            if (other.has_value())
            {
                (void)scene.SpawnActor<BoxActor>("BoxActor", *other);
            }
            scene.SetBodyMask({maskedBox->Body()});
            // 有効な間は前の固定ステップとの補間で描くので、無効にして今の位置で描く
            scene.SetSimulationEnabled(false);

            NS::CameraPose pose;
            pose.position = NS::Vector3{0.0f, 1.0f, -6.0f};
            pose.target = NS::Vector3{0.0f, 0.0f, 0.0f};
            m_device.BeginFrame();
            scene.SetSceneViews({NS::SceneView{m_target.get(), pose}});
            scene.OnRender();
            scene.SetSceneViews({});
            const NS::Gfx::D3DTexture* mask = scene.BodyMaskShown(0);
            if (mask == nullptr)
            {
                return {};
            }
            return ReadMask(*mask);
        }

    private:
        NS::OS::Window m_window;
        NS::Gfx::D3DDevice m_device;
        NS::AssetManager m_assets;
        std::unique_ptr<NS::Gfx::D3DRenderTarget> m_target;
    };

    int CountSet(const std::vector<NS::uint8>& mask)
    {
        int count = 0;
        for (NS::uint8 value : mask)
        {
            if (value != 0)
            {
                ++count;
            }
        }
        return count;
    }
} // namespace

TEST(BodyMask, WritesOnlyTheChosenRenderable)
{
    BodyMaskRig rig;
    ASSERT_TRUE(rig.Valid());

    const std::vector<NS::uint8> alone = rig.Render(NS::Vector3{0.0f, 0.0f, 0.0f}, std::nullopt);
    ASSERT_EQ(alone.size(), static_cast<std::size_t>(k_Width * k_Height));
    const int aloneCount = CountSet(alone);
    EXPECT_GT(aloneCount, 100);
    EXPECT_LT(aloneCount, k_Width * k_Height / 2);
    for (NS::uint8 value : alone)
    {
        EXPECT_TRUE(value == 0 || value == 255);
    }

    // 選んでいない箱を横に離して置いても、型は変わらない
    const std::vector<NS::uint8> withOther = rig.Render(NS::Vector3{0.0f, 0.0f, 0.0f}, NS::Vector3{3.0f, 0.0f, 0.0f});
    EXPECT_EQ(withOther, alone);
}

// 選んだ箱のうち、手前の箱の後ろに隠れた画素は 0
TEST(BodyMask, HiddenPixelsAreLeftOut)
{
    BodyMaskRig rig;
    ASSERT_TRUE(rig.Valid());

    const std::vector<NS::uint8> alone = rig.Render(NS::Vector3{0.0f, 0.0f, 0.0f}, std::nullopt);
    ASSERT_EQ(alone.size(), static_cast<std::size_t>(k_Width * k_Height));
    const std::vector<NS::uint8> covered = rig.Render(NS::Vector3{0.0f, 0.0f, 0.0f}, NS::Vector3{0.6f, 0.0f, -2.0f});
    ASSERT_EQ(covered.size(), alone.size());

    int hidden = 0;
    for (std::size_t i = 0; i < alone.size(); ++i)
    {
        // 隠すと減るだけで、型の外に新しく写る画素は無い
        if (alone[i] == 0)
        {
            EXPECT_EQ(covered[i], 0) << "i=" << i;
        }
        else if (covered[i] == 0)
        {
            ++hidden;
        }
    }
    EXPECT_GT(hidden, 50) << "alone=" << CountSet(alone) << " covered=" << CountSet(covered);
    EXPECT_GT(CountSet(covered), 50);
}

// 型を描き終えると、型を描く前の描き先・深度・ビューポートへ戻る
TEST(BodyMask, EndReturnsToTheTargetsBeforeBegin)
{
    NS::OS::Window window(NS::OS::WindowDesc{.title = "型の描き先の試し", .size = {16, 16}, .visible = false});
    NS::Gfx::D3DDevice device(NS::Gfx::D3DDeviceDesc{}, window);
    ASSERT_TRUE(device.IsValid());
    NS::AssetManager assets(NS::OS::FileSystem::ContentRoot());
    NS::Gfx::ScreenPasses passes(assets.LoadBuiltinShaders());
    ASSERT_TRUE(passes.IsValid());

    device.BeginFrame();
    NS::Gfx::D3DCommandList& cmd = device.Commands();
    cmd.SetViewport(12.0f, 10.0f);
    NS::Gfx::ComPtr<ID3D11RenderTargetView> firstColor;
    NS::Gfx::ComPtr<ID3D11DepthStencilView> firstDepth;
    cmd->OMGetRenderTargets(1, firstColor.GetAddressOf(), firstDepth.GetAddressOf());
    ASSERT_NE(firstColor, nullptr);

    ASSERT_TRUE(passes.BeginBodyMask(device));
    NS::Gfx::ComPtr<ID3D11RenderTargetView> maskColor;
    cmd->OMGetRenderTargets(1, maskColor.GetAddressOf(), nullptr);
    EXPECT_NE(maskColor, firstColor);
    passes.EndBodyMask(device);

    NS::Gfx::ComPtr<ID3D11RenderTargetView> color;
    NS::Gfx::ComPtr<ID3D11DepthStencilView> depth;
    cmd->OMGetRenderTargets(1, color.GetAddressOf(), depth.GetAddressOf());
    EXPECT_EQ(color, firstColor);
    EXPECT_EQ(depth, firstDepth);
    D3D11_VIEWPORT viewport{};
    UINT viewportCount = 1;
    cmd->RSGetViewports(&viewportCount, &viewport);
    ASSERT_EQ(viewportCount, 1u);
    EXPECT_FLOAT_EQ(viewport.Width, 12.0f);
    EXPECT_FLOAT_EQ(viewport.Height, 10.0f);
}
