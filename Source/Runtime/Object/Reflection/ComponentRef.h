#pragma once

#include "Runtime/Object/Reflection/ActorRef.h"

#include <string>

namespace NS::Obj
{
    struct ReflectionInfo;

    //! @brief 別の配置物の特定の Component を保存をまたいで指す参照の値。型を持たない共通部分
    //! @details actor は持ち主の配置物、partName は持ち主の部品名。partName が空なら未設定
    //! 実行中は持ち主を永続 id で持つので、配置物を改名しても切れない。ファイルには両方の名前で書く
    //! リフレクションと保存は型を問わずこの形で扱う
    struct ComponentRefValue
    {
        ActorRef actor;
        std::string partName;

        //! 持ち主と部品名の両方が設定されている場合 true、それ以外の場合は false
        [[nodiscard]] bool IsSet() const noexcept { return actor.IsSet() && !partName.empty(); }

        [[nodiscard]] bool operator==(const ComponentRefValue&) const noexcept = default;
    };

    //! @brief 型 T の Component を指す参照。欄の型が Inspector で選べる相手を決める
    //! @details 値は ComponentRefValue と同じで、T は選べる型と引いた後の型を決めるだけ
    //! 型が合わない相手を指したデータは、引くと nullptr になり、読込時に警告される
    template <class T> struct ComponentRef : ComponentRefValue
    {
        using Target = T;
    };
} // namespace NS::Obj
