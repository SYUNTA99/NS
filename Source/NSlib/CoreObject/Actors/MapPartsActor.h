#pragma once

#include "NSlib/CoreObject/SceneActor.h"

namespace NS
{
    /// Model の形状を MeshCollision に使う静的な地形。個体ごとの形はメッシュ参照で選ぶ
    /// 動く足場など、固有の振る舞いを持つ地形は別のクラスにする
    class MapPartsActor : public SceneActor
    {
    public:
        /// 保存形式と TypeRegistry の登録名。読込はこの名前で SceneActor の型を選ぶ
        NS_REFLECT_NONE(MapPartsActor, SceneActor)

    protected:
        void OnInit() override;
    };
} // namespace NS
