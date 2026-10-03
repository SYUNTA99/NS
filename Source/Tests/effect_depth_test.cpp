#include "Runtime/Core/CameraData.h"
#include "Runtime/Graphics/EffectScene.h"
#include "Runtime/Graphics/GraphicObject.h"
#include "Runtime/Graphics/Texture.h"
#include "TestEffectFiles.h"

#include <gtest/gtest.h>

#include <d3d11.h>
#include <wrl/client.h>

#include <memory>
#include <vector>

// 奥行きをエフェクトへ渡す道 (impact-feel-pass R-7-1) は描画装置が無いと通らない。窓の要らないソフトウェアの描画装置
// (WARP) を作り、層の外から Gpu() へ差して確かめる

namespace
{
    using Microsoft::WRL::ComPtr;

    // 試しの間だけ WARP の描画装置を Gpu() に差す。作れない機械では空のまま
    struct ScopedWarpGpu
    {
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;

        ScopedWarpGpu()
        {
            const HRESULT hr = ::D3D11CreateDevice(nullptr,
                                                   D3D_DRIVER_TYPE_WARP,
                                                   nullptr,
                                                   0,
                                                   nullptr,
                                                   0,
                                                   D3D11_SDK_VERSION,
                                                   device.GetAddressOf(),
                                                   nullptr,
                                                   context.GetAddressOf());
            if (FAILED(hr))
            {
                device.Reset();
                context.Reset();
                return;
            }
            NS::Gfx::Gpu().device = device.Get();
            NS::Gfx::Gpu().context = context.Get();
        }

        ~ScopedWarpGpu()
        {
            NS::Gfx::Gpu().device = nullptr;
            NS::Gfx::Gpu().context = nullptr;
        }

        [[nodiscard]] bool Valid() const { return device != nullptr; }
    };

    std::unique_ptr<NS::Gfx::Texture> MakeDepth(UINT width, UINT height, DXGI_FORMAT format)
    {
        NS::Gfx::TextureDesc desc{};
        desc.width = width;
        desc.height = height;
        desc.format = format;
        desc.bindFlags = D3D11_BIND_DEPTH_STENCIL;
        return NS::Gfx::Texture::Create(desc);
    }

    NS::Core::CameraData MakeCamera()
    {
        NS::Core::CameraData camera;
        camera.SetPosition(NS::Core::Vector3{0.0f, 1.0f, -5.0f});
        camera.SetTarget(NS::Core::Vector3{0.0f, 0.0f, 0.0f});
        return camera;
    }

    void BindDepth(const ScopedWarpGpu& gpu, const NS::Gfx::Texture& depth)
    {
        gpu.context->OMSetRenderTargets(0, nullptr, depth.Dsv());
    }
} // namespace

// 書式の欄で読むビューの書式を変えられる。渡さなければ作った書式のまま
TEST(EffectDepth, TextureViewFormatIsUsedOnlyWhenGiven)
{
    ScopedWarpGpu gpu;
    if (!gpu.Valid())
    {
        GTEST_SKIP() << "WARP の描画装置を作れなかった";
    }
    NS::Gfx::TextureDesc depthCopy{};
    depthCopy.width = 8;
    depthCopy.height = 4;
    depthCopy.format = DXGI_FORMAT_R24G8_TYPELESS;
    depthCopy.viewFormat = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    const std::unique_ptr<NS::Gfx::Texture> copy = NS::Gfx::Texture::Create(depthCopy);
    ASSERT_NE(copy->Srv(), nullptr);
    D3D11_SHADER_RESOURCE_VIEW_DESC view{};
    copy->Srv()->GetDesc(&view);
    EXPECT_EQ(view.Format, DXGI_FORMAT_R24_UNORM_X8_TYPELESS);

    NS::Gfx::TextureDesc plain{};
    plain.width = 8;
    plain.height = 4;
    const std::unique_ptr<NS::Gfx::Texture> color = NS::Gfx::Texture::Create(plain);
    ASSERT_NE(color->Srv(), nullptr);
    color->Srv()->GetDesc(&view);
    EXPECT_EQ(view.Format, DXGI_FORMAT_R8G8B8A8_UNORM);
}

// 結ばれた奥行きを写してエフェクトへ渡す。大きさの違う 2 つの描画先を交互に描いても写し先を作り直さない
TEST(EffectDepth, PassesTheBoundDepthAndKeepsOneCopyPerSize)
{
    ScopedWarpGpu gpu;
    if (!gpu.Valid())
    {
        GTEST_SKIP() << "WARP の描画装置を作れなかった";
    }
    NS::Gfx::EffectScene effects("Assets/Effects");
    ASSERT_TRUE(effects.IsValid());
    const std::unique_ptr<NS::Gfx::Texture> game = MakeDepth(64, 32, DXGI_FORMAT_D24_UNORM_S8_UINT);
    const std::unique_ptr<NS::Gfx::Texture> editor = MakeDepth(48, 48, DXGI_FORMAT_D24_UNORM_S8_UINT);
    ASSERT_NE(game->Dsv(), nullptr);
    ASSERT_NE(editor->Dsv(), nullptr);
    const NS::Core::CameraData camera = MakeCamera();

    for (int frame = 0; frame < 3; ++frame)
    {
        BindDepth(gpu, *game);
        effects.Draw(camera);
        EXPECT_TRUE(effects.PassesDepth());
        BindDepth(gpu, *editor);
        effects.Draw(camera);
        EXPECT_TRUE(effects.PassesDepth());
    }
    EXPECT_EQ(effects.DepthCopiesCreated(), 2u);
    EXPECT_FALSE(effects.IsUsingDepthFallback());
}

// 写せない奥行きの時は奥行き無しで描き、前の奥行きを残さない。読み口が真になる
TEST(EffectDepth, UnreadableDepthFallsBackToDrawingWithoutIt)
{
    ScopedWarpGpu gpu;
    if (!gpu.Valid())
    {
        GTEST_SKIP() << "WARP の描画装置を作れなかった";
    }
    NS::Gfx::EffectScene effects("Assets/Effects");
    ASSERT_TRUE(effects.IsValid());
    const std::unique_ptr<NS::Gfx::Texture> good = MakeDepth(32, 32, DXGI_FORMAT_D24_UNORM_S8_UINT);
    const std::unique_ptr<NS::Gfx::Texture> other = MakeDepth(32, 32, DXGI_FORMAT_D32_FLOAT);
    ASSERT_NE(other->Dsv(), nullptr);
    const NS::Core::CameraData camera = MakeCamera();

    BindDepth(gpu, *good);
    effects.Draw(camera);
    ASSERT_TRUE(effects.PassesDepth());
    BindDepth(gpu, *other);
    effects.Draw(camera);
    EXPECT_FALSE(effects.PassesDepth());
    EXPECT_TRUE(effects.IsUsingDepthFallback());
    // 戻せる奥行きなら戻る
    BindDepth(gpu, *good);
    effects.Draw(camera);
    EXPECT_TRUE(effects.PassesDepth());
    EXPECT_FALSE(effects.IsUsingDepthFallback());
}

namespace
{
    // 柔らかい粒の Far の距離が 0 でない節を数える
    int CountSoftNodesIn(const char* path)
    {
        const Effekseer::EffectRef effect = LoadEffectFile(path);
        if (effect == nullptr)
        {
            return -1;
        }
        std::vector<Effekseer::EffectNode*> nodes;
        CollectEffectNodesInDrawOrder(effect->GetRoot(), nodes);
        int count = 0;
        for (Effekseer::EffectNode* node : nodes)
        {
            if (node->GetBasicRenderParameter().SoftParticleDistanceFar > 0.0f)
            {
                ++count;
            }
        }
        return count;
    }
} // namespace

// 柔らかく消す欄を入れた節は、書き出した絵でも Far の距離が 0 でない。綴りを誤ると黙って 0 のまま組まれる
TEST(EffectDepth, ShippedChargeLayersCarryTheSoftParticleDistance)
{
    EXPECT_EQ(CountSoftNodesIn("Assets/Effects/charge.gather.efkefc"), 21);
    EXPECT_EQ(CountSoftNodesIn("Assets/Effects/charge.curl.efkefc"), 2);
    EXPECT_EQ(CountSoftNodesIn("Assets/Effects/charge.full.efkefc"), 2);
    // 床に寝かせた照りには入れない。全面が床と同じ面で丸ごと消える
    EXPECT_EQ(CountSoftNodesIn("Assets/Effects/impact.glow.efkefc"), 0);
}
