#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/Gfx/Render/RenderSettings.h"

#include <array>

namespace NS
{
    namespace Gfx
    {
        //! ビューごとの定数 (Common.hlsli の cbuffer ViewCB とレイアウト一致、行優先・左手系)
        //! SceneRenderer が 1 本持ち、ビューを描き始める時に 1 回送って k_ViewConstantsSlot に差す
        struct alignas(16) ViewConstants
        {
            Matrix viewProj{};
            // 照明の既定値はプロジェクト描画既定値の RenderSettings と共有し、値の二重管理を避ける
            Vector3 lightDir = Gfx::RenderSettings{}.lightDir;
            float lightDirPad = 0.0f;
            Vector3 lightColor = Gfx::RenderSettings{}.lightColor;
            float lightColorPad = 0.0f;
            Vector3 ambientColor = Gfx::RenderSettings{}.ambientColor;
            float ambientColorPad = 0.0f;
            Vector3 groundColor = Gfx::RenderSettings{}.groundColor;
            float exposure = Gfx::RenderSettings{}.exposure;
            // 名前の無いシーンの値 2 つ。意味は SceneRenderer の SetSceneValue で書く物と、シェーダの口のファイル SurfaceHooks.hlsli が決める
            std::array<Vector4, 2> sceneValues{};
            Vector3 cameraPosition{};
            float sceneTimeSeconds = 0.0f;
        };
        static_assert(sizeof(ViewConstants) == 176, "ViewConstants の大きさは Common.hlsli の ViewCB と同じ 176 バイト");
        static_assert(alignof(ViewConstants) == 16, "ViewConstants は 16 バイト境界");

        //! 物ごとの定数 (Common.hlsli の cbuffer ObjectCB とレイアウト一致、行優先・左手系)
        //! 材質ごとの定数バッファに描く 1 回ごとに送って k_DrawConstantsSlot に差す
        struct alignas(16) ObjectConstants
        {
            Matrix world{};
            Vector3 baseColor{1.0f, 1.0f, 1.0f};
            float opacity = 1.0f; // 不透明度 0..1 (lit / unlit / soft_circle が読む。Alpha ブレンド時のみ有効)
            // 名前の無い物ごとの 4 つの値。意味は Model の SetDrawValues で書く物と、シェーダの口のファイル SurfaceHooks.hlsli が決める
            std::array<Vector4, 4> objectValues{};
            // ビューごとのシーンの値に掛ける重み。1 なら効き、0 なら効かない (残像のコピーは 0)
            float sceneValuesWeight = 1.0f;
            float pad0 = 0.0f;
            float pad1 = 0.0f;
            float pad2 = 0.0f;
        };
        static_assert(sizeof(ObjectConstants) == 160, "ObjectConstants の大きさは Common.hlsli の ObjectCB と同じ 160 バイト");
        static_assert(alignof(ObjectConstants) == 16, "ObjectConstants は 16 バイト境界");
    } // namespace Gfx
} // namespace NS
