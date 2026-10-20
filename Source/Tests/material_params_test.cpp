#include "NSlib/CoreObject/Asset/Material.h"
#include "NSlib/Gfx/D3DShader.h"

#include <gtest/gtest.h>

#include <d3dcompiler.h>
#include <wrl/client.h>

#include <bit>
#include <cstddef>
#include <cstring>
#include <vector>
#include <optional>
#include <span>
#include <string>
#include <string_view>

// 材質の値は HLSL の MaterialParams (b2) に書き、コンパイルしたバイトコードから名前・型・位置・初期値を取り出す。
// 並びは HLSL の格納規則で決まり、C++ の sizeof とは合わないので、コンパイラーの出した物と突き合わせる

namespace
{
    using Microsoft::WRL::ComPtr;
    using NS::Gfx::ShaderParam;
    using NS::Gfx::ShaderParamLayout;
    using NS::Gfx::ShaderParamType;

    constexpr std::string_view k_ProbePs = R"HLSL(
cbuffer MaterialParams : register(b2)
{
    float density = 4.0;
    float3 tint = float3(0.2, 0.4, 0.6);
    float lineWidth = 0.05;
    float2 tiling = float2(1.0, 1.0);
    uint mode = 2;
    bool enabled = true;
    float unusedValue = 9.0;
};
float4 PSMain(float4 pos : SV_Position) : SV_Target
{
    float v = density * lineWidth + tiling.x + tiling.y + (float)mode;
    if (enabled)
    {
        v += 1.0;
    }
    return float4(tint * v, 1.0);
}
)HLSL";

    // コンパイルできなければ空。失敗の文は outError に入れる
    ComPtr<ID3DBlob> Compile(std::string_view source, const char* entry, const char* target, UINT flags, std::string& outError)
    {
        ComPtr<ID3DBlob> blob;
        ComPtr<ID3DBlob> errors;
        const HRESULT hr = ::D3DCompile(source.data(),
                                        source.size(),
                                        "material_params_test",
                                        nullptr,
                                        nullptr,
                                        entry,
                                        target,
                                        flags,
                                        0,
                                        blob.GetAddressOf(),
                                        errors.GetAddressOf());
        if (FAILED(hr))
        {
            if (errors != nullptr)
            {
                outError.assign(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize());
            }
            return nullptr;
        }
        return blob;
    }

    std::span<const std::byte> Bytes(const ComPtr<ID3DBlob>& blob)
    {
        return std::span<const std::byte>(static_cast<const std::byte*>(blob->GetBufferPointer()),
                                          blob->GetBufferSize());
    }

    std::optional<ShaderParamLayout> ReflectSource(std::string_view source, const char* entry, const char* target, UINT flags)
    {
        std::string error;
        const ComPtr<ID3DBlob> blob = Compile(source, entry, target, flags, error);
        EXPECT_NE(blob, nullptr) << error;
        if (blob == nullptr)
        {
            return std::nullopt;
        }
        return NS::Gfx::ReflectMaterialParams(Bytes(blob));
    }

    const ShaderParam* Find(const ShaderParamLayout& layout, std::string_view name)
    {
        for (const ShaderParam& param : layout.params)
        {
            if (param.name == name)
            {
                return &param;
            }
        }
        return nullptr;
    }

    void ExpectParam(const ShaderParamLayout& layout,
                     std::string_view name,
                     ShaderParamType type,
                     NS::uint offset,
                     NS::uint size,
                     bool used)
    {
        const ShaderParam* param = Find(layout, name);
        ASSERT_NE(param, nullptr) << name;
        EXPECT_EQ(param->type, type) << name;
        EXPECT_EQ(param->offset, offset) << name;
        EXPECT_EQ(param->size, size) << name;
        EXPECT_EQ(param->used, used) << name;
    }

    float InitialFloat(const ShaderParamLayout& layout, std::string_view name, std::size_t component)
    {
        const ShaderParam* param = Find(layout, name);
        if (param == nullptr)
        {
            return -1.0f;
        }
        return std::bit_cast<float>(param->initialBits[component]);
    }
} // namespace

// 最適化の有無で並びと初期値が変わらない事も見る。デバッグのビルドのシェーダーは最適化を飛ばしてコンパイルする
class MaterialParamsReflectTest : public ::testing::TestWithParam<UINT>
{
};

TEST_P(MaterialParamsReflectTest, ReadsNamesTypesOffsetsAndInitialValues)
{
    const std::optional<ShaderParamLayout> layout = ReflectSource(k_ProbePs, "PSMain", "ps_5_0", GetParam());
    ASSERT_TRUE(layout.has_value());

    EXPECT_EQ(layout->bufferSize, 48u);
    ASSERT_EQ(layout->params.size(), 7u);
    // float3 は 16 バイト境界に揃わず、前の float の直後に入る
    ExpectParam(*layout, "density", ShaderParamType::Float, 0, 4, true);
    ExpectParam(*layout, "tint", ShaderParamType::Float3, 4, 12, true);
    ExpectParam(*layout, "lineWidth", ShaderParamType::Float, 16, 4, true);
    ExpectParam(*layout, "tiling", ShaderParamType::Float2, 20, 8, true);
    ExpectParam(*layout, "mode", ShaderParamType::Uint, 28, 4, true);
    // bool は C++ の bool でなく 32 ビット
    ExpectParam(*layout, "enabled", ShaderParamType::Bool, 32, 4, true);
    ExpectParam(*layout, "unusedValue", ShaderParamType::Float, 36, 4, false);

    EXPECT_FLOAT_EQ(InitialFloat(*layout, "density", 0), 4.0f);
    EXPECT_FLOAT_EQ(InitialFloat(*layout, "tint", 0), 0.2f);
    EXPECT_FLOAT_EQ(InitialFloat(*layout, "tint", 1), 0.4f);
    EXPECT_FLOAT_EQ(InitialFloat(*layout, "tint", 2), 0.6f);
    EXPECT_FLOAT_EQ(InitialFloat(*layout, "lineWidth", 0), 0.05f);
    EXPECT_FLOAT_EQ(InitialFloat(*layout, "tiling", 1), 1.0f);
    EXPECT_EQ(Find(*layout, "mode")->initialBits[0], 2u);
    EXPECT_NE(Find(*layout, "enabled")->initialBits[0], 0u);
}

INSTANTIATE_TEST_SUITE_P(Flags,
                         MaterialParamsReflectTest,
                         ::testing::Values(D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION,
                                           D3DCOMPILE_OPTIMIZATION_LEVEL3));

TEST(MaterialParamsReflect, ShaderWithoutMaterialParamsHasNoParams)
{
    constexpr std::string_view k_Source = R"HLSL(
float4 PSMain(float4 pos : SV_Position) : SV_Target { return float4(1.0, 1.0, 1.0, 1.0); }
)HLSL";
    const std::optional<ShaderParamLayout> layout = ReflectSource(k_Source, "PSMain", "ps_5_0", 0);
    ASSERT_TRUE(layout.has_value());
    EXPECT_EQ(layout->bufferSize, 0u);
    EXPECT_TRUE(layout->params.empty());
}

TEST(MaterialParamsReflect, RejectsMaterialParamsOutsideB2)
{
    constexpr std::string_view k_Source = R"HLSL(
cbuffer MaterialParams : register(b3) { float density; };
float4 PSMain(float4 pos : SV_Position) : SV_Target { return float4(density, 0.0, 0.0, 1.0); }
)HLSL";
    EXPECT_FALSE(ReflectSource(k_Source, "PSMain", "ps_5_0", 0).has_value());
}

TEST(MaterialParamsReflect, KeepsUnsupportedTypesMarked)
{
    constexpr std::string_view k_Source = R"HLSL(
cbuffer MaterialParams : register(b2) { float4x4 spin; float density; };
float4 PSMain(float4 pos : SV_Position) : SV_Target { return mul(float4(density, 0.0, 0.0, 1.0), spin); }
)HLSL";
    const std::optional<ShaderParamLayout> layout = ReflectSource(k_Source, "PSMain", "ps_5_0", 0);
    ASSERT_TRUE(layout.has_value());
    ASSERT_EQ(layout->params.size(), 2u);
    ExpectParam(*layout, "spin", ShaderParamType::Unsupported, 0, 64, true);
    ExpectParam(*layout, "density", ShaderParamType::Float, 64, 4, true);
}

TEST(MaterialParamsMerge, RejectsSameNameWithDifferentType)
{
    constexpr std::string_view k_Vs = R"HLSL(
cbuffer MaterialParams : register(b2) { float foo; };
float4 VSMain(float3 p : POSITION) : SV_Position { return float4(p * foo, 1.0); }
)HLSL";
    constexpr std::string_view k_Ps = R"HLSL(
cbuffer MaterialParams : register(b2) { float2 foo; };
float4 PSMain(float4 pos : SV_Position) : SV_Target { return float4(foo, 0.0, 1.0); }
)HLSL";
    const std::optional<ShaderParamLayout> vs = ReflectSource(k_Vs, "VSMain", "vs_5_0", 0);
    const std::optional<ShaderParamLayout> ps = ReflectSource(k_Ps, "PSMain", "ps_5_0", 0);
    ASSERT_TRUE(vs.has_value());
    ASSERT_TRUE(ps.has_value());
    EXPECT_FALSE(NS::Gfx::MergeMaterialParams(*vs, *ps).has_value());
}

TEST(MaterialParamsMerge, SameDeclarationMergesUsedFromBothStages)
{
    constexpr std::string_view k_Vs = R"HLSL(
cbuffer MaterialParams : register(b2) { float lift; float shade; };
float4 VSMain(float3 p : POSITION) : SV_Position { return float4(p + lift, 1.0); }
)HLSL";
    constexpr std::string_view k_Ps = R"HLSL(
cbuffer MaterialParams : register(b2) { float lift; float shade; };
float4 PSMain(float4 pos : SV_Position) : SV_Target { return float4(shade, 0.0, 0.0, 1.0); }
)HLSL";
    const std::optional<ShaderParamLayout> vs = ReflectSource(k_Vs, "VSMain", "vs_5_0", 0);
    const std::optional<ShaderParamLayout> ps = ReflectSource(k_Ps, "PSMain", "ps_5_0", 0);
    ASSERT_TRUE(vs.has_value());
    ASSERT_TRUE(ps.has_value());
    const std::optional<ShaderParamLayout> merged = NS::Gfx::MergeMaterialParams(*vs, *ps);
    ASSERT_TRUE(merged.has_value());
    ExpectParam(*merged, "lift", ShaderParamType::Float, 0, 4, true);
    ExpectParam(*merged, "shade", ShaderParamType::Float, 4, 4, true);
}

TEST(MaterialParamsMerge, OneSideEmptyTakesTheOther)
{
    ShaderParamLayout ps;
    ps.bufferSize = 16;
    ShaderParam param;
    param.name = "tileLength";
    param.type = ShaderParamType::Float;
    param.size = 4;
    param.used = true;
    ps.params.push_back(param);
    const std::optional<ShaderParamLayout> merged = NS::Gfx::MergeMaterialParams(ShaderParamLayout{}, ps);
    ASSERT_TRUE(merged.has_value());
    EXPECT_EQ(merged->bufferSize, 16u);
    ASSERT_EQ(merged->params.size(), 1u);
    EXPECT_EQ(merged->params[0].name, "tileLength");
}

namespace
{
    ShaderParam MakeParam(std::string name, ShaderParamType type, NS::uint offset, NS::uint size, float initial)
    {
        ShaderParam param;
        param.name = std::move(name);
        param.type = type;
        param.offset = offset;
        param.size = size;
        param.initialBits[0] = std::bit_cast<NS::uint32>(initial);
        param.used = true;
        return param;
    }

    float ReadFloat(const std::vector<std::byte>& bytes, std::size_t offset)
    {
        float value = 0.0f;
        std::memcpy(&value, bytes.data() + offset, sizeof(value));
        return value;
    }
} // namespace

// 保存した値は名前と型が合う項目にだけ当て、合わない項目はシェーダーの初期値で埋める
TEST(MaterialParamsPack, StoredValuesGoToTheirOffsetAndOthersUseTheInitialValue)
{
    ShaderParamLayout layout;
    layout.bufferSize = 16;
    layout.params.push_back(MakeParam("tileLength", ShaderParamType::Float, 0, 4, 1.0f));
    layout.params.push_back(MakeParam("gap", ShaderParamType::Float, 4, 4, 0.5f));

    NS::MaterialParamValues values;
    values["tileLength"] = NS::MaterialParamValue{.numbers = {3.125}};

    const std::vector<std::byte> bytes = NS::PackMaterialParams(layout, values);
    ASSERT_EQ(bytes.size(), 16u);
    EXPECT_FLOAT_EQ(ReadFloat(bytes, 0), 3.125f);
    EXPECT_FLOAT_EQ(ReadFloat(bytes, 4), 0.5f);
}

// シェーダーを直して並びが変わっても、名前と型の合う値は新しい位置へ移る。型が変わった項目には前の値を当てない
TEST(MaterialParamsPack, ReorderedShaderKeepsValuesByNameAndDropsChangedTypes)
{
    NS::MaterialParamValues values;
    values["tileLength"] = NS::MaterialParamValue{.numbers = {3.125}};
    values["gap"] = NS::MaterialParamValue{.numbers = {0.1}};
    values["gone"] = NS::MaterialParamValue{.numbers = {9.0}};

    ShaderParamLayout reordered;
    reordered.bufferSize = 16;
    reordered.params.push_back(MakeParam("gap", ShaderParamType::Float, 0, 4, 0.5f));
    reordered.params.push_back(MakeParam("tileLength", ShaderParamType::Float2, 4, 8, 2.0f));

    const std::vector<std::byte> bytes = NS::PackMaterialParams(reordered, values);
    EXPECT_FLOAT_EQ(ReadFloat(bytes, 0), 0.1f);
    EXPECT_FLOAT_EQ(ReadFloat(bytes, 4), 2.0f);
    EXPECT_FALSE(NS::FitsParam(reordered.params[1], values.at("tileLength")));
}

TEST(MaterialParamsPack, IntegerAndBoolFitOnlyTheirOwnKind)
{
    const ShaderParam mode = MakeParam("mode", ShaderParamType::Uint, 0, 4, 0.0f);
    const ShaderParam level = MakeParam("level", ShaderParamType::Int, 0, 4, 0.0f);
    const ShaderParam enabled = MakeParam("enabled", ShaderParamType::Bool, 0, 4, 0.0f);
    EXPECT_TRUE(NS::FitsParam(mode, NS::MaterialParamValue{.numbers = {2.0}}));
    EXPECT_FALSE(NS::FitsParam(mode, NS::MaterialParamValue{.numbers = {2.5}}));
    EXPECT_FALSE(NS::FitsParam(mode, NS::MaterialParamValue{.numbers = {-1.0}}));
    EXPECT_TRUE(NS::FitsParam(level, NS::MaterialParamValue{.numbers = {-1.0}}));
    EXPECT_TRUE(NS::FitsParam(enabled, NS::MaterialParamValue{.isBool = true, .numbers = {1.0}}));
    EXPECT_FALSE(NS::FitsParam(enabled, NS::MaterialParamValue{.numbers = {1.0}}));
    EXPECT_FALSE(NS::FitsParam(mode, NS::MaterialParamValue{.isBool = true, .numbers = {1.0}}));
}

// 初期値はシェーダーに書いた値を、.mat に書く値の形で返す
TEST(MaterialParamsPack, InitialValueReadsTheShaderDefault)
{
    const ShaderParam length = MakeParam("tileLength", ShaderParamType::Float, 0, 4, 3.125f);
    EXPECT_EQ(NS::InitialParamValue(length).numbers, std::vector<double>{3.125});
    ShaderParam mode = MakeParam("mode", ShaderParamType::Uint, 0, 4, 0.0f);
    mode.initialBits[0] = 2u;
    EXPECT_EQ(NS::InitialParamValue(mode).numbers, std::vector<double>{2.0});
}
