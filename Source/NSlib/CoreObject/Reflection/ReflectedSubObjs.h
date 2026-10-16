#pragma once

#include "NSlib/CoreObject/Reflection/Reflection.h"

#include <functional>

namespace NS
{
    class SceneActor;
    class Object;

    using SubObjFieldVisitor = std::function<void(const SubObjectDesc&, Object*)>;

    //! @brief クラスの登録 (NS_REFLECT_SUBOBJ) から、作った部品を保存キーの登録と一緒に渡す
    //! @details 基底の部品を先に、派生の固定メンバーを後に渡す。作っていない部品は飛ばす。所有は移さない
    void ForEachReflectedSubObj(const SceneActor& actor, const SubObjFieldVisitor& visit);

} // namespace NS
