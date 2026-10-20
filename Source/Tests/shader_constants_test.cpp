#include "NSlib/Gfx/D3DShader.h"
#include "NSlib/Gfx/D3DSkeletalMesh.h"
#include "NSlib/Gfx/Render/Bloom.h"
#include "NSlib/Gfx/Render/DrawItem.h"
#include "NSlib/Gfx/Render/FrameConstants.h"
#include "NSlib/Gfx/Render/ScreenOverlay.h"
#include "NSlib/Gfx/Render/ScreenPasses.h"
#include "NSlib/Gfx/Render/Skybox.h"

#include <gtest/gtest.h>

#include <d3d11.h>
#include <d3d11shader.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <string>

// 定数バッファの場所と形は C++ が正。HLSL は実行中にコンパイルするので同じヘッダを読めない。
// 試しの中でシェーダーを建てて反射で cbuffer の場所と大きさを読み、C++ の場所の定数と型の大きさに照らす

namespace
{
    using Microsoft::WRL::ComPtr;

    struct ReflectedShader
    {
        ComPtr<ID3D11ShaderReflection> reflection;
        std::string error;
    };

    ReflectedShader Reflect(const wchar_t* path, const char* entry, const char* target)
    {
        ReflectedShader out;
        ComPtr<ID3DBlob> blob;
        ComPtr<ID3DBlob> errors;
        HRESULT hr = ::D3DCompileFromFile(path,
                                          nullptr,
                                          D3D_COMPILE_STANDARD_FILE_INCLUDE,
                                          entry,
                                          target,
                                          0,
                                          0,
                                          blob.GetAddressOf(),
                                          errors.GetAddressOf());
        if (FAILED(hr))
        {
            if (errors != nullptr)
            {
                out.error.assign(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize());
            }
            else
            {
                out.error = "コンパイルに失敗した";
            }
            return out;
        }
        hr = ::D3DReflect(blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(out.reflection.GetAddressOf()));
        if (FAILED(hr))
        {
            out.reflection.Reset();
            out.error = "バイトコードを反射で読めない";
        }
        return out;
    }

    // 名前の cbuffer が、場所 slot にあって大きさが size か。シェーダーが読まない cbuffer は反射に出ないので、無ければ失敗にする
    ::testing::AssertionResult HasConstantBuffer(ID3D11ShaderReflection& reflection,
                                                 const char* name,
                                                 NS::uint slot,
                                                 std::size_t size)
    {
        D3D11_SHADER_INPUT_BIND_DESC bind{};
        if (FAILED(reflection.GetResourceBindingDescByName(name, &bind)))
        {
            return ::testing::AssertionFailure() << name << " がシェーダーの資源に無い";
        }
        if (bind.Type != D3D_SIT_CBUFFER)
        {
            return ::testing::AssertionFailure() << name << " が cbuffer でない";
        }
        D3D11_SHADER_BUFFER_DESC buffer{};
        if (FAILED(reflection.GetConstantBufferByName(name)->GetDesc(&buffer)))
        {
            return ::testing::AssertionFailure() << name << " の記述を読めない";
        }
        if (bind.BindPoint != slot || buffer.Size != size)
        {
            return ::testing::AssertionFailure() << name << " は b" << bind.BindPoint << " で " << buffer.Size
                                                 << " バイト。C++ は b" << slot << " で " << size << " バイト";
        }
        return ::testing::AssertionSuccess();
    }

    // 材質の値の cbuffer は持たないシェーダーもある。持つ時だけ場所を照らす
    ::testing::AssertionResult MaterialParamsAtItsSlot(ID3D11ShaderReflection& reflection)
    {
        D3D11_SHADER_INPUT_BIND_DESC bind{};
        if (FAILED(reflection.GetResourceBindingDescByName("MaterialParams", &bind)))
        {
            return ::testing::AssertionSuccess();
        }
        if (bind.BindPoint != NS::Gfx::k_MaterialParamsSlot)
        {
            return ::testing::AssertionFailure() << "MaterialParams は b" << bind.BindPoint << "。C++ は b"
                                                 << NS::Gfx::k_MaterialParamsSlot;
        }
        return ::testing::AssertionSuccess();
    }
} // namespace

TEST(ShaderConstants, StandardVertexShaderMatchesTheDrawConstants)
{
    const ReflectedShader shader = Reflect(L"Shaders/standard.vs.hlsl", "VSMain", "vs_5_0");
    ASSERT_NE(shader.reflection, nullptr) << shader.error;
    EXPECT_TRUE(
        HasConstantBuffer(*shader.reflection.Get(), "FrameCB", NS::Gfx::k_DrawConstantsSlot, sizeof(NS::Gfx::FrameCB)));
    EXPECT_TRUE(MaterialParamsAtItsSlot(*shader.reflection.Get()));
}

TEST(ShaderConstants, SkinnedVertexShaderMatchesTheDrawConstantsAndTheBones)
{
    const ReflectedShader shader = Reflect(L"Shaders/skinned.vs.hlsl", "VSMain", "vs_5_0");
    ASSERT_NE(shader.reflection, nullptr) << shader.error;
    EXPECT_TRUE(
        HasConstantBuffer(*shader.reflection.Get(), "FrameCB", NS::Gfx::k_DrawConstantsSlot, sizeof(NS::Gfx::FrameCB)));
    EXPECT_TRUE(HasConstantBuffer(
        *shader.reflection.Get(), "BonePalette", NS::Gfx::k_BonePaletteSlot, sizeof(NS::Gfx::BonePaletteCB)));
    EXPECT_TRUE(MaterialParamsAtItsSlot(*shader.reflection.Get()));
}

// 頂点シェーダーへ足す定数を差す場所の既定は骨の場所。骨の場所を変えた時に一緒に動く
TEST(ShaderConstants, DrawItemExtraVertexConstantsDefaultToTheBoneSlot)
{
    EXPECT_EQ(NS::Gfx::DrawItem{}.extraVsSlot, NS::Gfx::k_BonePaletteSlot);
}

TEST(ShaderConstants, BloomPixelShadersMatchTheBloomConstants)
{
    const wchar_t* const paths[] = {L"Shaders/bloom_threshold.ps.hlsl",
                                    L"Shaders/bloom_down.ps.hlsl",
                                    L"Shaders/bloom_up.ps.hlsl",
                                    L"Shaders/bloom_composite.ps.hlsl"};
    for (const wchar_t* path : paths)
    {
        const ReflectedShader shader = Reflect(path, "PSMain", "ps_5_0");
        ASSERT_NE(shader.reflection, nullptr) << shader.error;
        EXPECT_TRUE(
            HasConstantBuffer(*shader.reflection.Get(), "BloomCB", NS::Gfx::k_DrawConstantsSlot, sizeof(NS::Gfx::BloomCB)));
    }
}

TEST(ShaderConstants, FadePixelShaderMatchesTheFullscreenColorConstants)
{
    const ReflectedShader shader = Reflect(L"Shaders/fade.ps.hlsl", "PSMain", "ps_5_0");
    ASSERT_NE(shader.reflection, nullptr) << shader.error;
    EXPECT_TRUE(HasConstantBuffer(
        *shader.reflection.Get(), "FadeCB", NS::Gfx::k_DrawConstantsSlot, sizeof(NS::Gfx::FullscreenColorCB)));
}

TEST(ShaderConstants, ScreenRectShadersMatchTheScreenRectConstants)
{
    const ReflectedShader vs = Reflect(L"Shaders/ui_rect.vs.hlsl", "VSMain", "vs_5_0");
    ASSERT_NE(vs.reflection, nullptr) << vs.error;
    EXPECT_TRUE(
        HasConstantBuffer(*vs.reflection.Get(), "UiRectCB", NS::Gfx::k_DrawConstantsSlot, sizeof(NS::Gfx::ScreenRectCB)));

    const ReflectedShader ps = Reflect(L"Shaders/ui_rect.ps.hlsl", "PSMain", "ps_5_0");
    ASSERT_NE(ps.reflection, nullptr) << ps.error;
    EXPECT_TRUE(
        HasConstantBuffer(*ps.reflection.Get(), "UiRectCB", NS::Gfx::k_DrawConstantsSlot, sizeof(NS::Gfx::ScreenRectCB)));
}

TEST(ShaderConstants, SkyboxVertexShaderMatchesTheSkyboxConstants)
{
    const ReflectedShader shader = Reflect(L"Shaders/skybox.vs.hlsl", "VSMain", "vs_5_0");
    ASSERT_NE(shader.reflection, nullptr) << shader.error;
    EXPECT_TRUE(
        HasConstantBuffer(*shader.reflection.Get(), "SkyboxCB", NS::Gfx::k_DrawConstantsSlot, sizeof(NS::Gfx::SkyboxCB)));
}

TEST(ShaderConstants, ScreenDistortionPixelShaderMatchesTheDistortionConstants)
{
    const ReflectedShader shader = Reflect(L"Shaders/screen_distortion.ps.hlsl", "PSMain", "ps_5_0");
    ASSERT_NE(shader.reflection, nullptr) << shader.error;
    EXPECT_TRUE(HasConstantBuffer(*shader.reflection.Get(),
                                  "ScreenDistortionCB",
                                  NS::Gfx::k_DrawConstantsSlot,
                                  sizeof(NS::Gfx::ScreenDistortionCB)));
}

// 開発用の線は定数の型を持たず、ビュー射影の行列 1 つをそのまま差す
TEST(ShaderConstants, DebugLineVertexShaderMatchesTheViewProjection)
{
    const ReflectedShader shader = Reflect(L"Shaders/debug_line.vs.hlsl", "VSMain", "vs_5_0");
    ASSERT_NE(shader.reflection, nullptr) << shader.error;
    EXPECT_TRUE(HasConstantBuffer(*shader.reflection.Get(), "DebugCB", NS::Gfx::k_DrawConstantsSlot, sizeof(NS::Matrix)));
}
