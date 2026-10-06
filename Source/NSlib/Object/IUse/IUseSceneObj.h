#pragma once

#include "NSlib/Object/Scene/SceneObjHolder.h"

namespace NS::Obj
{
    //! @brief シーンに 1 つの物の窓口
    //! @details Actor・UIActor・シーンが持つ。部品は持ち主の Actor から引く
    class IUseSceneObj
    {
    public:
        //! シーンに 1 つの物の置き場。シーンに居ない間は nullptr
        [[nodiscard]] virtual SceneObjHolder* GetSceneObjHolder() const noexcept = 0;

    protected:
        ~IUseSceneObj() = default;
    };

    //! 型 T のシーンに 1 つの物。作っていないかシーンに居なければ nullptr
    template <class T> [[nodiscard]] T* FindSceneObj(const IUseSceneObj& user) noexcept
    {
        SceneObjHolder* holder = user.GetSceneObjHolder();
        if (holder == nullptr)
        {
            return nullptr;
        }
        return holder->Find<T>();
    }

    //! 型 T のシーンに 1 つの物。まだ作っていなければ作る。シーンに居なければ nullptr
    template <class T> T* GetOrCreateSceneObj(const IUseSceneObj& user)
    {
        SceneObjHolder* holder = user.GetSceneObjHolder();
        if (holder == nullptr)
        {
            return nullptr;
        }
        return &holder->GetOrCreate<T>();
    }
} // namespace NS::Obj
