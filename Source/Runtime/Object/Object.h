#pragma once

#include "Runtime/Core/NonCopyable.h"

#include <cstdint>
#include <string>
#include <utility>

namespace NS::Obj
{
    class GameObject;
    class ObjectList;

    //! @brief GameObject と Component の共通基底。クラス名と永続 id と名前を持つ
    //! @details ClassName() は保存が配置物の JSON の class へ写し、読込は TypeRegistry の同名登録で型を選ぶ
    //! Id() は保存と参照解決で使う同一性。配置物と Component の両方に振られ、番号の空間は共通
    //! Name() は人が読む名前。配置物はシーンの中で、Component は持ち主の配置物の中で一意
    //! ファイルの参照は名前で書き、読込で id へ直す。実行中の参照は id で持つので、改名しても切れない
    class Object : public NS::Core::NonCopyable
    {
    public:
        Object() noexcept = default;
        virtual ~Object() noexcept = default;

        //! クラス名。素の GameObject と未リフレクション Component は空を返す
        [[nodiscard]] virtual const char* ClassName() const noexcept { return ""; }

        //! 永続 id。0 は未採番
        [[nodiscard]] std::uint32_t Id() const noexcept { return m_id; }

        //! 名前。配置物の空は未設定で、呼出側が型から表示名を導出する。Component は作った時点で必ず付く
        [[nodiscard]] const std::string& Name() const noexcept { return m_name; }

    private:
        // id はシーンの中で一意なので、シーンの配置物を持つ ObjectList だけが書く
        // 名前は一意の範囲を持つ側が書く。配置物は ObjectList、Component は持ち主の GameObject
        friend class ObjectList;
        friend class GameObject;
        void SetId(std::uint32_t id) noexcept { m_id = id; }
        void SetName(std::string name) noexcept { m_name = std::move(name); }

        std::uint32_t m_id = 0; // 永続 id、0 は未採番
        std::string m_name;     // 名前。配置物の空は未設定
    };

} // namespace NS::Obj
