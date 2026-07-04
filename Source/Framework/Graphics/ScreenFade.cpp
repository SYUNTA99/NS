#include "Framework/Graphics/ScreenFade.h"

#include "Framework/Graphics/Buffer.h"
#include "Framework/Graphics/CommandList.h"
#include "Framework/Graphics/GraphicObject.h"
#include "Framework/Graphics/Mesh.h"
#include "Framework/Graphics/Pipeline.h"
#include "Framework/Graphics/Renderer.h"
#include "Framework/Graphics/Shader.h"

#include "Framework/Core/Filesystem.h"
#include "Framework/Core/LogCategories.h"
#include "Framework/Core/Logger.h"

namespace NS::Graphics
{

    namespace
    {
        // fade.ps.hlsl の FadeCB と一致するレイアウト。 float4 1 個で 16 バイト
        struct alignas(16) FadeCB
        {
            NS::Math::Color color;
        };
        static_assert(sizeof(FadeCB) == 16, "FadeCB は HLSL の cbuffer b0 とバイト一致が必要");
    } // namespace

    std::unique_ptr<ScreenFade> ScreenFade::Create()
    {
        return std::unique_ptr<ScreenFade>(new ScreenFade());
    }

    ScreenFade::ScreenFade()
    {
        auto* device = Gpu().device;
        if (device == nullptr)
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Graphics, "ScreenFade: グローバル Device が無効");
            return;
        }

        const auto contentRoot = ::NS::Core::FileSystem::ContentRoot();
        m_vs = Shader::Create(contentRoot / "Shaders" / "fade.vs.hlsl");
        m_ps = Shader::Create(contentRoot / "Shaders" / "fade.ps.hlsl");
        if (!m_vs->IsValid() || !m_ps->IsValid())
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Graphics, "ScreenFade: shader 構築失敗");
            return;
        }

        BufferDesc cbDesc = MakeConstantBufferDesc(sizeof(FadeCB));
        m_cb = Buffer::Create(cbDesc);
        if (!m_cb->IsValid())
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Graphics, "ScreenFade: ConstantBuffer 構築失敗");
            return;
        }

        // 描画済みシーンの上へ半透明で重ねる。 深度は無効で常に最前面、 全画面三角形なのでカリングも無効
        PipelineDesc pipeDesc{};
        pipeDesc.cull = CullMode::None;
        pipeDesc.blend = BlendMode::Alpha;
        pipeDesc.depth = DepthMode::Disabled;
        m_pipeline = Pipeline::Create(pipeDesc);
        if (!m_pipeline->IsValid())
        {
            NS_LOG_ERROR(::NS::Core::LogCat::Graphics, "ScreenFade: Pipeline 構築失敗");
            return;
        }

        m_valid = true;
    }

    ScreenFade::~ScreenFade() = default;

    void ScreenFade::Render(Renderer& renderer, const NS::Math::Color& color) noexcept
    {
        if (!m_valid)
            return;
        auto& cmd = renderer.Commands();
        if (cmd.Native() == nullptr)
            return;

        FadeCB cbData{};
        cbData.color = color;
        cmd.UpdateBuffer(*m_cb, &cbData, sizeof(cbData));

        cmd.SetPipeline(*m_pipeline);
        cmd.SetShader(*m_vs);
        cmd.SetShader(*m_ps);
        cmd.SetConstantBuffer(*m_cb, 0, ShaderType::Pixel);

        // VS が SV_VertexID から三角形を作るので、 InputLayout を外し
        // 頂点もインデックスもバインドせず 3 頂点を投げる
        cmd->IASetInputLayout(nullptr);
        cmd.SetTopology(Topology::TriangleList);
        cmd.Draw(3);
    }

    bool ScreenFade::IsValid() const noexcept
    {
        return m_valid;
    }

} // namespace NS::Graphics
