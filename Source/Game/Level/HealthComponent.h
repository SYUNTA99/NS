#pragma once

#include "Runtime/Object/Component.h"

namespace NS::Game::Level
{
    /// @brief プレイヤーの命。残量の所有と増減の能力だけ持ち、誰に削られるかは知らない
    /// @details hazard / KillZone 等のルール配置物が ApplyDamage / Kill を呼ぶ
    /// 死んだ後どうするかは RespawnerComponent が IsDead を読んで決める。OnUpdate は持たない
    class HealthComponent : public NS::Object::Component
    {
    public:
        HealthComponent() noexcept;

        /// amount だけ削る。下限 0。既に 0 か amount が 0 以下なら何もしない
        void ApplyDamage(int amount) noexcept;

        /// 即死。残量を 0 にする
        void Kill() noexcept { m_current = 0; }

        /// 満タンへ戻す。プレイ突入とリスタートで呼ぶ
        void Reset() noexcept { m_current = m_maxHealth; }

        [[nodiscard]] bool IsDead() const noexcept { return m_current <= 0; }
        [[nodiscard]] int Current() const noexcept { return m_current; }

        NS_REFLECT_BEGIN(HealthComponent, NS::Object::Component)
        NS_REFLECT_FIELD(m_maxHealth, "Health")
        NS_REFLECT_END()

    private:
        int m_maxHealth = 8; // 8 段階。hazard 接触 1 回で 1 減る
        int m_current = 8;   // プレイ中の残量。保存せず、プレイ突入とリスタートで満タンに戻る
    };
} // namespace NS::Game::Level
