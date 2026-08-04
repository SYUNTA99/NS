#pragma once

#include "Runtime/Core/NonCopyable.h"

#include <cstdint>
#include <string>
#include <utility>

namespace NS::Object
{
    class World;

    struct ObjectIdAccess;

    /// @brief GameObject と Component の共通基底。クラス名と永続 id と表示名を持つ
    /// @details ClassName() は保存が ObjectData.className へ写し、読込は登録簿の同名登録で型を選ぶ
    /// Id() は保存と参照解決で使う同一性。配置物と Component の両方に振られ、番号の空間は共通
    /// Name() は編集側が付ける表示名。付けられるのは配置物だけで、Component は空のまま
    class Object : public NS::Core::NonCopyable
    {
    public:
        Object() noexcept = default;
        virtual ~Object() noexcept = default;

        /// クラス名。素の GameObject と未反射 Component は空を返す
        [[nodiscard]] virtual const char* ClassName() const noexcept { return ""; }

        /// 永続 id。0 は未採番
        [[nodiscard]] std::uint32_t Id() const noexcept { return m_id; }

        /// 編集側が付けた表示名。空なら呼出側が型から名前を導出する
        [[nodiscard]] const std::string& Name() const noexcept { return m_name; }

    private:
        // id と表示名の書き込みは data を焼く経路だけに絞る
        // 配置物は World::Rebuild、Component は組み立て側が ObjectIdAccess 越しに書く
        friend class World;
        friend struct ObjectIdAccess;
        void SetId(std::uint32_t id) noexcept { m_id = id; }
        void SetName(std::string name) noexcept { m_name = std::move(name); }

        std::uint32_t m_id = 0; // 永続 id、0 は未採番
        std::string m_name;     // 表示名、空は未設定
    };

    /// @brief Component へ永続 id を書くための専用経路。組み立て経路だけが使う
    /// @details 配置物の id は World が焼くので、こちらは Component 用
    struct ObjectIdAccess
    {
        static void SetId(Object& object, std::uint32_t id) noexcept { object.SetId(id); }
    };

} // namespace NS::Object
