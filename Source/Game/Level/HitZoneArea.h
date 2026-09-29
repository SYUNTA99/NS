#pragma once

#include "Runtime/Object/Component.h"

namespace NS::Game::Level
{
    //! @brief 相手の段の面に置く色 1 つ
    //! @details 面の上の位置 (左右 u・上下 v、どちらも -1〜1) のうち、この色が覆う所を形・広さ・位置で決める
    //! 気持ちいいの色は赤、外れの色は青で描く。同じ物に積んだ HitZones が集めて段と威力を決める
    //! 色を足す・消すは、この部品を足す・消す
    class HitZoneArea : public NS::Obj::Component
    {
    public:
        //! @brief 面の上の位置がこの色に入るかを返す
        //! @details 縁ちょうどは入らない。広さが 0 の向きがある色はどこも覆わない
        //! @param[in] u 面の上の左右の位置。自機から見て右が正
        //! @param[in] v 面の上の上下の位置。上が正
        //! @return 入る場合 true、それ以外の場合は false
        [[nodiscard]] bool Contains(float u, float v) const noexcept;

        //! 気持ちいいの色か。偽なら外れの色
        [[nodiscard]] bool IsCenter() const noexcept { return m_center; }
        //! 気持ちいいの色にするかを置く
        void SetCenter(bool center) noexcept { m_center = center; }
        //! 丸か。偽なら箱
        [[nodiscard]] bool IsRound() const noexcept { return m_round; }
        //! 丸にするかを置く
        void SetRound(bool round) noexcept { m_round = round; }
        //! 面全体に対する左右の半分の幅の割合。0〜1
        [[nodiscard]] float Width() const noexcept { return m_width; }
        //! 左右の半分の幅の割合を置く。0〜1 へ丸める。非数と無限は 0
        void SetWidth(float width) noexcept;
        //! 面全体に対する上下の半分の幅の割合。0〜1
        [[nodiscard]] float Height() const noexcept { return m_height; }
        //! 上下の半分の幅の割合を置く。0〜1 へ丸める。非数と無限は 0
        void SetHeight(float height) noexcept;
        //! 色の中心の左右の位置。-1〜1、自機から見て右が正
        [[nodiscard]] float CenterU() const noexcept { return m_centerU; }
        //! 色の中心の左右の位置を置く。-1〜1 へ丸める。非数と無限は 0
        void SetCenterU(float u) noexcept;
        //! 色の中心の上下の位置。-1〜1、上が正
        [[nodiscard]] float CenterV() const noexcept { return m_centerV; }
        //! 色の中心の上下の位置を置く。-1〜1 へ丸める。非数と無限は 0
        void SetCenterV(float v) noexcept;
        //! この色で当たった時の当たり位置の係数
        [[nodiscard]] float PowerScale() const noexcept { return m_powerScale; }
        //! 当たり位置の係数を置く。負は 0。非数と無限は 0
        void SetPowerScale(float scale) noexcept;

        // 相手ごとに Inspector で色を決める
        NS_REFLECT_BEGIN(HitZoneArea, NS::Obj::Component)
        NS_REFLECT_ACCESSOR(bool, "気持ちいい", IsCenter(), SetCenter)
        NS_REFLECT_ACCESSOR(bool, "丸", IsRound(), SetRound)
        NS_REFLECT_ACCESSOR(float, "横幅", Width(), SetWidth)
        NS_REFLECT_ACCESSOR(float, "縦の幅", Height(), SetHeight)
        NS_REFLECT_ACCESSOR(float, "左右の位置", CenterU(), SetCenterU)
        NS_REFLECT_ACCESSOR(float, "上下の位置", CenterV(), SetCenterV)
        NS_REFLECT_ACCESSOR(float, "威力の倍率", PowerScale(), SetPowerScale)
        NS_REFLECT_END()

    private:
        bool m_center = true; // 気持ちいいの色か
        bool m_round = false; // 丸か
        // 前の本人の指定「真ん中 0.5 m」を、半径 0.5 の玉と半径 0.65 の自機で割合に直した値。0.5 ÷ 1.15 ≒ 0.43
        float m_width = 0.43f;  // 左右の半分の幅の割合
        float m_height = 0.43f; // 上下の半分の幅の割合
        float m_centerU = 0.0f; // 中心の左右の位置
        float m_centerV = 0.0f; // 中心の上下の位置
        // 気持ちいいの当たりは威力を減らさない。今までの「突進位置係数カーブ」の中心の値
        float m_powerScale = 1.0f; // 当たり位置の係数
    };
} // namespace NS::Game::Level
