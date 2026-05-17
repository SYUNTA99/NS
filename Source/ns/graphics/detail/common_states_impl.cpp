#include "ns/graphics/common_states.h"

#include "ns/graphics/detail/d3d_context.h"

#include <CommonStates.h>

namespace ns::graphics
{

    struct CommonStates::Impl
    {
        std::unique_ptr<DirectX::CommonStates> states;
    };

    CommonStates::CommonStates(void* device) : m_pImpl(std::make_unique<Impl>())
    {
        auto* dev = static_cast<ID3D11Device*>(device);
        if (dev != nullptr)
        {
            m_pImpl->states = std::make_unique<DirectX::CommonStates>(dev);
        }
    }

    CommonStates::~CommonStates() = default;

    void* CommonStates::Opaque() const noexcept
    {
        return m_pImpl->states ? static_cast<void*>(m_pImpl->states->Opaque()) : nullptr;
    }

    void* CommonStates::AlphaBlend() const noexcept
    {
        return m_pImpl->states ? static_cast<void*>(m_pImpl->states->AlphaBlend()) : nullptr;
    }

    void* CommonStates::DepthDefault() const noexcept
    {
        return m_pImpl->states ? static_cast<void*>(m_pImpl->states->DepthDefault()) : nullptr;
    }

    void* CommonStates::DepthNone() const noexcept
    {
        return m_pImpl->states ? static_cast<void*>(m_pImpl->states->DepthNone()) : nullptr;
    }

    void* CommonStates::CullCounterClockwise() const noexcept
    {
        return m_pImpl->states ? static_cast<void*>(m_pImpl->states->CullCounterClockwise()) : nullptr;
    }

    void* CommonStates::CullClockwise() const noexcept
    {
        return m_pImpl->states ? static_cast<void*>(m_pImpl->states->CullClockwise()) : nullptr;
    }

    void* CommonStates::LinearWrap() const noexcept
    {
        return m_pImpl->states ? static_cast<void*>(m_pImpl->states->LinearWrap()) : nullptr;
    }

    void* CommonStates::LinearClamp() const noexcept
    {
        return m_pImpl->states ? static_cast<void*>(m_pImpl->states->LinearClamp()) : nullptr;
    }

    void* CommonStates::PointWrap() const noexcept
    {
        return m_pImpl->states ? static_cast<void*>(m_pImpl->states->PointWrap()) : nullptr;
    }

    void* CommonStates::PointClamp() const noexcept
    {
        return m_pImpl->states ? static_cast<void*>(m_pImpl->states->PointClamp()) : nullptr;
    }

} // namespace ns::graphics
