#pragma once

#include "Runtime/Object/Actor.h"

namespace NS::Obj
{
    //! @brief 動かない地形の部品。床・壁・足場・飾り・メッシュの地形
    //! @details 見た目の MeshRenderer と、その三角形で当たる MeshCollider を持つ。当たりの形は見た目のメッシュが決め、
    //! 立方体・球・坂・取り込んだ地形の違いは、個体のメッシュの参照の違いだけになる
    //! 動かないので自分の振る舞いを持たない。動く足場のような固有の動きを持つ物は別のクラスにする
    class MapParts : public Actor
    {
    public:
        MapParts() noexcept;

        //! 保存形式と TypeRegistry の登録名。読込はこの名前で Actor の型を選ぶ
        [[nodiscard]] const char* ClassName() const noexcept override { return "MapParts"; }
    };
} // namespace NS::Obj
