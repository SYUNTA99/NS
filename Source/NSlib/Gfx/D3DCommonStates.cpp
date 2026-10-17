#include "NSlib/Gfx/D3DCommonStates.h"

#include "NSlib/Core/Logger.h"

#include <CommonStates.h>
#include <memory>

namespace NS
{
    namespace Gfx
    {
        D3DCommonStates::D3DCommonStates(ID3D11Device* device) noexcept
        {
            if (device == nullptr)
            {
                NS_LOG_ERROR(Graphics, "D3DCommonStates 構築時に device が null");
                return;
            }
            m_states = std::make_unique<DirectX::CommonStates>(device);
        }

        D3DCommonStates::~D3DCommonStates() = default;

        bool D3DCommonStates::IsValid() const noexcept
        {
            return m_states != nullptr;
        }

        ID3D11BlendState* D3DCommonStates::Opaque() const noexcept
        {
            if (m_states)
            {
                return m_states->Opaque();
            }
            return nullptr;
        }

        ID3D11BlendState* D3DCommonStates::AlphaBlend() const noexcept
        {
            if (m_states)
            {
                return m_states->AlphaBlend();
            }
            return nullptr;
        }

        ID3D11DepthStencilState* D3DCommonStates::DepthDefault() const noexcept
        {
            if (m_states)
            {
                return m_states->DepthDefault();
            }
            return nullptr;
        }

        ID3D11DepthStencilState* D3DCommonStates::DepthNone() const noexcept
        {
            if (m_states)
            {
                return m_states->DepthNone();
            }
            return nullptr;
        }

        ID3D11RasterizerState* D3DCommonStates::CullCounterClockwise() const noexcept
        {
            if (m_states)
            {
                return m_states->CullCounterClockwise();
            }
            return nullptr;
        }

        ID3D11RasterizerState* D3DCommonStates::CullClockwise() const noexcept
        {
            if (m_states)
            {
                return m_states->CullClockwise();
            }
            return nullptr;
        }

        ID3D11SamplerState* D3DCommonStates::LinearWrap() const noexcept
        {
            if (m_states)
            {
                return m_states->LinearWrap();
            }
            return nullptr;
        }

        ID3D11SamplerState* D3DCommonStates::LinearClamp() const noexcept
        {
            if (m_states)
            {
                return m_states->LinearClamp();
            }

            return nullptr;
        }

        ID3D11SamplerState* D3DCommonStates::PointWrap() const noexcept
        {
            if (m_states)
            {
                return m_states->PointWrap();
            }

            return nullptr;
        }

        ID3D11SamplerState* D3DCommonStates::PointClamp() const noexcept
        {
            if (m_states)
            {
                return m_states->PointClamp();
            }

            return nullptr;
        }
    } // namespace Gfx
} // namespace NS
