#pragma once

/// @file PickupComponent.h
/// @brief 取得アイテムの種別だけを持つデータ Component
///
/// @details コイン / ゴールスターの「拾える意味」だけを反射フィールド 1 つで表す。 描画 / 当たりは
/// 持たない (視覚は別 Component が担い、 コイン / スターは視覚を持たないまま当たり判定だけで拾う)
/// PlayMode が owner の collider ではなく本 Component の種別を読んで拾得とクリアを評価する

#include "Framework/Scene/Component.h"

namespace NS::Scene
{
    /// 拾得アイテムの種別を保持するデータ Component。 0=コイン / 1=ゴールスター。 描画・当たりは持たない
    class PickupComponent : public Component
    {
    public:
        PickupComponent() noexcept;

        /// ゴールスター (拾得でクリア) かどうか
        [[nodiscard]] bool IsStar() const noexcept { return m_pickupKind == 1; }
        /// コイン (拾得でカウント) かどうか
        [[nodiscard]] bool IsCoin() const noexcept { return m_pickupKind == 0; }

        // 拾得種別を Inspector / 直列化へ公開する。 0=コイン / 1=ゴールスター
        NS_REFLECT_BEGIN(PickupComponent)
        NS_REFLECT_FIELD(m_pickupKind, "Pickup Kind")
        NS_REFLECT_END()

    private:
        int m_pickupKind = 0;
    };
} // namespace NS::Scene
