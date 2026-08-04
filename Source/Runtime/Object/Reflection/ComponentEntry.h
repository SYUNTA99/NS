#pragma once

#include "Runtime/Math/Math.h"
#include "Runtime/Object/Reflection/ObjectRef.h"

#pragma warning(push, 0)
#include "ThirdParty/nlohmann/json.hpp"
#pragma warning(pop)

#include <cstdint>
#include <string>
#include <string_view>

namespace NS::Object
{
    // 配置物データに書かれたコンポーネント 1 件 (型名と欄) を読み書きする
    struct ObjectData;

    /// {"type": typeName, "fields": fields} のコンポーネント 1 件を作る。 fields 省略は空 object
    [[nodiscard]] nlohmann::json MakeComponentEntry(std::string_view typeName,
                                                    nlohmann::json fields = nlohmann::json::object());

    /// コンポーネント 1 件の型名。 壊れた形は空文字
    [[nodiscard]] std::string_view ComponentEntryType(const nlohmann::json& entry) noexcept;

    /// コンポーネント 1 件の永続 id。 未採番と壊れた形は 0
    /// 型名と並び順でなく id で 1 個を名指しできるようにするための同一性。 番号は object と同じ空間
    [[nodiscard]] std::uint32_t ComponentEntryId(const nlohmann::json& entry) noexcept;

    /// コンポーネント 1 件へ永続 id を書く。 0 は未採番の印なので消す
    void SetComponentEntryId(nlohmann::json& entry, std::uint32_t id);

    /// コンポーネント 1 件のデータ active が有効か。 印が無ければ有効
    [[nodiscard]] bool ComponentEntryEnabled(const nlohmann::json& entry) noexcept;

    /// コンポーネント 1 件へデータ active を書く。 有効は既定なので印を消す
    void SetComponentEntryEnabled(nlohmann::json& entry, bool enabled);

    /// エントリの fields。 壊れた形は nullptr
    [[nodiscard]] const nlohmann::json* ComponentEntryFields(const nlohmann::json& entry) noexcept;

    /// object.components から型名一致の最初の 1 件を返す。 無ければ nullptr
    /// component / field 走査の唯一の経路。 各利用側が同じループを手書きするのを防ぐ
    [[nodiscard]] const nlohmann::json* FindComponentEntry(const ObjectData& object,
                                                           std::string_view typeName) noexcept;
    [[nodiscard]] nlohmann::json* FindComponentEntry(ObjectData& object, std::string_view typeName) noexcept;

    /// entry の fields から値を型付きで読む。 不在・型不一致は fallback
    [[nodiscard]] float FieldFloat(const nlohmann::json& entry, std::string_view name, float fallback) noexcept;
    [[nodiscard]] int FieldInt(const nlohmann::json& entry, std::string_view name, int fallback) noexcept;
    [[nodiscard]] NS::Math::Vector3 FieldVector3(const nlohmann::json& entry,
                                                 std::string_view name,
                                                 const NS::Math::Vector3& fallback) noexcept;
    [[nodiscard]] std::string FieldString(const nlohmann::json& entry,
                                          std::string_view name,
                                          std::string_view fallback);
    /// {"ref": id} を読む。 不在・壊れた形は未設定 0
    [[nodiscard]] ObjectRef FieldObjectRef(const nlohmann::json& entry, std::string_view name) noexcept;
    [[nodiscard]] bool HasField(const nlohmann::json& entry, std::string_view name) noexcept;

    /// entry の fields へ値を書く。 JSON 表現は保存形式と同じ (Vector3=[x,y,z] / ObjectRef={"ref":id})
    void SetField(nlohmann::json& entry, std::string_view name, float value);
    void SetField(nlohmann::json& entry, std::string_view name, int value);
    void SetField(nlohmann::json& entry, std::string_view name, bool value);
    void SetField(nlohmann::json& entry, std::string_view name, const NS::Math::Vector3& value);
    void SetField(nlohmann::json& entry, std::string_view name, std::string_view value);
    /// 文字列リテラル (const char*) を string_view 版へ通す。 無いと bool 版へ落ちて true が書かれる
    void SetField(nlohmann::json& entry, std::string_view name, const char* value);
    void SetField(nlohmann::json& entry, std::string_view name, ObjectRef value);

} // namespace NS::Object
