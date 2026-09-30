#pragma once

#include "Runtime/Object/Component.h"

namespace NS::Game::Level
{
    //! @brief KillZone の Actor が持つ、触れたプレイヤーを即死させる部品
    //! @details 奈落の下へ大きな体積で置き、落下死をレベルのデータとして表す
    //! 当たり箱は Is Trigger にして置く。固形だと落ちてきたプレイヤーが上面に着地してしまう
    //! 自分の BoxCollider とプレイヤーカプセルの重なりを LateUpdate 帯で自分で判定し、触れたら player の Kill を呼ぶ
    //! 死んだ後どうするかは知らない。プレイ中しか帯更新が回らないため編集中は何もしない
    class KillZoneComponent : public NS::Obj::Component
    {
    public:
        KillZoneComponent() noexcept;

        void OnUpdate() override;

        // 調整できるフィールドは無いが、リフレクション typeName を持たせて type と空 fields で直列化できるようにする
        NS_REFLECT_NONE(KillZoneComponent, NS::Obj::Component)
    };
} // namespace NS::Game::Level
