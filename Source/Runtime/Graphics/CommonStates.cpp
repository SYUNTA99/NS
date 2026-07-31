#include "Runtime/Graphics/CommonStates.h"

#include "Runtime/Core/LogCategories.h"
#include "Runtime/Core/Logger.h"

#include <CommonStates.h>
#include <exception>
#include <memory>

namespace NS::Graphics
{

    CommonStates::CommonStates(ID3D11Device* device) noexcept
    {
        if (device == nullptr)
        {
            NS_LOG_ERROR(Graphics, "CommonStates 構築時に device が null");
            return;
        }
        // DirectXTK は生成失敗を例外で投げるので、 ここで受けて m_states を空のままにする
        try
        {
            m_states = std::make_unique<DirectX::CommonStates>(device);
        }
        catch (const std::exception& e)
        {
            NS_LOG_ERROR(Graphics, "DirectXTK CommonStates 構築失敗: {}", e.what());
        }
    }

    CommonStates::~CommonStates() = default;

    bool CommonStates::IsValid() const noexcept
    {
        return m_states != nullptr;
    }

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
