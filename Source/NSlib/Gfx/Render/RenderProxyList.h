#pragma once

#include "NSlib/Core/AABB.h"
#include "NSlib/Core/Math.h"
#include "NSlib/Gfx/Render/DrawItem.h"

#include "NSlib/Core/Types.h"
#include <vector>

namespace NS
{
    namespace Gfx
    {
        struct RenderContext;

        struct RenderHandle
        {
            static constexpr uint32 k_InvalidSlot = 0xffffffffu;

            uint32 slot = k_InvalidSlot;
            uint32 generation = 0;

            [[nodiscard]] bool IsValid() const noexcept { return slot != k_InvalidSlot; }
        };

        struct RenderProxyDesc
        {
            AABB bounds{};        // 視錐台カリング用のワールド空間 AABB
            Vector3 sortCenter{}; // 深度ソート用のワールド座標
            int sortPriority = 0; // 半透明ソートで距離が等しい場合の優先度 (小さい順に先に描画)
            bool transparent = false;
            void (*collect)(void* owner, const RenderContext& context, std::vector<DrawItem>& out) = nullptr;
            void* owner = nullptr;
        };

        class RenderProxyList
        {
        public:
            [[nodiscard]] RenderHandle Register(const RenderProxyDesc& desc);

            void Unregister(RenderHandle handle) noexcept;

            void Update(RenderHandle handle,
                        const AABB& bounds,
                        const Vector3& sortCenter,
                        int sortPriority,
                        bool transparent) noexcept;

            /// 視錐台カリングを行い、transparent に一致するバケットを描画する
            void DrawBucket(const RenderContext& context, bool transparent);

            [[nodiscard]] std::size_t Count() const noexcept;

        private:
            struct Proxy
            {
                RenderProxyDesc desc{};
                uint32 generation = 0;
                bool alive = false;
            };

            [[nodiscard]] bool IsLive(RenderHandle handle) const noexcept;

            std::vector<Proxy> m_proxies;
            std::vector<uint32> m_freeSlots;
            std::vector<uint32> m_visibleScratch;
            std::vector<DrawItem> m_drawScratch;
        };
    } // namespace Gfx
} // namespace NS
