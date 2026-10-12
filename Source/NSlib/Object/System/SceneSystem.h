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
    };
} // namespace NS::Obj
