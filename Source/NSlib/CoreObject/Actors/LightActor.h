#pragma once

#include "NSlib/CoreObject/SceneActor.h"

namespace NS
{
    //! シーンを照らす平行光。基底の平行光 (LightClassObj) を作り、向きを根の回転で決める
    class LightActor : public SceneActor
    {
    public:
        //! 保存形式と TypeRegistry の登録名。読込はこの名前で SceneActor の型を選ぶ
        NS_REFLECT_NONE(LightActor, SceneActor)

    protected:
        void OnInit() override;
    };
} // namespace NS
