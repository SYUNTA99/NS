#pragma once

#include "Runtime/Object/Component.h"
#include "Runtime/Object/Scene/SceneData.h"

namespace NS::Game::Level
{
    /// @brief 触れたプレイヤーを即死させる配置物の印
    /// @details 奈落の下へ大きな体積で置き、落下死をレベルのデータとして表す
    /// 当たり箱は Is Trigger にして置く。固形だと落ちてきたプレイヤーが上面に着地してしまう
    /// 自分の BoxCollider とプレイヤーカプセルの重なりを LateUpdate 帯で自分で判定し、触れたら player の Kill を呼ぶ
    /// 死んだ後どうするかは知らない。プレイ中しか帯更新が回らないため編集中は何もしない
    class KillZoneComponent : public NS::Object::Component
    {
    public:
        KillZoneComponent() noexcept;

        void OnUpdate() override;

        // 調整できるフィールドは無いが、 反射 typeName を持たせて type と空 fields で直列化できるようにする
        NS_REFLECT_NONE(KillZoneComponent, NS::Object::Component)
    };

    /// 即死体積の印を持つ配置物か
    [[nodiscard]] bool IsKillZoneObject(const NS::Object::ObjectData& object) noexcept;

    /// 奈落用の即死体積の ObjectData を作る。落下死をレベルのデータとして持たせる
    [[nodiscard]] NS::Object::ObjectData MakeKillZoneObject();

    /// 即死体積が 1 つも無ければ奈落の受け皿を敷き、永続 id まで振る
    /// 無いレベルは奈落で死ねず落ち続けてしまうので、読込のたびに通す
    [[nodiscard]] bool EnsureKillZoneObject(NS::Object::SceneData& level);
} // namespace NS::Game::Level
