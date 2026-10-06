#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/Graphics/RenderSettings.h"

#include <cmath>

namespace NS::Gfx
{
    class Renderer;

    //! @brief 床の波。上を向いた面に、中心からの水平の距離が半径の所を走る光の輪を出す
    //! @details 床は頂点の少ない箱なので形は曲げず、画素のシェーダーが光と面の向きだけを変える。
    //! 式は Shaders/Common.hlsli の GroundWaveNormal と GroundWaveGlow が持つ
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
        NS::Vector3 center{}; //!< 中心の世界の点
        float radius = 0.0f;        //!< 輪の半径
        float push = 0.0f;          //!< 輪の真ん中で絵を押し出す長さ。0 以下なら歪めない
        float halfWidth = 0.0f;     //!< 輪の半分の幅
    };

    //! @brief 1フレームの描画処理全体で共有されるコンテキスト情報
    struct RenderContext
    {
        //! 描画先のレンダラー
        Renderer* renderer = nullptr;

        //! カメラのビュープロジェクション行列
        NS::Matrix viewProjection{};

        //! カメラのワールド座標
        NS::Vector3 cameraPosition{};

        //! 固定ステップ更新と描画のズレを埋める補間係数 (0.0 〜 1.0)
        float alpha = 1.0f;

        //! シーン全体に適用される共通の描画設定
        RenderSettings resolvedSettings{};

        //! このフレームの床の波。強さ 0 なら出さない
        GroundWave groundWave{};

        //! このフレームの歪みの輪。押しが 0 なら歪めない
        DistortionRing distortionRing{};
    };

    //! @brief 世界の点を描画先の画素 (左上が原点) へ投げる
    //! @param[in] viewProjection カメラのビュープロジェクション行列
    //! @param[in] point 投げる世界の点
    //! @param[in] width 描画先の幅の画素
    //! @param[in] height 描画先の高さの画素
    //! @param[out] outPixel 投げた画素。false の時は書かない
    //! @param[out] outW 投げた w。奥行きで大きさを割る時に使う。false の時は書かない
    //! @return 投げられた場合 true。カメラの後ろ (投げた w が 0 以下) なら false
    [[nodiscard]] inline bool TryProjectToPixels(const NS::Matrix& viewProjection,
                                                 const NS::Vector3& point,
                                                 float width,
                                                 float height,
                                                 NS::Vector2& outPixel,
                                                 float& outW) noexcept
    {
        const NS::Vector4 clip =
            NS::Vector4::Transform(NS::Vector4{point.x, point.y, point.z, 1.0f}, viewProjection);
        if (!(clip.w > 0.0f))
        {
            return false;
        }
        const float ndcX = clip.x / clip.w;
        const float ndcY = clip.y / clip.w;
        outPixel = NS::Vector2{(ndcX + 1.0f) * 0.5f * width, (1.0f - ndcY) * 0.5f * height};
        outW = clip.w;
        return true;
    }

    //! @brief 投げた点と同じ奥行きの面の上の世界の長さを、描画先の画素の長さにする
    //! @details 行列の縦の成分のうち世界の x・y・z に掛かる 3 つの長さは、ビューの回転で変わらず射影の縦の倍率になる。
    //! カメラの向きで大きさが変わらない
    //! @param[in] viewProjection カメラのビュープロジェクション行列
    //! @param[in] length 世界の長さ
    //! @param[in] projectedW TryProjectToPixels が返した w。0 より大きい
    //! @param[in] height 描画先の高さの画素
    //! @return 画素の長さ
    [[nodiscard]] inline float ProjectedLengthPixels(const NS::Matrix& viewProjection,
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