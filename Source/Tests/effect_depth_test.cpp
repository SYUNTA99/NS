#include "NSlib/Core/CameraData.h"
#include "NSlib/Core/Types.h"
#include "NSlib/Core/Windows/Filesystem.h"
#include "NSlib/Core/Windows/Window.h"
#include "NSlib/CoreObject/Asset/AssetManager.h"
#include "NSlib/CoreObject/Asset/Effect.h"
#include "NSlib/CoreObject/Asset/EffectCatalog.h"
#include "NSlib/CoreObject/ClassObjs/EffectObj.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/Gfx/D3DDevice.h"
#include "NSlib/Gfx/D3DGlobal.h"
#include "NSlib/Gfx/D3DTexture.h"
#include "NSlib/Gfx/Render/EffectScene.h"
#include "TestEffectFiles.h"
#include "TestLogCapture.h"
#include "Game/PlayerActor.h"
#include "NSlib/CoreObject/IGet/IGetEffect.h"

#include <gtest/gtest.h>

#include <d3d11.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
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

    void ExpectSameEffectPixels(NS::Gfx::EffectScene& expectedScene, NS::Gfx::EffectScene& actualScene)
    {
        const std::vector<NS::uint32> expected = CaptureEffectPixels(expectedScene);
        const std::vector<NS::uint32> actual = CaptureEffectPixels(actualScene);
        ASSERT_FALSE(expected.empty());
        ASSERT_EQ(expected.size(), actual.size());
        unsigned int largestDifference = 0;
        for (std::size_t pixel = 0; pixel < expected.size(); ++pixel)
        {
            for (unsigned int shift = 0; shift < 32; shift += 8)
            {
                const int a = static_cast<int>((expected[pixel] >> shift) & 255u);
                const int b = static_cast<int>((actual[pixel] >> shift) & 255u);
                largestDifference = std::max(largestDifference, static_cast<unsigned int>(std::abs(a - b)));
            }
        }
        EXPECT_LE(largestDifference, 1u);
    }
} // namespace

TEST(EffectPlayback, NonFiniteInitialInputsDoNotCreateAPlayback)
{
    const ScopedWarpGpu gpu;
    ASSERT_TRUE(gpu.Valid());
    const std::string root = NS::OS::FileSystem::ContentRoot();
    NS::Gfx::EffectScene effects;
    const NS::Gfx::EffectResourceDesc resource{
        "playback-input-test", "impact.core", NS::OS::FileSystem::Combine(root, "Assets/Effects/impact.core.efkefc"), 1};
    ASSERT_TRUE(effects.Preload(resource));
    const float invalidValues[]{std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()};
    for (const float invalid : invalidValues)
    {
        for (std::size_t index = 0; index < 4; ++index)
        {
            NS::Gfx::EffectPlayDesc desc;
            desc.dynamicInputs[index] = invalid;
            EXPECT_FALSE(effects.Play(resource, desc).IsValid()) << index;
        }
    }
}

TEST(EffectAssetsRender, CoreMaterialKeepsTheHeldLightAndItsRelease)
{
    const ScopedWarpGpu gpu;
    ASSERT_TRUE(gpu.Valid());
    const std::string root = NS::OS::FileSystem::ContentRoot();
    NS::Gfx::EffectScene legacy;
    NS::Gfx::EffectScene material;
    const NS::Gfx::EffectResourceDesc before{
        "legacy-core", "legacy", NS::OS::FileSystem::Combine(root, "Source/Tests/data/effects/impact_core_legacy.efkefc"), 123};
    const NS::Gfx::EffectResourceDesc after{
        "material-core", "material", NS::OS::FileSystem::Combine(root, "Assets/Effects/impact.core.efkefc"), 123};
    ASSERT_TRUE(legacy.Preload(before));
    ASSERT_TRUE(material.Preload(after));
    struct HeldLight
    {
        float diameter;
        bool wide;
        int holdFrames;
        float power;
    };
    const HeldLight cases[]{{0.3f, false, 1, 0.0f}, {0.5f, false, 4, 1.0f}, {0.7f, false, 10, 2.0f},
                           {0.3f, true, 0, 0.0f}, {0.5f, true, 1, 1.0f}, {0.7f, true, 7, 2.0f}};
    for (const HeldLight& light : cases)
    {
        SCOPED_TRACE(::testing::Message() << light.diameter << "/" << light.wide << "/" << light.holdFrames);
        NS::Gfx::EffectPlayDesc oldDesc;
        oldDesc.scale = NS::Vector3::One * (light.diameter * 0.85f);
        oldDesc.dynamicInputs[0] = 0.0f;
        oldDesc.dynamicInputs[1] = 0.0f;
        oldDesc.dynamicInputs[2] = 0.0f;
        int tier = 0;
        if (light.wide)
        {
            tier = 2;
        }
        oldDesc.dynamicInputs[tier] = 1.0f;
        oldDesc.dynamicInputs[3] = 0.5f / 0.85f;
        NS::Gfx::EffectPlayDesc newDesc = oldDesc;
        newDesc.scale = NS::Vector3::One;
        newDesc.dynamicInputs[3] = light.power;
        const NS::Gfx::EffectHandle oldHandle = legacy.Play(before, oldDesc);
        const NS::Gfx::EffectHandle newHandle = material.Play(after, newDesc);
        ASSERT_TRUE(oldHandle.IsValid());
        ASSERT_TRUE(newHandle.IsValid());
        for (int frame = 0; frame < 16; ++frame)
        {
            SCOPED_TRACE(frame);
            if (frame > 0 && frame <= light.holdFrames)
            {
                float diameter = light.diameter;
                if (light.wide)
                {
                    diameter *= std::sqrt(0.74f + (1.0f - 0.74f) * std::pow(0.4f, static_cast<float>(frame - 1)));
                }
                else if (frame % 2 == 0)
                {
                    diameter *= 0.85f;
                }
                legacy.SetTransform(oldHandle, NS::Vector3::Zero, NS::Quaternion::Identity, NS::Vector3::One * diameter);
                if (frame == 1)
                {
                    legacy.SetDynamicInput(oldHandle, 3, 1.0f);
                }
            }
            if (light.wide && frame == light.holdFrames + 1)
            {
                legacy.Stop(oldHandle);
                material.Stop(newHandle);
            }
            else if (!light.wide && frame == light.holdFrames)
            {
                legacy.StopRoot(oldHandle);
                material.StopRoot(newHandle);
            }
            legacy.Update(1.0f / 60.0f);
            material.Update(1.0f / 60.0f);
            EXPECT_EQ(legacy.Exists(oldHandle), material.Exists(newHandle));
            ExpectSameEffectPixels(legacy, material);
        }
        legacy.StopAll();
        material.StopAll();
        legacy.Update(0.0f);
        material.Update(0.0f);
    }
}

TEST(EffectAssetsRender, HitLayerMaterialsKeepTheirCurvesAcrossDifferentHoldLengths)
{
    const ScopedWarpGpu gpu;
    ASSERT_TRUE(gpu.Valid());
    const std::string root = NS::OS::FileSystem::ContentRoot();
    for (const std::string name : {"streak", "ring", "glow"})
    {
        SCOPED_TRACE(name);
        NS::Gfx::EffectScene legacy;
        NS::Gfx::EffectScene material;
        const NS::Gfx::EffectResourceDesc before{
            "legacy-" + name, name,
            NS::OS::FileSystem::Combine(root, "Source/Tests/data/effects/impact_" + name + "_legacy.efkefc"), 123};
        const NS::Gfx::EffectResourceDesc after{
            "material-" + name, name, NS::OS::FileSystem::Combine(root, "Assets/Effects/impact." + name + ".efkefc"), 123};
        ASSERT_TRUE(legacy.Preload(before));
        ASSERT_TRUE(material.Preload(after));
        for (const int hold : {1, 4, 10})
        {
            for (const float power : {0.0f, 1.0f, 2.0f})
            {
                SCOPED_TRACE(::testing::Message() << hold << "/" << power);
                NS::Gfx::EffectPlayDesc oldDesc;
                float size = 4.0f + 2.0f * power;
                oldDesc.scale = NS::Vector3{size, 1.0f, 1.0f};
                if (name == "glow")
                {
                    size = 2.0f + 1.6f * power;
                    oldDesc.scale = NS::Vector3::One * (size * std::sqrt(0.9f));
                    oldDesc.rotation = NS::TurnNormalTo(NS::Vector3{0.0f, 1.0f, 0.0f});
                }
                else if (name == "ring")
                {
                    size = 0.5f + 0.6f * power;
                    oldDesc.scale = NS::Vector3::One * 0.3f;
                }
                NS::Gfx::EffectPlayDesc newDesc = oldDesc;
                newDesc.scale = NS::Vector3::One;
                newDesc.dynamicInputs[0] = static_cast<float>(hold);
                newDesc.dynamicInputs[1] = power;
                if (name == "ring")
                {
                    newDesc.dynamicInputs[0] = power;
                }
                const NS::Gfx::EffectHandle oldHandle = legacy.Play(before, oldDesc);
                const NS::Gfx::EffectHandle newHandle = material.Play(after, newDesc);
                ASSERT_TRUE(oldHandle.IsValid());
                ASSERT_TRUE(newHandle.IsValid());
                for (int age = 0; age < hold + 9; ++age)
                {
                    SCOPED_TRACE(age);
                    if (name == "streak")
                    {
                        const int frame = age + 1;
                        if (frame >= hold + 6)
                        {
                            legacy.Stop(oldHandle);
                            material.Stop(newHandle);
                        }
                        else if (frame >= hold + 3)
                        {
                            legacy.StopRoot(oldHandle);
                            material.StopRoot(newHandle);
                        }
                        else
                        {
                            float length = size;
                            if (frame <= hold && frame % 2 == 0)
                            {
                                length *= 0.85f;
                            }
                            const float t = static_cast<float>(std::min(frame, hold + 1) - 1) / static_cast<float>(hold);
                            legacy.SetTransform(oldHandle, oldDesc.position, oldDesc.rotation, {length, 1.0f + (0.3f - 1.0f) * t, 1.0f});
                        }
                    }
                    else if (name == "glow")
                    {
                        if (age >= 1 && age <= hold)
                        {
                            float area = 0.9f;
                            if (age == 3)
                            {
                                area = 1.0f;
                            }
                            else if (age > 3)
                            {
                                area = static_cast<float>(hold + 2 - age) / static_cast<float>(hold - 1);
                            }
                            legacy.SetTransform(oldHandle, oldDesc.position, oldDesc.rotation, NS::Vector3::One * (size * std::sqrt(area)));
                        }
                        if (age == hold)
                        {
                            legacy.StopRoot(oldHandle);
                            material.StopRoot(newHandle);
                        }
                    }
                    else if (age >= 7)
                    {
                        legacy.Stop(oldHandle);
                        material.Stop(newHandle);
                    }
                    else
                    {
                        const float radius = 0.3f + (size - 0.3f) * NS::EaseOutCubic(static_cast<float>(age) / 6.0f);
                        legacy.SetTransform(oldHandle, oldDesc.position, oldDesc.rotation, NS::Vector3::One * radius);
                    }
                    legacy.Update(1.0f / 60.0f);
                    material.Update(1.0f / 60.0f);
                    EXPECT_EQ(legacy.Exists(oldHandle), material.Exists(newHandle));
                    ExpectSameEffectPixels(legacy, material);
                }
                legacy.StopAll();
                material.StopAll();
                legacy.Update(0.0f);
                material.Update(0.0f);
            }
        }
    }
}

TEST(EffectPlayback, NonFiniteChangesKeepTheCurrentInput)
{
    const ScopedWarpGpu gpu;
    ASSERT_TRUE(gpu.Valid());
    const std::string root = NS::OS::FileSystem::ContentRoot();
    NS::Gfx::EffectScene expected;
    NS::Gfx::EffectScene actual;
    const NS::Gfx::EffectResourceDesc resource{
        "runtime-input-test", "core", NS::OS::FileSystem::Combine(root, "Assets/Effects/impact.core.efkefc"), 123};
    ASSERT_TRUE(expected.Preload(resource));
    ASSERT_TRUE(actual.Preload(resource));
    const NS::Gfx::EffectHandle expectedHandle = expected.Play(resource, {});
    const NS::Gfx::EffectHandle actualHandle = actual.Play(resource, {});
    ASSERT_TRUE(expectedHandle.IsValid());
    ASSERT_TRUE(actualHandle.IsValid());
    actual.SetDynamicInput(actualHandle, 0, std::numeric_limits<float>::quiet_NaN());
    actual.SetDynamicInput(actualHandle, 0, std::numeric_limits<float>::infinity());
    expected.Update(1.0f / 60.0f);
    actual.Update(1.0f / 60.0f);
    const std::vector<NS::uint32> pixels = CaptureEffectPixels(expected);
    ASSERT_TRUE(std::any_of(pixels.begin(), pixels.end(), [](NS::uint32 pixel) { return (pixel & 0x00FFFFFFu) != 0; }));
    EXPECT_EQ(CaptureEffectPixels(actual), pixels);
}

TEST(EffectPlayback, SceneAdvancesTheMaterialOncePerWorldStepDuringActorHitStop)
{
    NS::OS::Window window(NS::OS::WindowDesc{.title = "停止中の素材の時計", .size = {32, 32}, .visible = false});
    NS::Gfx::D3DDevice device(NS::Gfx::D3DDeviceDesc{}, window);
    ASSERT_TRUE(device.IsValid());
    const std::string root = NS::OS::FileSystem::ContentRoot();
    NS::AssetManager assets(root);
    const NS::AssetManagerScope useAssets{assets};
    NS::Scene scene;
    scene.SetDevice(&device);
    PlayerActor* player = scene.SpawnActor<PlayerActor>("PlayerActor");
    ASSERT_NE(player, nullptr);
    scene.OnUpdate();
    const NS::Effect* asset = assets.LoadEffectAsset("6e4f2fccb9df5a339885b53d42fafd45");
    ASSERT_TRUE(asset->IsValid());
    NS::PreloadEffects(player, {asset});
    NS::Gfx::EffectScene expected;
    ASSERT_TRUE(expected.Preload(asset->Resource()));
    expected.Update(1.0f / 60.0f);
    const NS::uint32 id = NS::PlayEffect(player, asset, {});
    ASSERT_NE(id, 0u);
    ASSERT_TRUE(player->GetEffect()->Find(id)->handle.IsValid());
    ASSERT_TRUE(expected.Play(asset->Resource(), {}).IsValid());
    player->RequestHitStop(10);
    const NS::Vector3 position = player->Root().Position();
    for (int frame = 0; frame < 30; ++frame)
    {
        SCOPED_TRACE(frame);
        if (frame == 12)
        {
            scene.SetWorldSpeed(0.5f);
        }
        const int previousStep = player->GetEffect()->Step();
        scene.OnUpdate();
        if (player->GetEffect()->Step() != previousStep)
        {
            expected.Update(1.0f / 60.0f);
        }
        if (frame < 10)
        {
            EXPECT_EQ(player->Root().Position(), position);
        }
        ExpectSameEffectPixels(expected, *scene.GetEffectScene());
    }
}

TEST(EffectAssetsRender, ParticleMaterialsDeriveTheirCountsAndMotionFromGameInputs)
{
    const ScopedWarpGpu gpu;
    ASSERT_TRUE(gpu.Valid());
    const std::string root = NS::OS::FileSystem::ContentRoot();
    for (const std::string name : {"sparks", "embers", "recoil"})
    {
        SCOPED_TRACE(name);
        NS::Gfx::EffectScene legacy;
        NS::Gfx::EffectScene material;
        const NS::Gfx::EffectResourceDesc before{
            "legacy-" + name, name, NS::OS::FileSystem::Combine(root, "Source/Tests/data/effects/impact_" + name + "_legacy.efkefc"), 123};
        const NS::Gfx::EffectResourceDesc after{
            "material-" + name, name, NS::OS::FileSystem::Combine(root, "Assets/Effects/impact." + name + ".efkefc"), 123};
        ASSERT_TRUE(legacy.Preload(before));
        ASSERT_TRUE(material.Preload(after));
        for (const float power : {0.7f, 1.0f, 2.0f})
        {
            for (const float motion : {0.0f, 1.0f, 3.0f})
            {
                for (const bool wide : {false, true})
                {
                    if (wide && name != "sparks")
                    {
                        continue;
                    }
                    SCOPED_TRACE(::testing::Message() << power << "/" << motion << "/" << wide);
                    const float share = std::clamp((power - 0.7f) / (2.0f - 0.7f), 0.0f, 1.0f);
                    const int count = static_cast<int>(std::lround(10.0f + 20.0f * share));
                    NS::Gfx::EffectPlayDesc oldDesc;
                    oldDesc.rotation = NS::TurnUpTo(NS::Vector3{1.0f, 0.0f, 1.0f});
                    NS::Gfx::EffectPlayDesc newDesc = oldDesc;
                    newDesc.dynamicInputs[0] = power;
                    newDesc.dynamicInputs[1] = motion;
                    if (name == "sparks")
                    {
                        oldDesc.dynamicInputs[0] = static_cast<float>(count);
                        oldDesc.dynamicInputs[1] = 0.0f;
                        oldDesc.dynamicInputs[2] = (6.0f + 3.0f * motion) / 60.0f;
                        oldDesc.dynamicInputs[3] = 0.0f;
                        newDesc.dynamicInputs[2] = 0.0f;
                        if (wide)
                        {
                            oldDesc.scale = NS::Vector3::One * 3.0f;
                            oldDesc.dynamicInputs[0] = 0.0f;
                            oldDesc.dynamicInputs[1] = 16.0f;
                            oldDesc.dynamicInputs[2] = 4.0f / 60.0f;
                            newDesc.dynamicInputs[2] = 1.0f;
                        }
                    }
                    else if (name == "embers")
                    {
                        oldDesc.dynamicInputs[0] = static_cast<float>(count * 7);
                        oldDesc.dynamicInputs[1] = (6.0f + 3.0f * motion) / 60.0f;
                    }
                    else
                    {
                        oldDesc.dynamicInputs[0] = 8.0f;
                        oldDesc.dynamicInputs[1] = 0.6f + 0.5f * motion;
                        oldDesc.dynamicInputs[2] = 0.0f;
                        oldDesc.dynamicInputs[3] = 0.0f;
                        newDesc.dynamicInputs[0] = motion;
                        newDesc.dynamicInputs[1].reset();
                    }
                    const NS::Gfx::EffectHandle oldHandle = legacy.Play(before, oldDesc);
                    const NS::Gfx::EffectHandle newHandle = material.Play(after, newDesc);
                    ASSERT_TRUE(oldHandle.IsValid());
                    ASSERT_TRUE(newHandle.IsValid());
                    bool visible = false;
                    for (int frame = 0; frame < 24; ++frame)
                    {
                        SCOPED_TRACE(frame);
                        legacy.Update(1.0f / 60.0f);
                        material.Update(1.0f / 60.0f);
                        EXPECT_EQ(legacy.Exists(oldHandle), material.Exists(newHandle));
                        visible |= CountEffectPixels(legacy) > 0;
                        ExpectSameEffectPixels(legacy, material);
                    }
                    EXPECT_TRUE(visible);
                    legacy.StopAll();
                    material.StopAll();
                    legacy.Update(0.0f);
                    material.Update(0.0f);
                }
            }
        }
    }
}

TEST(EffectAssetsRender, DustMaterialsMapGameInputsToTheOriginalSizeAndLifetime)
{
    const ScopedWarpGpu gpu;
    ASSERT_TRUE(gpu.Valid());
    const std::string root = NS::OS::FileSystem::ContentRoot();
    for (const std::string name : {"impact.dust", "land.dust", "launch.landDust"})
    {
        NS::Gfx::EffectScene reference;
        NS::Gfx::EffectScene material;
        const NS::Gfx::EffectResourceDesc before{
            "reference-" + name, name, NS::OS::FileSystem::Combine(root, "Source/Tests/data/effects/" + name + "_reference.efkefc"), 123};
        const NS::Gfx::EffectResourceDesc after{
            "material-" + name, name, NS::OS::FileSystem::Combine(root, "Assets/Effects/" + name + ".efkefc"), 123};
        ASSERT_TRUE(reference.Preload(before));
        ASSERT_TRUE(material.Preload(after));
        for (const float strength : {0.0f, 1.0f, 2.0f})
        {
            for (const float mass : {0.0f, 1.0f, 4.0f, 9.0f})
            {
                SCOPED_TRACE(::testing::Message() << name << "/" << strength << "/" << mass);
                NS::Gfx::EffectPlayDesc oldDesc;
                NS::Gfx::EffectPlayDesc newDesc;
                newDesc.dynamicInputs[0] = strength;
                newDesc.dynamicInputs[1] = std::sqrt(mass);
                float size = (0.8f + 0.3f * std::sqrt(mass)) * std::max(0.0f, 1.0f + 0.5f * (strength - 1.0f));
                if (name == "impact.dust")
                {
                    oldDesc.dynamicInputs[0] = static_cast<float>(4 + std::lround(std::min(mass, 4.0f)));
                    oldDesc.position = NS::Vector3{0.6f, 0.0f, 0.8f} * (size * 0.7f);
                    newDesc.dynamicInputs[2] = 0.6f;
                    newDesc.dynamicInputs[3] = 0.8f;
                }
                else if (name == "land.dust")
                {
                    const float fallSpeed = strength * 20.0f;
                    size = (1.2f + 0.04f * fallSpeed) / 1.2f;
                    newDesc.dynamicInputs[0] = fallSpeed;
                }
                else
                {
                    size = (0.8f + 0.4f * std::sqrt(mass)) * std::max(0.0f, 1.0f + 0.5f * (strength - 1.0f)) / 1.2f;
                    oldDesc.rotation = NS::TurnUpTo(NS::NormalizedOr(NS::Vector3{0.0f, 1.0f, 0.5f}, NS::Vector3::UnitY));
                    newDesc.rotation = oldDesc.rotation;
                }
                oldDesc.scale = NS::Vector3::One * size;
                const NS::Gfx::EffectHandle a = reference.Play(before, oldDesc);
                const NS::Gfx::EffectHandle b = material.Play(after, newDesc);
                ASSERT_TRUE(a.IsValid());
                ASSERT_TRUE(b.IsValid());
                bool visible = false;
                for (int frame = 0; frame < 40; ++frame)
                {
                    SCOPED_TRACE(frame);
                    reference.Update(1.0f / 60.0f);
                    material.Update(1.0f / 60.0f);
                    EXPECT_EQ(reference.Exists(a), material.Exists(b));
                    visible |= CountEffectPixels(reference) > 0;
                    ExpectSameEffectPixels(reference, material);
                }
                EXPECT_TRUE(visible);
                EXPECT_FALSE(material.Exists(b));
                reference.StopAll();
                material.StopAll();
                reference.Update(0.0f);
                material.Update(0.0f);
            }
        }
    }
}

TEST(EffectAssetsRender, LaunchTrailMapsLaunchStrengthToPointLifetime)
{
    const ScopedWarpGpu gpu;
    ASSERT_TRUE(gpu.Valid());
    const std::string root = NS::OS::FileSystem::ContentRoot();
    NS::Gfx::EffectScene reference;
    NS::Gfx::EffectScene material;
    const NS::Gfx::EffectResourceDesc before{
        "legacy-launch", "launch", NS::OS::FileSystem::Combine(root, "Source/Tests/data/effects/launch_trail_legacy.efkefc"), 123};
    const NS::Gfx::EffectResourceDesc after{
        "material-launch", "launch", NS::OS::FileSystem::Combine(root, "Assets/Effects/launch.trail.efkefc"), 123};
    ASSERT_TRUE(reference.Preload(before));
    ASSERT_TRUE(material.Preload(after));
    for (const float strength : {0.0f, 0.13f, 1.0f, 3.0f})
    {
        for (const int tier : {0, 1})
        {
            SCOPED_TRACE(::testing::Message() << strength << "/" << tier);
            NS::Gfx::EffectPlayDesc oldDesc;
            oldDesc.dynamicInputs[0] = 0.0f;
            oldDesc.dynamicInputs[1] = 0.0f;
            oldDesc.dynamicInputs[tier] = 1.0f;
            oldDesc.dynamicInputs[2] = static_cast<float>(std::lround(8.0f + 4.0f * strength));
            NS::Gfx::EffectPlayDesc newDesc = oldDesc;
            newDesc.dynamicInputs[2] = strength;
            const NS::Gfx::EffectHandle a = reference.Play(before, oldDesc);
            const NS::Gfx::EffectHandle b = material.Play(after, newDesc);
            ASSERT_TRUE(a.IsValid());
            ASSERT_TRUE(b.IsValid());
            for (int frame = 0; frame < 40; ++frame)
            {
                SCOPED_TRACE(frame);
                const NS::Vector3 position{static_cast<float>(frame) * 0.03f, 0.0f, 0.0f};
                reference.SetTransform(a, position, NS::Quaternion::Identity, NS::Vector3::One);
                material.SetTransform(b, position, NS::Quaternion::Identity, NS::Vector3::One);
                if (frame == 12)
                {
                    reference.StopRoot(a);
                    material.StopRoot(b);
                }
                reference.Update(1.0f / 60.0f);
                material.Update(1.0f / 60.0f);
                EXPECT_EQ(reference.Exists(a), material.Exists(b));
                ExpectSameEffectPixels(reference, material);
            }
            reference.StopAll();
            material.StopAll();
            reference.Update(0.0f);
            material.Update(0.0f);
        }
    }
}

TEST(EffectAssetsRender, ReleaseBurstOwnsItsStrengthToSizeMapping)
{
    const ScopedWarpGpu gpu;
    ASSERT_TRUE(gpu.Valid());
    const std::string root = NS::OS::FileSystem::ContentRoot();
    NS::Gfx::EffectScene legacy;
    NS::Gfx::EffectScene material;
    const NS::Gfx::EffectResourceDesc before{
        "legacy-release", "release", NS::OS::FileSystem::Combine(root, "Source/Tests/data/effects/release_burst_legacy.efkefc"), 123};
    const NS::Gfx::EffectResourceDesc after{
        "material-release", "release", NS::OS::FileSystem::Combine(root, "Assets/Effects/release.burst.efkefc"), 123};
    ASSERT_TRUE(legacy.Preload(before));
    ASSERT_TRUE(material.Preload(after));
    for (const float strength : {0.0f, 0.5f, 1.0f})
    {
        SCOPED_TRACE(strength);
        NS::Gfx::EffectPlayDesc oldDesc;
        oldDesc.dynamicInputs[0] = strength;
        oldDesc.dynamicInputs[1] = 0.75f + 0.25f * strength;
        NS::Gfx::EffectPlayDesc newDesc = oldDesc;
        newDesc.dynamicInputs[1].reset();
        const NS::Gfx::EffectHandle oldHandle = legacy.Play(before, oldDesc);
        const NS::Gfx::EffectHandle newHandle = material.Play(after, newDesc);
        ASSERT_TRUE(oldHandle.IsValid());
        ASSERT_TRUE(newHandle.IsValid());
        for (int frame = 0; frame < 24; ++frame)
        {
            SCOPED_TRACE(frame);
            legacy.Update(1.0f / 60.0f);
            material.Update(1.0f / 60.0f);
            EXPECT_EQ(legacy.Exists(oldHandle), material.Exists(newHandle));
            ExpectSameEffectPixels(legacy, material);
        }
        legacy.StopAll();
        material.StopAll();
        legacy.Update(0.0f);
        material.Update(0.0f);
    }
}

TEST(EffectAssetsRender, DisplayNameChangesDoNotAlterAnyShippedFrame)
{
    NS::OS::Window window(NS::OS::WindowDesc{.title = "素材参照の描画比較", .size = {32, 32}, .visible = false});
    NS::Gfx::D3DDevice device(NS::Gfx::D3DDeviceDesc{}, window);
    ASSERT_TRUE(device.IsValid());
    const std::string root = NS::OS::FileSystem::ContentRoot();
    NS::AssetManager assets(root);
    NS::Gfx::EffectScene original;
    NS::Gfx::EffectScene migrated;
    int compared = 0;
    for (const NS::EffectAssetFile& file : NS::ReadEffectAssetFiles(root))
    {

        SCOPED_TRACE(file.path);
        const NS::Effect* asset = assets.LoadEffectAsset(file.id);
        ASSERT_TRUE(asset->IsValid());
        NS::Gfx::EffectResourceDesc renamed = asset->Resource();
        renamed.name = "Renamed effect";
        ASSERT_TRUE(original.Preload(asset->Resource()));
        ASSERT_TRUE(migrated.Preload(renamed));
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
        const NS::Gfx::EffectHandle originalHandle = original.Play(asset->Resource(), desc);
        const NS::Gfx::EffectHandle migratedHandle = migrated.Play(renamed, desc);
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
    NS::Gfx::EffectScene scene;
    NS::Effect* asset = assets.LoadEffectAsset("6e4f2fccb9df5a339885b53d42fafd45");
    ASSERT_TRUE(scene.Preload(asset->Resource()));
    asset->Invalidate();
    NS::EffectObj owner;
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
    const NS::Effect* asset = assets.LoadEffectAsset("860f24d17f3b52a7bf97bcae923726fa");
    NS::Gfx::EffectResourceDesc changed = asset->Resource();
    NS::Gfx::EffectScene cached;
    NS::Gfx::EffectScene fresh;
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
    scene.SetDevice(&device);
    NS::Gfx::EffectScene* effects = scene.GetEffectScene();
    ASSERT_NE(effects, nullptr);
    const std::string root = NS::OS::FileSystem::ContentRoot();
    const NS::Gfx::EffectResourceDesc coreResource{
        "core", "core", NS::OS::FileSystem::Combine(root, "Assets/Effects/impact.core.efkefc"), 1};
    const NS::Gfx::EffectResourceDesc trailResource{
        "trail", "trail", NS::OS::FileSystem::Combine(root, "Assets/Effects/rebound.trail.efkefc"), 1};
    ASSERT_TRUE(effects->Preload(coreResource));
    ASSERT_TRUE(effects->Preload(trailResource));
    const nlohmann::json baseline = scene.ToJson();
    const NS::Gfx::EffectHandle core = effects->Play(coreResource, {});
    const NS::Gfx::EffectHandle trail = effects->Play(trailResource, {});
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
    const NS::Gfx::EffectHandle fresh = effects->Play(coreResource, {});
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
    NS::Gfx::EffectScene effects;
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
    NS::Gfx::EffectScene effects;
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
TEST(EffectDepth, GroundGlowDoesNotFadeAgainstTheFloorDepth)
{
    // 床に寝かせた照りには入れない。全面が床と同じ面で丸ごと消える
    EXPECT_EQ(CountSoftNodesIn("Assets/Effects/impact.glow.efkefc"), 0);
}
