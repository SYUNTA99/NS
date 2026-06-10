#include "Framework/Graphics/CommonStates.h"

#include "Framework/Graphics/D3dCommon.h"

#include <CommonStates.h>

namespace NS::Graphics
{

    CommonStates::CommonStates(ID3D11Device* device)
    {
        if (device != nullptr)
        {
            m_states = std::make_unique<DirectX::CommonStates>(device);
        }
    }

    CommonStates::~CommonStates() = default;

    ID3D11BlendState* CommonStates::Opaque() const noexcept
    {
        return m_states ? m_states->Opaque() : nullptr;
    }

    ID3D11BlendState* CommonStates::AlphaBlend() const noexcept
    {
        return m_states ? m_states->AlphaBlend() : nullptr;
    }

    ID3D11DepthStencilState* CommonStates::DepthDefault() const noexcept
    {
        return m_states ? m_states->DepthDefault() : nullptr;
    }

    ID3D11DepthStencilState* CommonStates::DepthNone() const noexcept
    {
        return m_states ? m_states->DepthNone() : nullptr;
    }

    ID3D11RasterizerState* CommonStates::CullCounterClockwise() const noexcept
    {
        return m_states ? m_states->CullCounterClockwise() : nullptr;
    }

    ID3D11RasterizerState* CommonStates::CullClockwise() const noexcept
    {
        return m_states ? m_states->CullClockwise() : nullptr;
    }

    ID3D11SamplerState* CommonStates::LinearWrap() const noexcept
    {
        return m_states ? m_states->LinearWrap() : nullptr;
    }

    ID3D11SamplerState* CommonStates::LinearClamp() const noexcept
    {
        return m_states ? m_states->LinearClamp() : nullptr;
    }

    ID3D11SamplerState* CommonStates::PointWrap() const noexcept
    {
        return m_states ? m_states->PointWrap() : nullptr;
    }

    ID3D11SamplerState* CommonStates::PointClamp() const noexcept
    {
        return m_states ? m_states->PointClamp() : nullptr;
    }

} // namespace NS::Graphics
