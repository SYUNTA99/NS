#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/Gfx/Render/RenderSettings.h"

#include <cmath>

namespace NS::Gfx
{
        class D3DDevice;
        class ScreenOverlay;

        //! 上向きの面に広がる光の輪。形状は変えず、画素シェーダーで表現する
        struct GroundWave
        {
            float centerX = 0.0f;  //!< 中心の世界の x
            float centerZ = 0.0f;  //!< 中心の世界の z
            float radius = 0.0f;   //!< 輪の半径 (m)
            float strength = 0.0f; //!< 輪の強さ。0 以下なら出さない
        };

        //! @brief 歪みの輪。世界の点を中心に、画面の上で広がる輪の所の絵を外へ押し出す
        //! @details 長さはどれも描画先の高さに対する割合。光のにじみが世界の絵を書き戻す時に掛ける
        struct DistortionRing
        {
            Vector3 center{};   //!< 中心の世界の点
            float radius = 0.0f;    //!< 輪の半径
            float push = 0.0f;      //!< 輪の真ん中で絵を押し出す長さ。0 以下なら歪めない
            float halfWidth = 0.0f; //!< 輪の半分の幅
        };

        //! @brief 1フレームの描画処理全体で共有されるコンテキスト情報
        struct RenderContext
        {
            D3DDevice* device = nullptr;

            //! 重ね描きと UI が、全画面の色と画面の矩形を描く先。無ければ描かない
            ScreenOverlay* overlay = nullptr;

            //! カメラのビュープロジェクション行列
            Matrix viewProjection{};

            //! カメラのワールド座標
            Vector3 cameraPosition{};

            //! 固定ステップ更新と描画のズレを埋める補間係数 (0.0 〜 1.0)
            float alpha = 1.0f;

            //! シーン全体に適用される共通の描画設定
            RenderSettings resolvedSettings{};

            //! このフレームの床の波。強さ 0 なら出さない
            GroundWave groundWave{};

            //! このフレームの歪みの輪。押しが 0 なら歪めない
            DistortionRing distortionRing{};
        };

        //! 世界座標を描画先の左上原点の画素座標へ投影する。width・height は画素数
        //! カメラの後ろなら false で出力を変更しない。outW は投影後の奥行き係数
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

        //! projectedW の奥行きで、世界座標の長さを画素数へ変換する。カメラの回転に依存しない
        //! projectedW は TryProjectToPixels の正の出力、height は描画先の高さの画素数
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
} // namespace NS::Gfx
