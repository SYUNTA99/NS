#pragma once

#include "Runtime/Object/Component.h"
#include "Runtime/Object/Scene/SceneData.h"

namespace NS::Game::Level
{
    /// @brief 触れたらクリアになるゴールの配置物
    /// @details プレイヤーとの中心距離を LateUpdate 帯で自分で判定し、触れたら旗を立てて保持する
    /// 旗を読んでどう応答するかは FinisherComponent が決める。旗は走行のやり直しと編集復帰で戻される
    class GoalComponent : public NS::Object::Component
    {
    public:
        /// 「触れた」とみなす player 中心からのメートル距離
        static constexpr float k_GoalRadius = 0.9f;

        GoalComponent() noexcept;

        void OnUpdate() override;

        [[nodiscard]] bool Reached() const noexcept { return m_reached; }

        /// 旗を戻す。走行のやり直し (respawner) と編集へ戻る時 (エディタ) に呼ばれる
        void ResetReached() noexcept { m_reached = false; }

        // 調整できるフィールドは無いが、 反射 typeName を持たせて type と空 fields で直列化できるようにする
        NS_REFLECT_NONE(GoalComponent, NS::Object::Component)

    private:
        bool m_reached = false; // 触れたら立つ。保存しない
    };

    /// ゴールの印を持つ配置物か。表示と固形判定が同じ契約を読む
    [[nodiscard]] bool IsGoalObject(const NS::Object::ObjectData& object) noexcept;
} // namespace NS::Game::Level
