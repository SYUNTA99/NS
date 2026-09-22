#include "Runtime/Graphics/RenderProxyList.h"

#include "Runtime/Core/AABB.h"
#include "Runtime/Core/Frustum.h"
#include "Runtime/Graphics/RenderContext.h"

#include <algorithm>

namespace NS::Gfx
{
    RenderHandle RenderProxyList::Register(const RenderProxyDesc& desc)
    {
        if (!m_freeSlots.empty())
        {
            const std::uint32_t slot = m_freeSlots.back();
            m_freeSlots.pop_back();
            Proxy& p = m_proxies[slot];
            p.desc = desc;
            p.alive = true;
            // generation は Unregister で進めた値を使い回し、古いハンドルを無効にする
            return RenderHandle{slot, p.generation};
        }

        const std::uint32_t slot = static_cast<std::uint32_t>(m_proxies.size());
        Proxy p{};
        p.desc = desc;
        p.alive = true;
        m_proxies.push_back(p);
        return RenderHandle{slot, p.generation};
    }

    void RenderProxyList::Unregister(RenderHandle handle) noexcept
    {
        if (!IsLive(handle))
        {
            return;
        }
        Proxy& p = m_proxies[handle.slot];
        p.alive = false;
        ++p.generation; // 外に残った古いハンドルを世代不一致で弾く
        p.desc = RenderProxyDesc{};
        m_freeSlots.push_back(handle.slot);
    }

    void RenderProxyList::Update(RenderHandle handle,
                                 const NS::Core::AABB& bounds,
                                 const NS::Core::Vector3& sortCenter,
                                 int sortPriority,
                                 bool transparent) noexcept
    {
        if (!IsLive(handle))
        {
            return;
        }
        Proxy& p = m_proxies[handle.slot];
        p.desc.bounds = bounds;
        p.desc.sortCenter = sortCenter;
        p.desc.sortPriority = sortPriority;
        p.desc.transparent = transparent;
    }

    void RenderProxyList::DrawBucket(const RenderContext& context, bool transparent)
    {
        const NS::Core::Frustum frustum = NS::Core::Frustum::FromViewProjection(context.viewProjection);

        m_visibleScratch.clear();
        for (std::uint32_t slot = 0; slot < m_proxies.size(); ++slot)
        {
            const Proxy& p = m_proxies[slot];
            if (!p.alive || p.desc.transparent != transparent)
            {
                continue;
            }
            if (!frustum.Intersects(p.desc.bounds))
            {
                continue; // 視錐台の外は collect も発行もしない
            }
            m_visibleScratch.push_back(slot);
        }

        if (transparent)
        {
            const NS::Core::Vector3 camPos = context.cameraPosition;
            // 距離も優先度も同じ物はスロット順のまま残すため stable_sort
            std::stable_sort(m_visibleScratch.begin(),
                             m_visibleScratch.end(),
                             [this, camPos](std::uint32_t a, std::uint32_t b) noexcept {
                                 const float da = (m_proxies[a].desc.sortCenter - camPos).LengthSquared();
                                 const float db = (m_proxies[b].desc.sortCenter - camPos).LengthSquared();
                                 if (da != db)
                                 {
                                     return da > db; // 遠い順に描かないと半透明の重なりが崩れる
                                 }
                                 return m_proxies[a].desc.sortPriority < m_proxies[b].desc.sortPriority;
                             });
        }

        for (const std::uint32_t slot : m_visibleScratch)
        {
            const Proxy& p = m_proxies[slot];
            if (p.desc.collect == nullptr)
            {
                continue;
            }
            m_drawScratch.clear();
            p.desc.collect(p.desc.owner, context, m_drawScratch);
            if (context.renderer != nullptr)
            {
                for (const DrawItem& item : m_drawScratch)
                {
                    IssueDrawItem(*context.renderer, item);
                }
            }
        }
    }

    std::size_t RenderProxyList::Count() const noexcept
    {
        std::size_t n = 0;
        for (const Proxy& p : m_proxies)
        {
            if (p.alive)
            {
                ++n;
            }
        }
        return n;
    }

    bool RenderProxyList::IsLive(RenderHandle handle) const noexcept
    {
        return handle.slot < m_proxies.size() && m_proxies[handle.slot].alive &&
               m_proxies[handle.slot].generation == handle.generation;
    }

} // namespace NS::Gfx