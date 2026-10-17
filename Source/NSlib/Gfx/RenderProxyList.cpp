#include "NSlib/Gfx/RenderProxyList.h"
#include "NSlib/Core/Types.h"

#include "NSlib/Core/AABB.h"
#include "NSlib/Core/Frustum.h"
#include "NSlib/Gfx/RenderContext.h"

#include <algorithm>

namespace NS
{
    namespace Gfx
    {
        RenderHandle RenderProxyList::Register(const RenderProxyDesc& desc)
        {
            uint32 slot = static_cast<uint32>(m_proxies.size());
            if (!m_freeSlots.empty())
            {
                slot = m_freeSlots.back();
                m_freeSlots.pop_back();
            }
            else
            {
                m_proxies.push_back(Proxy{});
            }
            Proxy& p = m_proxies[slot];
            p.desc = desc;
            p.alive = true;
            // 使い回す枠の generation は Unregister で進めた値のまま。古いハンドルを無効にする
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
                                     const AABB& bounds,
                                     const Vector3& sortCenter,
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
            const Frustum frustum = Frustum::FromViewProjection(context.viewProjection);

            m_visibleScratch.clear();
            for (uint32 slot = 0; slot < m_proxies.size(); ++slot)
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
                const Vector3 camPos = context.cameraPosition;
                // 距離も優先度も同じ物はスロット順のまま残すため stable_sort
                std::stable_sort(m_visibleScratch.begin(),
                                 m_visibleScratch.end(),
                                 [this, camPos](uint32 a, uint32 b) noexcept {
                                     const float da = (m_proxies[a].desc.sortCenter - camPos).LengthSquared();
                                     const float db = (m_proxies[b].desc.sortCenter - camPos).LengthSquared();
                                     if (da != db)
                                     {
                                         return da > db; // 遠い順に描かないと半透明の重なりが崩れる
                                     }
                                     return m_proxies[a].desc.sortPriority < m_proxies[b].desc.sortPriority;
                                 });
            }

            for (const uint32 slot : m_visibleScratch)
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
            return static_cast<std::size_t>(std::ranges::count_if(m_proxies, [](const Proxy& p) { return p.alive; }));
        }

        bool RenderProxyList::IsLive(RenderHandle handle) const noexcept
        {
            return handle.slot < m_proxies.size() && m_proxies[handle.slot].alive &&
                   m_proxies[handle.slot].generation == handle.generation;
        }
    } // namespace Gfx
} // namespace NS
