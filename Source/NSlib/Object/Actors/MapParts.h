#pragma once

#include "NSlib/Object/Actor.h"

namespace NS::Obj
{
    //! Model の形状を MeshCollision に使う静的な地形。個体ごとの形はメッシュ参照で選ぶ
    //! 動く足場など、固有の振る舞いを持つ地形は別のクラスにする
    class MapParts : public Actor
    {
    public:
        //! 保存形式と TypeRegistry の登録名。読込はこの名前で Actor の型を選ぶ
        NS_REFLECT_NONE(MapParts, NS::Obj::Actor)

    protected:
        void OnInit() override;
    };
} // namespace NS::Obj
