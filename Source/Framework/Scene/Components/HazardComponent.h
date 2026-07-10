#pragma once

/// @file HazardComponent.h
/// @brief 接触ダメージ Component
///
/// @details 触れている間プレイヤーへダメージを与える trigger marker。 削り量や死亡判定の
/// ルールは Play 側が持ち、 本 Component は印として存在するだけにする。 SceneData は
/// 一切変更せず CRC32 round-trip 互換を保つ

#include "Framework/Scene/Component.h"

namespace NS::Scene
{
    /// 触れたプレイヤーにダメージを与える trigger Component。削り量と死亡判定は Play 側、描画・衝突応答は他 Component
    /// が担う
    class HazardComponent : public Component
    {
    public:
        HazardComponent() noexcept;

        // 調整できるフィールドは無いが、 反射 typeName を持たせて type と空 fields で直列化できるようにする
        NS_REFLECT_NONE(HazardComponent, Component)
    };
} // namespace NS::Scene
