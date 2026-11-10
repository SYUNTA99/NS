#include "NSlib/Gfx/D3DShader.h"
#include "NSlib/Gfx/D3DSkeletalMesh.h"
#include "NSlib/Gfx/Render/Bloom.h"
#include "NSlib/Gfx/Render/DrawItem.h"
#include "NSlib/Gfx/Render/DrawConstants.h"
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

    // ビューごとの定数 ViewCB と物ごとの定数 ObjectCB が、どちらも C++ の場所と大きさにあるか
    ::testing::AssertionResult HasDrawConstants(ID3D11ShaderReflection& reflection)
    {
        const ::testing::AssertionResult view =
            HasConstantBuffer(reflection, "ViewCB", NS::Gfx::k_ViewConstantsSlot, sizeof(NS::Gfx::ViewConstants));
        if (!view)
        {
            return view;
        }
        return HasConstantBuffer(reflection, "ObjectCB", NS::Gfx::k_DrawConstantsSlot, sizeof(NS::Gfx::ObjectConstants));
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
    EXPECT_TRUE(HasDrawConstants(*shader.reflection.Get()));
    EXPECT_TRUE(MaterialParamsAtItsSlot(*shader.reflection.Get()));
}

TEST(ShaderConstants, WaterShadersMatchTheDrawConstantsAndExposeTheViewAndTime)
{
    const ReflectedShader vertex = Reflect(L"Shaders/water.vs.hlsl", "VSMain", "vs_5_0");
    const ReflectedShader pixel = Reflect(L"Shaders/water.ps.hlsl", "PSMain", "ps_5_0");
    ASSERT_NE(pixel.reflection, nullptr) << pixel.error;
    D3D11_SHADER_INPUT_BIND_DESC depthBind{};
    D3D11_SHADER_INPUT_BIND_DESC skyBind{};
    D3D11_SHADER_INPUT_BIND_DESC samplerBind{};
    ASSERT_TRUE(SUCCEEDED(pixel.reflection->GetResourceBindingDescByName("g_sceneDepth", &depthBind)));
    ASSERT_TRUE(SUCCEEDED(pixel.reflection->GetResourceBindingDescByName("g_skyCube", &skyBind)));
    ASSERT_TRUE(SUCCEEDED(pixel.reflection->GetResourceBindingDescByName("g_skySampler", &samplerBind)));
    EXPECT_EQ(depthBind.BindPoint, NS::Gfx::k_SceneDepthSlot);
    EXPECT_EQ(skyBind.BindPoint, NS::Gfx::k_SkyCubeSlot);
    EXPECT_EQ(samplerBind.BindPoint, NS::Gfx::k_SkySamplerSlot);
    ASSERT_NE(vertex.reflection, nullptr) << vertex.error;
    ASSERT_NE(pixel.reflection, nullptr) << pixel.error;
    EXPECT_TRUE(HasDrawConstants(*vertex.reflection.Get()));
    // 水の色は物ごとの値を読まないので、ピクセルシェーダーにはビューごとの定数だけがある
    EXPECT_TRUE(HasConstantBuffer(*pixel.reflection.Get(), "ViewCB", NS::Gfx::k_ViewConstantsSlot, sizeof(NS::Gfx::ViewConstants)));
    EXPECT_TRUE(MaterialParamsAtItsSlot(*vertex.reflection.Get()));
    EXPECT_TRUE(MaterialParamsAtItsSlot(*pixel.reflection.Get()));
    ID3D11ShaderReflectionConstantBuffer* constants = pixel.reflection->GetConstantBufferByName("ViewCB");
    D3D11_SHADER_VARIABLE_DESC camera{};
    D3D11_SHADER_VARIABLE_DESC time{};
    ASSERT_TRUE(SUCCEEDED(constants->GetVariableByName("g_cameraPosition")->GetDesc(&camera)));
    ASSERT_TRUE(SUCCEEDED(constants->GetVariableByName("g_sceneTimeSeconds")->GetDesc(&time)));
    EXPECT_EQ(camera.StartOffset, offsetof(NS::Gfx::ViewConstants, cameraPosition));
    EXPECT_EQ(time.StartOffset, offsetof(NS::Gfx::ViewConstants, sceneTimeSeconds));
}

// 物を描くピクセルシェーダーも、ビューごとと物ごとの 2 つの定数を C++ と同じ場所と大きさで読む
TEST(ShaderConstants, SurfacePixelShadersMatchTheViewAndObjectConstants)
{
    for (const wchar_t* path : {L"Shaders/lit.ps.hlsl", L"Shaders/unlit.ps.hlsl", L"Shaders/soft_circle.ps.hlsl"})
    {
        const ReflectedShader shader = Reflect(path, "PSMain", "ps_5_0");
        ASSERT_NE(shader.reflection, nullptr) << shader.error;
        EXPECT_TRUE(HasDrawConstants(*shader.reflection.Get()));
        EXPECT_TRUE(MaterialParamsAtItsSlot(*shader.reflection.Get()));
    }
}

TEST(ShaderConstants, SkinnedVertexShaderMatchesTheDrawConstantsAndTheBones)
{
    const ReflectedShader shader = Reflect(L"Shaders/skinned.vs.hlsl", "VSMain", "vs_5_0");
    ASSERT_NE(shader.reflection, nullptr) << shader.error;
    EXPECT_TRUE(HasDrawConstants(*shader.reflection.Get()));
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

TEST(ShaderConstants, ScreenLineShadersMatchTheScreenLineConstants)
{
    const ReflectedShader vs = Reflect(L"Shaders/ui_line.vs.hlsl", "VSMain", "vs_5_0");
    ASSERT_NE(vs.reflection, nullptr) << vs.error;
    EXPECT_TRUE(
        HasConstantBuffer(*vs.reflection.Get(), "UiLineCB", NS::Gfx::k_DrawConstantsSlot, sizeof(NS::Gfx::ScreenLineCB)));

    const ReflectedShader ps = Reflect(L"Shaders/ui_line.ps.hlsl", "PSMain", "ps_5_0");
    ASSERT_NE(ps.reflection, nullptr) << ps.error;
    EXPECT_TRUE(
        HasConstantBuffer(*ps.reflection.Get(), "UiLineCB", NS::Gfx::k_DrawConstantsSlot, sizeof(NS::Gfx::ScreenLineCB)));
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
