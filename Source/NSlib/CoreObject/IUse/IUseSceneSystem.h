#pragma once

#include "NSlib/Core/Assert.h"
#include "NSlib/CoreObject/System/SceneSystemHolder.h"

namespace NS
{
    //! @brief シーンに 1 つの物の窓口
    //! @details SceneActor とシーンが持つ。部品は持ち主の SceneActor から引く
    class IUseSceneSystem
    {
    public:
        //! シーンに 1 つの物の置き場。シーンに居ない間は nullptr
        [[nodiscard]] virtual SceneSystemHolder* GetSceneSystemHolder() const noexcept = 0;

    protected:
        ~IUseSceneSystem() = default;
    };

    //! 型 T のシーンに 1 つの物。作っていないかシーンに居なければ nullptr
    template <class T> [[nodiscard]] T* FindSceneSystem(const IUseSceneSystem* user) noexcept
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        SceneSystemHolder* holder = user->GetSceneSystemHolder();
        if (holder == nullptr)
        {
            return nullptr;
        }
        return holder->Find<T>();
    }

    //! 型 T のシーンに 1 つの物。まだ作っていなければ作る。シーンに居なければ nullptr
    template <class T> T* GetOrCreateSceneSystem(const IUseSceneSystem* user)
    {
        NS_ASSERT(Scene, user != nullptr, "窓口の関数に null の user を渡している");
        SceneSystemHolder* holder = user->GetSceneSystemHolder();
        if (holder == nullptr)
        {
            return nullptr;
        }
        return &holder->GetOrCreate<T>();
    }
} // namespace NS
