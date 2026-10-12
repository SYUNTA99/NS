#pragma once

#include "NSlib/Core/NonCopyable.h"

namespace NS::Obj
{
    //! @brief シーンが持つ管理役の共通基底
    //! @details シーンへ管理役を作る口は、この型の派生だけを受ける
    class SceneSystem : public NS::NonCopyable
    {
    public:
        virtual ~SceneSystem() noexcept = default;

        //! @brief 登録した段で 1 固定ステップに 1 回呼ばれる。既定は何もしない
        //! @details 登録は Scene::AddSystemTick。同じ段の Actor の後に登録順で動く
        //! シミュレーションが止まっている間は呼ばれない
        virtual void OnTick() {}
    };
} // namespace NS::Obj
