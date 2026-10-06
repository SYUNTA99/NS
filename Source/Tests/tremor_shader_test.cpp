#include "NSlib/Core/Math.h"
#include "NSlib/Graphics/FrameConstants.h"

#include <gtest/gtest.h>

#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

// 衝撃の震えは頂点のシェーダーが描く形をずらす。窓の要らないソフトウェアの描画装置 (WARP) で standard.vs.hlsl を
// コンパイルし、頂点のシェーダーの出力を流し出して読む。world を平行移動だけ、viewProj を単位行列にすると、
// 出力の位置が世界の位置に震えのずれを足した物になる

namespace
{
    using Microsoft::WRL::ComPtr;

    constexpr float k_Pi = 3.14159265358979f;

    struct TestVertex
    {
        NS::Vector3 pos;
        float u = 0.0f;
        float v = 0.0f;
        NS::Vector3 normal{0.0f, 1.0f, 0.0f};
    };

    // 試しの側で書いた震えの式。シェーダーの式と別に書き、出力と比べる
    // 衝突点は world 行列の位置 origin からのずれ
    NS::Vector3 ExpectedOffset(const NS::Gfx::TremorCB& tremor,
                                     const NS::Vector3& origin,
                                     const NS::Vector3& pos)
    {
        if (!(tremor.amplitude > 0.0f) || !(tremor.ringFrames > 0.0f))
        {
            return NS::Vector3{0.0f, 0.0f, 0.0f};
        }
        const float t = tremor.elapsedFrames - (pos - origin - tremor.contactOffset).Length() * tremor.framesPerMeter;
        if (t < 0.0f || t >= tremor.ringFrames)
        {
            return NS::Vector3{0.0f, 0.0f, 0.0f};
        }
        const float envelope = 1.0f - t / tremor.ringFrames;
        return (tremor.right * std::cos(k_Pi * t) + tremor.up * std::sin(k_Pi * t * 0.5f)) * tremor.amplitude *
               envelope;
    }

    // 頂点のシェーダーを 1 回走らせて、点ごとの出力の位置を返す。描画装置かコンパイルが無い時は空
    class TremorShaderRig
    {
    public:
        TremorShaderRig()
        {
            HRESULT hr = ::D3D11CreateDevice(nullptr,
                                             D3D_DRIVER_TYPE_WARP,
                                             nullptr,
                                             0,
                                             nullptr,
                                             0,
                                             D3D11_SDK_VERSION,
                                             m_device.GetAddressOf(),
                                             nullptr,
                                             m_context.GetAddressOf());
            if (FAILED(hr))
            {
                return;
            }
            ComPtr<ID3DBlob> errors;
            hr = ::D3DCompileFromFile(L"Shaders/standard.vs.hlsl",
                                      nullptr,
                                      D3D_COMPILE_STANDARD_FILE_INCLUDE,
                                      "VSMain",
                                      "vs_5_0",
                                      0,
                                      0,
                                      m_blob.GetAddressOf(),
                                      errors.GetAddressOf());
            if (FAILED(hr))
            {
                if (errors != nullptr)
                {
                    m_compileError.assign(static_cast<const char*>(errors->GetBufferPointer()),
                                          errors->GetBufferSize());
                }
                m_blob.Reset();
                return;
            }
            (void)m_device->CreateVertexShader(
                m_blob->GetBufferPointer(), m_blob->GetBufferSize(), nullptr, m_vs.GetAddressOf());
            const D3D11_SO_DECLARATION_ENTRY entry{0, "SV_POSITION", 0, 0, 4, 0};
            const UINT stride = sizeof(float) * 4;
            (void)m_device->CreateGeometryShaderWithStreamOutput(m_blob->GetBufferPointer(),
                                                                 m_blob->GetBufferSize(),
                                                                 &entry,
                                                                 1,
                                                                 &stride,
                                                                 1,
                                                                 D3D11_SO_NO_RASTERIZED_STREAM,
                                                                 nullptr,
                                                                 m_gs.GetAddressOf());
            const D3D11_INPUT_ELEMENT_DESC layout[] = {
                {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0},
            };
            (void)m_device->CreateInputLayout(
                layout, 3, m_blob->GetBufferPointer(), m_blob->GetBufferSize(), m_layout.GetAddressOf());
        }

        [[nodiscard]] bool Valid() const { return m_vs != nullptr && m_gs != nullptr && m_layout != nullptr; }
        [[nodiscard]] bool HasDevice() const { return m_device != nullptr; }
        [[nodiscard]] const std::string& CompileError() const { return m_compileError; }

        // points は模型の空間の位置。world は origin への平行移動にし、出力は世界の位置
        std::vector<NS::Vector3> Run(const NS::Gfx::TremorCB& tremor,
                                           const NS::Vector3& origin,
                                           const std::vector<NS::Vector3>& points)
        {
            std::vector<NS::Vector3> out;
            NS::Gfx::FrameCB constants{};
            constants.world = NS::Matrix::CreateTranslation(origin);
            constants.viewProj = NS::Matrix::Identity;
            constants.tremor = tremor;

            D3D11_BUFFER_DESC cbDesc{};
            cbDesc.ByteWidth = sizeof(constants);
            cbDesc.Usage = D3D11_USAGE_DEFAULT;
            cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            const D3D11_SUBRESOURCE_DATA cbData{&constants, 0, 0};
            ComPtr<ID3D11Buffer> cb;
            if (FAILED(m_device->CreateBuffer(&cbDesc, &cbData, cb.GetAddressOf())))
            {
                return out;
            }

            std::vector<TestVertex> vertices;
            for (const NS::Vector3& point : points)
            {
                TestVertex vertex;
                vertex.pos = point;
                vertices.push_back(vertex);
            }
            D3D11_BUFFER_DESC vbDesc{};
            vbDesc.ByteWidth = static_cast<UINT>(sizeof(TestVertex) * vertices.size());
            vbDesc.Usage = D3D11_USAGE_DEFAULT;
            vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            const D3D11_SUBRESOURCE_DATA vbData{vertices.data(), 0, 0};
            ComPtr<ID3D11Buffer> vb;
            if (FAILED(m_device->CreateBuffer(&vbDesc, &vbData, vb.GetAddressOf())))
            {
                return out;
            }

            D3D11_BUFFER_DESC soDesc{};
            soDesc.ByteWidth = static_cast<UINT>(sizeof(float) * 4 * points.size());
            soDesc.Usage = D3D11_USAGE_DEFAULT;
            soDesc.BindFlags = D3D11_BIND_STREAM_OUTPUT;
            ComPtr<ID3D11Buffer> so;
            if (FAILED(m_device->CreateBuffer(&soDesc, nullptr, so.GetAddressOf())))
            {
                return out;
            }
            D3D11_BUFFER_DESC stagingDesc = soDesc;
            stagingDesc.Usage = D3D11_USAGE_STAGING;
            stagingDesc.BindFlags = 0;
            stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            ComPtr<ID3D11Buffer> staging;
            if (FAILED(m_device->CreateBuffer(&stagingDesc, nullptr, staging.GetAddressOf())))
            {
                return out;
            }

            const UINT stride = sizeof(TestVertex);
            const UINT offset = 0;
            ID3D11Buffer* vbs[] = {vb.Get()};
            ID3D11Buffer* sos[] = {so.Get()};
            ID3D11Buffer* cbs[] = {cb.Get()};
            m_context->IASetInputLayout(m_layout.Get());
            m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
            m_context->IASetVertexBuffers(0, 1, vbs, &stride, &offset);
            m_context->VSSetShader(m_vs.Get(), nullptr, 0);
            m_context->VSSetConstantBuffers(0, 1, cbs);
            m_context->GSSetShader(m_gs.Get(), nullptr, 0);
            m_context->PSSetShader(nullptr, nullptr, 0);
            m_context->SOSetTargets(1, sos, &offset);
            m_context->Draw(static_cast<UINT>(points.size()), 0);
            ID3D11Buffer* none[] = {nullptr};
            m_context->SOSetTargets(1, none, &offset);
            m_context->CopyResource(staging.Get(), so.Get());

            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (FAILED(m_context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
            {
                return out;
            }
            const float* values = static_cast<const float*>(mapped.pData);
            for (std::size_t i = 0; i < points.size(); ++i)
            {
                out.push_back(NS::Vector3{values[i * 4 + 0], values[i * 4 + 1], values[i * 4 + 2]});
            }
            m_context->Unmap(staging.Get(), 0);
            return out;
        }

    private:
        ComPtr<ID3D11Device> m_device;
        ComPtr<ID3D11DeviceContext> m_context;
        ComPtr<ID3DBlob> m_blob;
        ComPtr<ID3D11VertexShader> m_vs;
        ComPtr<ID3D11GeometryShader> m_gs;
        ComPtr<ID3D11InputLayout> m_layout;
        std::string m_compileError;
    };

    // 半径 0.5 の玉の表面。衝突点は -X の端、一番遠い所は +X の端で、衝突点から 1 m。衝突点は玉の中心からのずれ
    NS::Gfx::TremorCB BallTremor(float elapsedFrames)
    {
        NS::Gfx::TremorCB tremor;
        tremor.contactOffset = NS::Vector3{-0.5f, 0.0f, 0.0f};
        tremor.amplitude = 0.01f;
        tremor.right = NS::Vector3{0.0f, 0.0f, -1.0f};
        tremor.up = NS::Vector3{0.0f, 1.0f, 0.0f};
        tremor.elapsedFrames = elapsedFrames;
        tremor.framesPerMeter = 6.0f;
        tremor.ringFrames = 8.0f;
        return tremor;
    }

    std::vector<NS::Vector3> BallPoints()
    {
        std::vector<NS::Vector3> points;
        for (int i = 0; i < 24; ++i)
        {
            const float a = static_cast<float>(i) * k_Pi / 12.0f;
            points.push_back(
                NS::Vector3{0.5f * std::cos(a), 0.5f * std::sin(a) * 0.6f, 0.5f * std::sin(a) * 0.8f});
        }
        return points;
    }
} // namespace

// 震えの欄は物ごとの定数の最後に置く。頂点のシェーダーの cbuffer と並びを揃える
TEST(TremorShader, ConstantsSitAtTheEndOfTheFrameConstants)
{
    EXPECT_EQ(sizeof(NS::Gfx::TremorCB), 64u);
    EXPECT_EQ(offsetof(NS::Gfx::FrameCB, tremor), 208u);
    EXPECT_EQ(sizeof(NS::Gfx::FrameCB), 272u);
    // 書いていない震えは振れ幅 0 で、どの物も震えない
    const NS::Gfx::FrameCB constants{};
    EXPECT_FLOAT_EQ(constants.tremor.amplitude, 0.0f);
}

// 頂点のシェーダーは、衝突点からの距離の分だけ遅れて始まり、1 か所は ringFrames で弱まって止まる震えを足す
// 始まりのフレームは衝突点の所だけ、届くフレーム数で一番遠い所まで、その後は弱まって 0 になる
TEST(TremorShader, VertexShaderDelaysTheTremorByTheDistanceFromTheContact)
{
    TremorShaderRig rig;
    if (!rig.HasDevice())
    {
        GTEST_SKIP() << "WARP の描画装置を作れなかった";
    }
    ASSERT_TRUE(rig.Valid()) << rig.CompileError();

    // 玉は世界の原点から離して置き、衝突点が world 行列の位置に付いていくのも見る
    const NS::Vector3 origin{2.0f, 1.0f, -3.0f};
    const NS::Vector3 contact{-0.5f, 0.0f, 0.0f};
    const NS::Vector3 farSide{0.5f, 0.0f, 0.0f};
    std::vector<NS::Vector3> points = BallPoints();
    points.push_back(contact);
    points.push_back(farSide);
    for (int frame = 0; frame <= 16; ++frame)
    {
        SCOPED_TRACE(frame);
        const NS::Gfx::TremorCB tremor = BallTremor(static_cast<float>(frame));
        const std::vector<NS::Vector3> moved = rig.Run(tremor, origin, points);
        ASSERT_EQ(moved.size(), points.size());
        for (std::size_t i = 0; i < points.size(); ++i)
        {
            const NS::Vector3 world = origin + points[i];
            const NS::Vector3 expected = world + ExpectedOffset(tremor, origin, world);
            EXPECT_NEAR(moved[i].x, expected.x, 1.0e-5f);
            EXPECT_NEAR(moved[i].y, expected.y, 1.0e-5f);
            EXPECT_NEAR(moved[i].z, expected.z, 1.0e-5f);
            // 画面の横と縦のどちらも振れ幅以内。突進の向き (画面の奥) には動かさない
            const NS::Vector3 offset = moved[i] - world;
            EXPECT_LE(std::abs(offset.Dot(tremor.right)), tremor.amplitude + 1.0e-6f);
            EXPECT_LE(std::abs(offset.Dot(tremor.up)), tremor.amplitude + 1.0e-6f);
            EXPECT_NEAR(offset.x, 0.0f, 1.0e-6f);
        }
        const NS::Vector3 atContact = moved[points.size() - 2] - origin - contact;
        const NS::Vector3 atFar = moved[points.size() - 1] - origin - farSide;
        if (frame == 0)
        {
            // 衝突点は始まりのフレームから画面の横へ振れ幅いっぱいに動き、一番遠い所はまだ動かない
            EXPECT_NEAR(atContact.Dot(tremor.right), tremor.amplitude, 1.0e-6f);
            EXPECT_FLOAT_EQ(atFar.Length(), 0.0f);
        }
        if (frame == 6)
        {
            // 届くフレーム数で一番遠い所が震え始める
            EXPECT_NEAR(atFar.Dot(tremor.right), tremor.amplitude, 1.0e-6f);
        }
        if (frame >= 14)
        {
            EXPECT_FLOAT_EQ(atContact.Length(), 0.0f);
            EXPECT_FLOAT_EQ(atFar.Length(), 0.0f);
        }
    }

    // 振れ幅 0 の物は震えない
    NS::Gfx::TremorCB still = BallTremor(2.0f);
    still.amplitude = 0.0f;
    const std::vector<NS::Vector3> unmoved = rig.Run(still, origin, points);
    ASSERT_EQ(unmoved.size(), points.size());
    for (std::size_t i = 0; i < points.size(); ++i)
    {
        EXPECT_FLOAT_EQ(unmoved[i].x, origin.x + points[i].x);
        EXPECT_FLOAT_EQ(unmoved[i].y, origin.y + points[i].y);
        EXPECT_FLOAT_EQ(unmoved[i].z, origin.z + points[i].z);
    }
}
