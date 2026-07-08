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
        if (m_states)
            return m_states->Opaque();
        return nullptr;
    }

    ID3D11BlendState* CommonStates::AlphaBlend() const noexcept
    {
        if (m_states)
            return m_states->AlphaBlend();
        return nullptr;
    }

    ID3D11DepthStencilState* CommonStates::DepthDefault() const noexcept
    {
        if (m_states)
            return m_states->DepthDefault();
        return nullptr;
    }

    ID3D11DepthStencilState* CommonStates::DepthNone() const noexcept
    {
        if (m_states)
            return m_states->DepthNone();
        return nullptr;
    }

    ID3D11RasterizerState* CommonStates::CullCounterClockwise() const noexcept
    {
        if (m_states)
            return m_states->CullCounterClockwise();
        return nullptr;
    }

    ID3D11RasterizerState* CommonStates::CullClockwise() const noexcept
    {
        if (m_states)
            return m_states->CullClockwise();
        return nullptr;
    }

    ID3D11SamplerState* CommonStates::LinearWrap() const noexcept
    {
        if (m_states)
            return m_states->LinearWrap();
        return nullptr;
    }

    ID3D11SamplerState* CommonStates::LinearClamp() const noexcept
    {
        if (m_states)
            return m_states->LinearClamp();
        return nullptr;
    }

    ID3D11SamplerState* CommonStates::PointWrap() const noexcept
    {
        if (m_states)
            return m_states->PointWrap();
        return nullptr;
    }

    ID3D11SamplerState* CommonStates::PointClamp() const noexcept
    {
        if (m_states)
            return m_states->PointClamp();
        return nullptr;
    }

} // namespace NS::Graphics
