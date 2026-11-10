#pragma once

#include "NSlib/Core/AABB.h"
#include "NSlib/Core/Math.h"
#include "NSlib/Gfx/Render/DrawItem.h"

namespace NS
{
    namespace Gfx
    {
        //! 描く物 1 つを、どの段でどの順に描き、視錐台で間引くかを決める値
        struct RenderBounds
        {
            AABB bounds{};        // 視錐台で間引くための世界の箱
            Vector3 sortCenter{}; // 半透明の段で、カメラから遠い順に並べる時の中心
            int sortPriority = 0; // 半透明の段で中心の距離が同じ時の優先度。小さいほど先に描く
            RenderPass pass = RenderPass::Opaque;
        };
    } // namespace Gfx
} // namespace NS
