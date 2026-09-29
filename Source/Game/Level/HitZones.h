#pragma once

#include "Game/Level/HitTier.h"
#include "Runtime/Core/Math.h"
#include "Runtime/Object/Component.h"

namespace NS::Game::Level
{
    //! @brief HitZones::Judge の結果
    struct HitZoneJudgement
    {
        HitTier tier = HitTier::Wide; //!< 当たった色の段。どの色にも入らなければ外れ
        float powerScale = 1.0f;      //!< 当たった色の威力の倍率。どの色にも入らなければ残りの威力の倍率
        float u = 0.0f;               //!< 面の上の左右の位置。自機から見て右が正。-1〜1 が触れられる幅
        float v = 0.0f;               //!< 面の上の上下の位置。上が正。-1〜1 が触れられる高さ
        float offset01 = 1.0f;        //!< 相手の体の中心から線までの水平の距離 ÷ 届く幅。0〜1 に丸めた値
        float ratio = 1.0f;           //!< 丸める前の同じ比。1 を超える相手は、線を進む自機の縁が届かない
        float along = 0.0f;           //!< 線の起点から相手の体の中心までの、線に沿った水平の距離 (m)。後ろは負
        NS::Core::Vector3 linePoint;  //!< 相手の体の中心に一番近い線の上の点を、中心の高さに置いた物
        //! 自機の玉が相手の表面に触れる点。線が届かない時は、線に一番近い表面の点
        NS::Core::Vector3 surfacePoint;
    };

    //! @brief 体当たりの相手の段の面
    //! @details 段は「相手のどこに当てたか」を表す。面は相手の前に立ち、いつも自機が来る向きを向く
    //! 面の上の位置は、相手の体の中心から突進の線までの左右と上下のずれを、左右は「半幅 + 自機の半径」、上下は
    //! 「半分の高さ + 自機の半径」で割った物。その位置を覆う色 (同じ物に積んだ HitZoneArea) で段と威力の倍率を決める
    //! 気持ちいいの色が外れの色より先。どの色にも入らない所は外れ
    //! 体は同じ物のぶつかる当たり判定 1 つ (FindBodyCollider)。箱か球だけを測る
    //! 持たない壊せる物は体当たりの相手にならない
    //! 依存: HitZoneArea, NS::Obj::BoxCollider, NS::Obj::SphereCollider, FindBodyCollider
    class HitZones : public NS::Obj::Component
    {
    public:
        //! @brief 突進の線から、段・威力の倍率・面の上の位置・線の通った点・表面に触れる点を出す
        //! @details 線は origin を通り、origin の高さのまま direction の水平の向きへ伸びる直線
        //! 面の上の位置の上下は origin の高さで測る。表面に触れる点は、自機の玉の中心が相手の中心から
        //! 「表面の半径 + playerRadius」の球に入る所の向きで出す
        //! @param[in] origin 線の起点。自機の玉の中心 (丸まっている間は根と同じ)
        //! @param[in] direction 突進の向き。縦の成分は捨てる
        //! @param[in] playerRadius 自機の半径 (m)。届く幅と高さに足す
        //! @param[out] out 判定の結果。false の時は触らない
        //! @return 判定できた場合 true。体の当たり判定が箱か球 1 つでない場合、向きの水平の長さが 0 か有限でない場合は
        //! false
        [[nodiscard]] bool Judge(const NS::Core::Vector3& origin,
                                 const NS::Core::Vector3& direction,
                                 float playerRadius,
                                 HitZoneJudgement& out) const noexcept;

        //! どの色にも入らない所 (外れ) の当たり位置の係数
        [[nodiscard]] float RemainderPowerScale() const noexcept { return m_remainderPowerScale; }
        //! どの色にも入らない所の当たり位置の係数を置く。負は 0。非数と無限は 0
        void SetRemainderPowerScale(float scale) noexcept;

        // 相手ごとに Inspector で決める。色は HitZoneArea を足して決める
        NS_REFLECT_BEGIN(HitZones, NS::Obj::Component)
        NS_REFLECT_ACCESSOR(float, "残りの威力の倍率", RemainderPowerScale(), SetRemainderPowerScale)
        NS_REFLECT_END()

    private:
        // 体の中心、線に直交する水平の軸への体の半幅、縦の半分の高さ、表面の半径を出す。体が箱か球 1 つでなければ
        // false。表面の半径は、球はその半径、箱は外接球の半径
        [[nodiscard]] bool TryGetBody(NS::Core::Vector3& outCenter,
                                      float normalX,
                                      float normalZ,
                                      float& outHalfWidth,
                                      float& outHalfHeight,
                                      float& outSurfaceRadius) const noexcept;

        // 今までの「突進位置係数カーブ」の端の値。かすめた当たりの威力を 3 割落とす
        float m_remainderPowerScale = 0.7f; // 残りの当たり位置の係数

        mutable bool m_warnedAmbiguousBody = false; // ぶつかる当たり判定が 2 つ以上ある警告を出したか
    };
} // namespace NS::Game::Level
