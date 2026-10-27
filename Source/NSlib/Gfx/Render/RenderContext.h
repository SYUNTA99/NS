#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/Gfx/Render/RenderSettings.h"

#include <cmath>

namespace NS
{
    namespace Gfx
    {
        class D3DDevice;
        class ScreenOverlay;

        //! 上向きの面に広がる光の輪 (ピクセルシェーダーで表現)
        struct GroundWave
        {
            float centerX = 0.0f;  // 中心のワールド X 座標
            float centerZ = 0.0f;  // 中心のワールド Z 座標
            float radius = 0.0f;   // 半径 (m)
            float strength = 0.0f; // 強さ (0 以下で無効)
        };

        //! ワールド座標の点を中心に画面上で広がる歪みの輪 (各長さは描画先の高さに対する比率)
        struct DistortionRing
        {
            Vector3 center{};       // 中心のワールド座標
            float radius = 0.0f;
            float push = 0.0f;      // 押し出し量 (0 以下で無効)
            float halfWidth = 0.0f;
        };

        struct RenderContext
        {
            D3DDevice* device = nullptr;
            ScreenOverlay* overlay = nullptr; // nullptr の場合はオーバーレイ・UI を描画しない
            Matrix viewProjection{};
            Vector3 cameraPosition{};
            float alpha = 1.0f; // 固定ステップ更新と描画の補間係数 (0.0 〜 1.0)
            RenderSettings resolvedSettings{};
            GroundWave groundWave{};
            DistortionRing distortionRing{};
        };

        //! ワールド座標を描画先の左上原点ピクセル座標へ投影する (width, height はピクセル数)
        //! カメラ背後の場合は false を返し出力を変更しない。outW は投影後のクリップ空間 W
        [[nodiscard]] inline bool TryProjectToPixels(const Matrix& viewProjection,
                                                     const Vector3& point,
                                                     float width,
                                                     float height,
                                                     Vector2& outPixel,
                                                     float& outW) noexcept
        {
            const Vector4 clip =
                Vector4::Transform(Vector4{point.x, point.y, point.z, 1.0f}, viewProjection);
            if (!IsPositive(clip.w))
            {
                return false;
            }
            const float ndcX = clip.x / clip.w;
            const float ndcY = clip.y / clip.w;
            outPixel = Vector2{(ndcX + 1.0f) * 0.5f * width, (1.0f - ndcY) * 0.5f * height};
            outW = clip.w;
            return true;
        }

        //! TryProjectToPixels が返した正の projectedW におけるワールド空間の長さ length をピクセル数へ変換する
        [[nodiscard]] inline float ProjectedLengthPixels(const Matrix& viewProjection,
                                                         float length,
                                                         float projectedW,
                                                         float height) noexcept
        {
            const float verticalScale =
                std::sqrt(viewProjection._12 * viewProjection._12 + viewProjection._22 * viewProjection._22 +
                          viewProjection._32 * viewProjection._32);
            return length * verticalScale / projectedW * height * 0.5f;
        }
    } // namespace Gfx
} // namespace NS
