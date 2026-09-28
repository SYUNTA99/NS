#pragma once

#include "Game/Level/HitTier.h"
#include "Runtime/Core/Math.h"
#include "Runtime/Object/Component.h"

namespace NS::Game::Level
{
    //! @brief HitZones::Judge の結果
    struct HitZoneJudgement
    {
        HitTier tier = HitTier::Wide; //!< 突進の線が通った段
        float offset01 = 1.0f;        //!< 相手の体の中心から線までの水平の距離 ÷ 届く幅。0〜1 に丸めた威力の入力
        float ratio = 1.0f;           //!< 丸める前の同じ比。1 を超える相手は、線を進む自機の縁が届かない
        float along = 0.0f;           //!< 線の起点から相手の体の中心までの、線に沿った水平の距離 (m)。後ろは負
        NS::Core::Vector3 linePoint;  //!< 相手の体の中心に一番近い線の上の点を、中心の高さに置いた物
    };

    //! @brief エディタが描く段の範囲の円
    struct ZoneRings
    {
        NS::Core::Vector3 center;  //!< 相手の体の中心。世界座標
        float centerRadius = 0.0f; //!< 真ん中の円の半径 (m)。拡縮込み
        float nearRadius = 0.0f;   //!< 惜しいの円の半径 (m)。拡縮込み。段の数が 2 の時は 0
        bool orderBroken = false;  //!< 真ん中が惜しい以上
    };

    //! @brief 体当たりの相手の段の範囲
    //! @details 段は「相手のど真ん中へ突進の線を通したか」を表す。線と相手の体の中心の水平の距離を、
    //! 真ん中と惜しいの範囲 (m) と比べて決める。範囲は相手の水平の拡縮を掛けて使う
    //! 体は同じ物のぶつかる当たり判定 1 つ (FindBodyCollider)。箱か球だけを測る
    //! 持たない壊せる物は体当たりの相手にならない
    //! 依存: NS::Obj::BoxCollider, NS::Obj::SphereCollider, FindBodyCollider
    class HitZones : public NS::Obj::Component
    {
    public:
        //! @brief 突進の線から、段・横ずれ・線の通った点を出す
        //! @details 線は origin を通り direction の水平の向きへ伸びる。高さは見ない
        //! @param[in] origin 線の起点。自機の根
        //! @param[in] direction 突進の向き。縦の成分は捨てる
        //! @param[in] playerRadius 自機の半径 (m)。届く幅に足す
        //! @param[out] out 判定の結果。false の時は触らない
        //! @return 判定できた場合 true。体の当たり判定が箱か球 1 つでない場合、向きの水平の長さが 0 か有限でない場合は
        //! false
        [[nodiscard]] bool Judge(const NS::Core::Vector3& origin,
                                 const NS::Core::Vector3& direction,
                                 float playerRadius,
                                 HitZoneJudgement& out) const noexcept;

        //! @brief エディタが描く範囲を、体の中心まわりの水平の円で返す
        //! @param[out] out 円の中心と半径。false の時は触らない
        //! @return 体の当たり判定が箱か球 1 つの場合 true、それ以外の場合は false
        [[nodiscard]] bool TryGetRings(ZoneRings& out) const noexcept;

        //! 段の数。2 は真ん中と外れ、3 は真ん中・惜しい・外れ
        [[nodiscard]] int TierCount() const noexcept { return m_tierCount; }
        //! 段の数を置く。2〜3 へ丸める
        void SetTierCount(int count) noexcept;
        //! 真ん中の範囲 (m)。拡縮を掛ける前
        [[nodiscard]] float CenterRadius() const noexcept { return m_centerRadius; }
        //! 真ん中の範囲を置く。負と非数は 0
        void SetCenterRadius(float radius) noexcept;
        //! 惜しいの範囲 (m)。拡縮を掛ける前。段の数が 2 の間も値は残す
        [[nodiscard]] float NearRadius() const noexcept { return m_nearRadius; }
        //! 惜しいの範囲を置く。負と非数は 0
        void SetNearRadius(float radius) noexcept;

        //! 段の数が 3 で、真ん中の範囲が惜しいの範囲以上の場合 true。エディタが警告の色に使う
        [[nodiscard]] bool IsOrderBroken() const noexcept;

        // 相手ごとに Inspector で段の数と範囲を決める
        NS_REFLECT_BEGIN(HitZones, NS::Obj::Component)
        NS_REFLECT_ACCESSOR(int, "段の数", TierCount(), SetTierCount)
        NS_REFLECT_ACCESSOR(float, "真ん中の範囲", CenterRadius(), SetCenterRadius)
        NS_REFLECT_ACCESSOR(float, "惜しいの範囲", NearRadius(), SetNearRadius)
        NS_REFLECT_END()

    private:
        // 体の中心と、線に直交する水平の軸への体の半幅を出す。体が箱か球 1 つでなければ false
        [[nodiscard]] bool TryGetBody(NS::Core::Vector3& outCenter,
                                      float normalX,
                                      float normalZ,
                                      float& outHalfWidth) const noexcept;
        // 持ち主の水平の拡縮。x と z の大きい方
        [[nodiscard]] float HorizontalScale() const noexcept;

        // 足した直後の 0.5 m は半径 0.5 m の玉で「線が玉の体を通る」と同じ。1.0 m はその倍 (2026-09-29 本人の指定)
        int m_tierCount = 3;         // 段の数
        float m_centerRadius = 0.5f; // 真ん中の範囲 (m)
        float m_nearRadius = 1.0f;   // 惜しいの範囲 (m)

        mutable bool m_warnedAmbiguousBody = false; // ぶつかる当たり判定が 2 つ以上ある警告を出したか
    };
} // namespace NS::Game::Level
