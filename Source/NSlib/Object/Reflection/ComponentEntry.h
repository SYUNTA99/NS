#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/Object/Reflection/ActorRef.h"

#pragma warning(push, 0)
#include "ThirdParty/nlohmann/json.hpp"
#pragma warning(pop)

#include <cstdint>
#include <string>
#include <string_view>

namespace NS::Obj
{
    //! エントリの fields。壊れた形は nullptr
    [[nodiscard]] const nlohmann::json* ComponentEntryFields(const nlohmann::json& entry) noexcept;

    //! entry の fields から値を型付きで読む。不在・型不一致は fallback
    [[nodiscard]] float FieldFloat(const nlohmann::json& entry, std::string_view name, float fallback) noexcept;
    [[nodiscard]] int FieldInt(const nlohmann::json& entry, std::string_view name, int fallback) noexcept;
    [[nodiscard]] NS::Vector3 FieldVector3(const nlohmann::json& entry,
                                                 std::string_view name,
                                                 const NS::Vector3& fallback) noexcept;
    [[nodiscard]] NS::Quaternion FieldQuaternion(const nlohmann::json& entry,
                                                       std::string_view name,
                                                       const NS::Quaternion& fallback) noexcept;
    [[nodiscard]] std::string FieldString(const nlohmann::json& entry,
                                          std::string_view name,
                                          std::string_view fallback);
    //! {"ref": id} を読む。不在・壊れた形は未設定 0
    [[nodiscard]] ActorRef FieldObjectRef(const nlohmann::json& entry, std::string_view name) noexcept;
    [[nodiscard]] bool HasField(const nlohmann::json& entry, std::string_view name) noexcept;

    //! entry の fields へ値を書く。JSON 表現は保存形式と同じ
    //! (Vector3=[x,y,z] / Quaternion=[x,y,z,w] / ActorRef={"ref":id})
    void SetField(nlohmann::json& entry, std::string_view name, float value);
    void SetField(nlohmann::json& entry, std::string_view name, int value);
    void SetField(nlohmann::json& entry, std::string_view name, bool value);
    void SetField(nlohmann::json& entry, std::string_view name, const NS::Vector3& value);
    void SetField(nlohmann::json& entry, std::string_view name, const NS::Quaternion& value);
    void SetField(nlohmann::json& entry, std::string_view name, std::string_view value);
    //! 文字列リテラル (const char*) を string_view 版へ通す。無いと bool 版へ落ちて true が書かれる
    void SetField(nlohmann::json& entry, std::string_view name, const char* value);
    void SetField(nlohmann::json& entry, std::string_view name, ActorRef value);

} // namespace NS::Obj
