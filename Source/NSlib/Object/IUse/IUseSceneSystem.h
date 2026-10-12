#pragma once

#include "NSlib/Object/System/SceneSystemHolder.h"

namespace NS::Obj
{
    //! @brief シーンに 1 つの物の窓口
    //! @details Actor とシーンが持つ。部品は持ち主の Actor から引く
    class IUseSceneSystem
    {
    public:
        //! シーンに 1 つの物の置き場。シーンに居ない間は nullptr
        [[nodiscard]] virtual SceneSystemHolder* GetSceneSystemHolder() const noexcept = 0;

    protected:
        ~IUseSceneSystem() = default;
    };

    //! 型 T のシーンに 1 つの物。作っていないかシーンに居なければ nullptr
    template <class T> [[nodiscard]] T* FindSceneSystem(const IUseSceneSystem& user) noexcept
    {
        SceneSystemHolder* holder = user.GetSceneSystemHolder();
        if (holder == nullptr)
        {
            return nullptr;
        }
        return holder->Find<T>();
    }

    //! 型 T のシーンに 1 つの物。まだ作っていなければ作る。シーンに居なければ nullptr
    template <class T> T* GetOrCreateSceneSystem(const IUseSceneSystem& user)
    {
        SceneSystemHolder* holder = user.GetSceneSystemHolder();
        if (holder == nullptr)
        {
            return nullptr;
        }
        return &holder->GetOrCreate<T>();
    }
} // namespace NS::Obj
