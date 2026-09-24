#pragma once

#include <cstdint>

namespace NS::Obj
{
    struct ReflectionInfo;

    //! @brief 別の配置物の特定の Component を保存をまたいで指す参照の値。型を持たない共通部分
    //! @details object は持ち主の配置物、component は Component の永続 id。component が 0 なら未設定
    //! 実行中は id で持つので、配置物や Component を改名しても切れない。ファイルには両方の名前で書く
    //! 引くのは使う時で、ポインタは控えない。ObjectList::FindComponent が引く
    //! リフレクションと保存は型を問わずこの形で扱う
    struct ComponentRefValue
    {
        std::uint32_t object = 0;    // 持ち主の配置物の永続 id
        std::uint32_t component = 0; // Component の永続 id。0 は未設定

        //! 参照先が設定されているか。component が 0 なら未設定
        [[nodiscard]] constexpr bool IsSet() const noexcept { return component != 0; }

        [[nodiscard]] constexpr bool operator==(const ComponentRefValue&) const noexcept = default;
    };

    //! @brief 型 T の Component を指す参照。欄の型が Inspector で選べる相手を決める
    //! @details 値は ComponentRefValue と同じで、T は選べる型と引いた後の型を決めるだけ
    //! 型が合わない相手を指したデータは、引くと nullptr になり、読込時に警告される
    template <class T> struct ComponentRef : ComponentRefValue
    {
        using Target = T;
    };
} // namespace NS::Obj
