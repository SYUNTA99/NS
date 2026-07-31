#include "Runtime/Graphics/Pipeline.h"

#include "Runtime/Core/LogCategories.h"
#include "Runtime/Core/Logger.h"
#include "Runtime/Graphics/GraphicObject.h"

namespace NS::Graphics
{

    namespace
    {
        D3D11_CULL_MODE ToD3d(CullMode cull) noexcept
        {
            switch (cull)
            {
            case CullMode::None:
                return D3D11_CULL_NONE;
            case CullMode::Front:
                return D3D11_CULL_FRONT;
            case CullMode::Back:
            default:
                return D3D11_CULL_BACK;
            }
        }

        D3D11_FILL_MODE ToD3d(FillMode fill) noexcept
        {
            if (fill == FillMode::Wireframe)
            {
                return D3D11_FILL_WIREFRAME;
            }
            return D3D11_FILL_SOLID;
        }

        bool CreateRasterizer(ID3D11Device* device,
                              const PipelineDesc& desc,
                              ComPtr<ID3D11RasterizerState>& out) noexcept
        {
            D3D11_RASTERIZER_DESC rd{};
            rd.FillMode = ToD3d(desc.fill);
            rd.CullMode = ToD3d(desc.cull);
            rd.FrontCounterClockwise = FALSE;
            rd.DepthClipEnable = TRUE;
            return SUCCEEDED(device->CreateRasterizerState(&rd, out.GetAddressOf()));
        }

        bool CreateBlend(ID3D11Device* device, const PipelineDesc& desc, ComPtr<ID3D11BlendState>& out) noexcept
        {
            D3D11_BLEND_DESC bd{};
            auto& rt = bd.RenderTarget[0];
            rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
            switch (desc.blend)
            {
            case BlendMode::Alpha:
                rt.BlendEnable = TRUE;
                rt.SrcBlend = D3D11_BLEND_SRC_ALPHA;
                rt.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
                rt.BlendOp = D3D11_BLEND_OP_ADD;
                rt.SrcBlendAlpha = D3D11_BLEND_ONE;
                rt.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
                rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
                break;
            case BlendMode::Additive:
                rt.BlendEnable = TRUE;
                rt.SrcBlend = D3D11_BLEND_SRC_ALPHA;
                rt.DestBlend = D3D11_BLEND_ONE;
                rt.BlendOp = D3D11_BLEND_OP_ADD;
                rt.SrcBlendAlpha = D3D11_BLEND_ZERO;
                rt.DestBlendAlpha = D3D11_BLEND_ONE;
                rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
                break;
            case BlendMode::Opaque:
            default:
                rt.BlendEnable = FALSE;
                rt.SrcBlend = D3D11_BLEND_ONE;
                rt.DestBlend = D3D11_BLEND_ZERO;
                rt.BlendOp = D3D11_BLEND_OP_ADD;
                rt.SrcBlendAlpha = D3D11_BLEND_ONE;
                rt.DestBlendAlpha = D3D11_BLEND_ZERO;
                rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
                break;
            }
            return SUCCEEDED(device->CreateBlendState(&bd, out.GetAddressOf()));
        }

        bool CreateDepthStencil(ID3D11Device* device,
                                const PipelineDesc& desc,
                                ComPtr<ID3D11DepthStencilState>& out) noexcept
        {
            D3D11_DEPTH_STENCIL_DESC dd{};
            dd.StencilEnable = FALSE;
            dd.StencilReadMask = D3D11_DEFAULT_STENCIL_READ_MASK;
            dd.StencilWriteMask = D3D11_DEFAULT_STENCIL_WRITE_MASK;
            switch (desc.depth)
            {
            case DepthMode::ReadOnly:
                dd.DepthEnable = TRUE;
                dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
                dd.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
                break;
            case DepthMode::Disabled:
                dd.DepthEnable = FALSE;
                dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
                dd.DepthFunc = D3D11_COMPARISON_ALWAYS;
                break;
            case DepthMode::ReadWrite:
            default:
                dd.DepthEnable = TRUE;
                dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
                dd.DepthFunc = D3D11_COMPARISON_LESS;
                break;
            }
            return SUCCEEDED(device->CreateDepthStencilState(&dd, out.GetAddressOf()));
        }
    } // namespace

    std::unique_ptr<Pipeline> Pipeline::Create(const PipelineDesc& desc)
    {
        return std::unique_ptr<Pipeline>(new Pipeline(desc));
    }

    Pipeline::Pipeline(const PipelineDesc& desc) noexcept : m_desc(desc)
    {
        ID3D11Device* device = Gpu().device;
        if (device == nullptr)
        {
            NS_LOG_ERROR(Graphics, "Pipeline: Device 不在のため構築不可");
            return;
        }
        if (!CreateRasterizer(device, desc, m_rasterizer) || !CreateBlend(device, desc, m_blend) ||
            !CreateDepthStencil(device, desc, m_depthStencil))
        {
            NS_LOG_ERROR(Graphics, "Pipeline: ステートオブジェクトの構築失敗");
            return;
        }
        m_valid = true;
    }

    Pipeline::~Pipeline() = default;

    bool Pipeline::IsValid() const noexcept
    {
        return m_valid;
    }

    const PipelineDesc& Pipeline::Desc() const noexcept
    {
        return m_desc;
    }

    ID3D11RasterizerState* Pipeline::RasterizerState() const noexcept
    {
        return m_rasterizer.Get();
    }

    ID3D11BlendState* Pipeline::BlendState() const noexcept
    {
        return m_blend.Get();
    }

    ID3D11DepthStencilState* Pipeline::DepthStencilState() const noexcept
    {
        return m_depthStencil.Get();
    }

} // namespace NS::Graphics
