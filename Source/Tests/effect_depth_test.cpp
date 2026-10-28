#include "NSlib/Core/CameraData.h"
#include "NSlib/Core/Types.h"
#include "NSlib/Core/Windows/Filesystem.h"
#include "NSlib/Core/Windows/Window.h"
#include "NSlib/CoreObject/Asset/AssetManager.h"
#include "NSlib/CoreObject/Asset/EffectAsset.h"
#include "NSlib/CoreObject/Asset/EffectCatalog.h"
#include "NSlib/CoreObject/ClassObjs/Effect.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/Gfx/D3DDevice.h"
#include "NSlib/Gfx/D3DGlobal.h"
#include "NSlib/Gfx/D3DTexture.h"
#include "NSlib/Gfx/Render/EffectScene.h"
#include "TestEffectFiles.h"
#include "TestLogCapture.h"

#include <gtest/gtest.h>

#include <d3d11.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <vector>

// 奥行きをエフェクトへ渡す道は描画装置が無いと通らない。窓の要らないソフトウェアの描画装置
// (WARP) を作り、層の外から Gpu() へ差して確かめる

namespace
{
    using Microsoft::WRL::ComPtr;

    // 試しの間だけ WARP の描画装置を Gpu() に差す。作れない機械では空のまま
    struct ScopedWarpGpu
    {
        ComPtr<ID3D11Device> d3d11Device;
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
                                                   d3d11Device.GetAddressOf(),
                                                   nullptr,
                                                   context.GetAddressOf());
            if (FAILED(hr))
            {
                d3d11Device.Reset();
                context.Reset();
                return;
            }
            NS::Gfx::Gpu().device = d3d11Device.Get();
            NS::Gfx::Gpu().context = context.Get();
        }

        ~ScopedWarpGpu()
        {
            NS::Gfx::Gpu().device = nullptr;
            NS::Gfx::Gpu().context = nullptr;
        }

        [[nodiscard]] bool Valid() const { return d3d11Device != nullptr; }
    };

    std::unique_ptr<NS::Gfx::D3DTexture> MakeDepth(UINT width, UINT height, DXGI_FORMAT format)
    {
        NS::Gfx::D3DTextureDesc desc{};
        desc.width = width;
        desc.height = height;
        desc.format = format;
        desc.bindFlags = D3D11_BIND_DEPTH_STENCIL;
        return NS::Gfx::D3DTexture::Create(desc);
    }

    NS::CameraData MakeCamera()
    {
        NS::CameraData camera;
        camera.SetPosition(NS::Vector3{0.0f, 1.0f, -5.0f});
        camera.SetTarget(NS::Vector3{0.0f, 0.0f, 0.0f});
        return camera;
    }

    void BindDepth(const ScopedWarpGpu& gpu, const NS::Gfx::D3DTexture& depth)
    {
        gpu.context->OMSetRenderTargets(0, nullptr, depth.Dsv());
    }
    std::vector<NS::uint32> CaptureEffectPixels(NS::Gfx::EffectScene& effects)
    {
        const NS::Gfx::D3DGlobal& gpu = NS::Gfx::Gpu();
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = 128;
        desc.Height = 128;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET;
        ComPtr<ID3D11Texture2D> target;
        if (FAILED(gpu.device->CreateTexture2D(&desc, nullptr, target.GetAddressOf())))
        {
            ADD_FAILURE() << "描画先を作れなかった";
            return {};
        }
        ComPtr<ID3D11RenderTargetView> view;
        if (FAILED(gpu.device->CreateRenderTargetView(target.Get(), nullptr, view.GetAddressOf())))
        {
            ADD_FAILURE() << "描画先のビューを作れなかった";
            return {};
        }
        const float clear[4]{};
        gpu.context->ClearRenderTargetView(view.Get(), clear);
        ID3D11RenderTargetView* views[]{view.Get()};
        gpu.context->OMSetRenderTargets(1, views, nullptr);
        const D3D11_VIEWPORT viewport{0.0f, 0.0f, 128.0f, 128.0f, 0.0f, 1.0f};
        gpu.context->RSSetViewports(1, &viewport);
        NS::CameraData camera = MakeCamera();
        camera.SetPosition({0.0f, 3.0f, -5.0f});
        effects.Draw(camera);
        gpu.context->OMSetRenderTargets(0, nullptr, nullptr);
        desc.BindFlags = 0;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> staging;
        if (FAILED(gpu.device->CreateTexture2D(&desc, nullptr, staging.GetAddressOf())))
        {
            ADD_FAILURE() << "読み戻し先を作れなかった";
            return {};
        }
        gpu.context->CopyResource(staging.Get(), target.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(gpu.context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
        {
            ADD_FAILURE() << "描いた画素を読み戻せなかった";
            return {};
        }
        std::vector<NS::uint32> pixels;
        pixels.reserve(desc.Width * desc.Height);
        for (UINT y = 0; y < desc.Height; ++y)
        {
            const NS::uint8* row = static_cast<const NS::uint8*>(mapped.pData) + y * mapped.RowPitch;
            for (UINT x = 0; x < desc.Width; ++x)
            {
                NS::uint32 pixel = 0;
                std::memcpy(&pixel, row + x * 4, sizeof(pixel));
                pixels.push_back(pixel);
            }
        }
        gpu.context->Unmap(staging.Get(), 0);
        return pixels;
    }

    int CountEffectPixels(NS::Gfx::EffectScene& effects)
    {
        const std::vector<NS::uint32> pixels = CaptureEffectPixels(effects);
        if (pixels.empty())
        {
            return -1;
        }
        return static_cast<int>(
            std::count_if(pixels.begin(), pixels.end(), [](NS::uint32 pixel) { return (pixel & 0x00FFFFFFu) != 0; }));
    }
} // namespace

TEST(EffectAssetsRender, EveryShippedAssetKeepsAllSixtyFramesAfterMigration)
{
    NS::OS::Window window(NS::OS::WindowDesc{.title = "素材参照の描画比較", .size = {32, 32}, .visible = false});
    NS::Gfx::D3DDevice device(NS::Gfx::D3DDeviceDesc{}, window);
    ASSERT_TRUE(device.IsValid());
    const std::string root = NS::OS::FileSystem::ContentRoot();
    NS::AssetManager assets(root);
    NS::Gfx::EffectScene original(NS::OS::FileSystem::Combine(root, "Assets/Effects"));
    NS::Gfx::EffectScene migrated(NS::OS::FileSystem::Combine(root, "Assets/Effects"));
    int compared = 0;
    for (const NS::EffectAssetFile& file : NS::ReadEffectAssetFiles(root))
    {
        if (file.isSet)
        {
            continue;
        }
        SCOPED_TRACE(file.path);
        const NS::EffectAsset* asset = assets.LoadEffectAsset(file.id);
        ASSERT_TRUE(asset->IsValid());
        ASSERT_TRUE(original.Preload(asset->Resource().name));
        ASSERT_TRUE(migrated.Preload(asset->Resource()));
        NS::Gfx::EffectPlayDesc desc;
        for (std::optional<float>& input : desc.dynamicInputs)
        {
            input = 1.0f;
        }
        if (asset->Resource().name == "impact.embers")
        {
            desc.dynamicInputs[0] = 50.0f;
            desc.dynamicInputs[1] = 0.05f;
        }
        const NS::Gfx::EffectHandle originalHandle = original.Play(asset->Resource().name, desc);
        const NS::Gfx::EffectHandle migratedHandle = migrated.Play(asset->Resource(), desc);
        ASSERT_TRUE(originalHandle.IsValid());
        ASSERT_TRUE(migratedHandle.IsValid());
        bool visible = false;
        for (int frame = 0; frame < 60; ++frame)
        {
            SCOPED_TRACE(frame);
            const NS::Vector3 position{0.0f, 0.0f, static_cast<float>(frame % 30) * 0.1f - 1.5f};
            original.SetTransform(originalHandle, position, NS::Quaternion::Identity, desc.scale);
            migrated.SetTransform(migratedHandle, position, NS::Quaternion::Identity, desc.scale);
            original.Update(1.0f / 60.0f);
            migrated.Update(1.0f / 60.0f);
            const std::vector<NS::uint32> before = CaptureEffectPixels(original);
            const std::vector<NS::uint32> after = CaptureEffectPixels(migrated);
            ASSERT_FALSE(before.empty());
            ASSERT_EQ(before, after);
            visible |=
                std::any_of(before.begin(), before.end(), [](NS::uint32 pixel) { return (pixel & 0x00FFFFFFu) != 0; });
        }
        EXPECT_TRUE(visible);
        original.StopAll();
        migrated.StopAll();
        original.Update(0.0f);
        migrated.Update(0.0f);
        ++compared;
    }
    const std::vector<std::string> shipped =
        NS::OS::FileSystem::ListFilesRecursive(NS::OS::FileSystem::Combine(root, "Assets/Effects"), ".efkefc");
    ASSERT_FALSE(shipped.empty());
    EXPECT_EQ(static_cast<std::size_t>(compared), shipped.size());
}

TEST(EffectAssetsRender, RemovedAssetDoesNotReplayItsPreviouslyCachedResource)
{
    NS::OS::Window window(NS::OS::WindowDesc{.title = "参照切れの再生確認", .size = {32, 32}, .visible = false});
    NS::Gfx::D3DDevice device(NS::Gfx::D3DDeviceDesc{}, window);
    ASSERT_TRUE(device.IsValid());
    const std::string root = NS::OS::FileSystem::ContentRoot();
    NS::AssetManager assets(root);
    NS::Gfx::EffectScene scene(NS::OS::FileSystem::Combine(root, "Assets/Effects"));
    NS::EffectAsset* asset = assets.LoadEffectAsset("6e4f2fccb9df5a339885b53d42fafd45");
    ASSERT_TRUE(scene.Preload(asset->Resource()));
    asset->Invalidate();
    NS::Effect owner;
    const NS::uint32 id = owner.Play(&scene, asset, {});
    ASSERT_NE(owner.Find(id), nullptr);
    EXPECT_EQ(owner.Find(id)->assetId, asset->Id());
    EXPECT_FALSE(owner.Find(id)->handle.IsValid());
}

TEST(EffectAssetsRender, PreloadingTheSameFileRefreshesItsRandomBasis)
{
    NS::OS::Window window(NS::OS::WindowDesc{.title = "乱数の基準の読み直し", .size = {32, 32}, .visible = false});
    NS::Gfx::D3DDevice device(NS::Gfx::D3DDeviceDesc{}, window);
    ASSERT_TRUE(device.IsValid());
    const std::string root = NS::OS::FileSystem::ContentRoot();
    NS::AssetManager assets(root);
    const NS::EffectAsset* asset = assets.LoadEffectAsset("860f24d17f3b52a7bf97bcae923726fa");
    NS::Gfx::EffectResourceDesc changed = asset->Resource();
    NS::Gfx::EffectScene cached(NS::OS::FileSystem::Combine(root, "Assets/Effects"));
    NS::Gfx::EffectScene fresh(NS::OS::FileSystem::Combine(root, "Assets/Effects"));
    ASSERT_TRUE(cached.Preload(changed));
    changed.seed ^= 0x13579BDFu;
    ASSERT_TRUE(cached.Preload(changed));
    ASSERT_TRUE(fresh.Preload(changed));
    NS::Gfx::EffectPlayDesc desc;
    desc.dynamicInputs[0] = 40.0f;
    desc.dynamicInputs[2] = 0.1f;
    ASSERT_TRUE(cached.Play(changed, desc).IsValid());
    ASSERT_TRUE(fresh.Play(changed, desc).IsValid());
    for (int frame = 0; frame < 12; ++frame)
    {
        cached.Update(1.0f / 60.0f);
        fresh.Update(1.0f / 60.0f);
        ASSERT_TRUE(CaptureEffectPixels(cached) == CaptureEffectPixels(fresh)) << frame;
    }
}

TEST(EffectLifecycle, RebuildingThePausedSceneClearsPixelsBeforeAnotherStep)
{
    NS::OS::Window window(NS::OS::WindowDesc{.title = "エフェクト終了の試し", .size = {32, 32}, .visible = false});
    NS::Gfx::D3DDevice device(NS::Gfx::D3DDeviceDesc{}, window);
    ASSERT_TRUE(device.IsValid());
    NS::AssetManager assets(NS::OS::FileSystem::ContentRoot());
    const NS::AssetManagerScope useAssets{assets};
    NS::Scene scene;
    scene.SetEffectRoot("Assets/Effects");
    scene.SetDevice(&device);
    NS::Gfx::EffectScene* effects = scene.GetEffectScene();
    ASSERT_NE(effects, nullptr);
    ASSERT_TRUE(effects->Preload("impact.core"));
    ASSERT_TRUE(effects->Preload("rebound.trail"));
    const nlohmann::json baseline = scene.ToJson();
    const NS::Gfx::EffectHandle core = effects->Play("impact.core");
    const NS::Gfx::EffectHandle trail = effects->Play("rebound.trail");
    effects->Update(1.0f / 60.0f);
    const int pixels = CountEffectPixels(*effects);
    ASSERT_GT(pixels, 0);

    scene.SetSimulationPaused(true);
    scene.OnUpdate();
    EXPECT_TRUE(effects->Exists(core));
    EXPECT_TRUE(effects->Exists(trail));
    EXPECT_EQ(CountEffectPixels(*effects), pixels);

    scene.SetSimulationEnabled(false);
    scene.ResetFromJson(baseline);
    EXPECT_FALSE(effects->Exists(core));
    EXPECT_FALSE(effects->Exists(trail));
    EXPECT_EQ(CountEffectPixels(*effects), 0);

    scene.SetSimulationEnabled(true);
    const NS::Gfx::EffectHandle fresh = effects->Play("impact.core");
    effects->Update(1.0f / 60.0f);
    EXPECT_TRUE(effects->Exists(fresh));
    EXPECT_GT(CountEffectPixels(*effects), 0);
    scene.OnShutdown();
    EXPECT_FALSE(effects->Exists(fresh));
    EXPECT_EQ(CountEffectPixels(*effects), 0);
}

// 書式の欄で読むビューの書式を変えられる。渡さなければ作った書式のまま
TEST(EffectDepth, TextureViewFormatIsUsedOnlyWhenGiven)
{
    ScopedWarpGpu gpu;
    if (!gpu.Valid())
    {
        GTEST_SKIP() << "WARP の描画装置を作れなかった";
    }
    NS::Gfx::D3DTextureDesc depthCopy{};
    depthCopy.width = 8;
    depthCopy.height = 4;
    depthCopy.format = DXGI_FORMAT_R24G8_TYPELESS;
    depthCopy.viewFormat = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    const std::unique_ptr<NS::Gfx::D3DTexture> copy = NS::Gfx::D3DTexture::Create(depthCopy);
    ASSERT_NE(copy->Srv(), nullptr);
    D3D11_SHADER_RESOURCE_VIEW_DESC view{};
    copy->Srv()->GetDesc(&view);
    EXPECT_EQ(view.Format, DXGI_FORMAT_R24_UNORM_X8_TYPELESS);

    NS::Gfx::D3DTextureDesc plain{};
    plain.width = 8;
    plain.height = 4;
    const std::unique_ptr<NS::Gfx::D3DTexture> color = NS::Gfx::D3DTexture::Create(plain);
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
    const std::unique_ptr<NS::Gfx::D3DTexture> game = MakeDepth(64, 32, DXGI_FORMAT_D24_UNORM_S8_UINT);
    const std::unique_ptr<NS::Gfx::D3DTexture> editor = MakeDepth(48, 48, DXGI_FORMAT_D24_UNORM_S8_UINT);
    ASSERT_NE(game->Dsv(), nullptr);
    ASSERT_NE(editor->Dsv(), nullptr);
    const NS::CameraData camera = MakeCamera();

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
    const std::unique_ptr<NS::Gfx::D3DTexture> good = MakeDepth(32, 32, DXGI_FORMAT_D24_UNORM_S8_UINT);
    const std::unique_ptr<NS::Gfx::D3DTexture> other = MakeDepth(32, 32, DXGI_FORMAT_D32_FLOAT);
    ASSERT_NE(other->Dsv(), nullptr);
    const NS::CameraData camera = MakeCamera();

    BindDepth(gpu, *good);
    effects.Draw(camera);
    ASSERT_TRUE(effects.PassesDepth());
    BindDepth(gpu, *other);
    const TestLogCapture log;
    effects.Draw(camera);
    EXPECT_EQ(log.ErrorCount(), 1);
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
