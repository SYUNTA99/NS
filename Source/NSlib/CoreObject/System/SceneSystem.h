#pragma once

#include "NSlib/Core/NonCopyable.h"

namespace NS
{
    //! @brief シーンが持つ管理役の共通基底
    //! @details シーンへ管理役を作る口は、この型の派生だけを受ける
    class SceneSystem : public NonCopyable
    {
    public:
        virtual ~SceneSystem() noexcept = default;

        //! 登録した段の SceneActor の後に登録順で呼ぶ。シミュレーション停止中は呼ばない
        virtual void OnTick() {}
    };
} // namespace NS
