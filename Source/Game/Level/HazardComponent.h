#pragma once

#include "Runtime/Object/Component.h"

namespace NS::Game::Level
{
    /// @brief 触れている間プレイヤーの Health を削る配置物の印
    /// @details 自分の BoxCollider とプレイヤーカプセルの重なりを LateUpdate 帯で自分で判定して削る
    /// 当たり箱は Is Trigger にして置く。固形だと中に入れず削れない
    /// 削られた後どうなるかは知らない。プレイ中しか帯更新が回らないため編集中は何もしない
    class HazardComponent : public NS::Object::Component
    {
    public:
        HazardComponent() noexcept;

        void OnUpdate() override;

        // 調整できるフィールドは無いが、 反射 typeName を持たせて type と空 fields で直列化できるようにする
        NS_REFLECT_NONE(HazardComponent, NS::Object::Component)
    };
} // namespace NS::Game::Level
