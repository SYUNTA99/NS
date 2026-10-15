#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/CoreObject/Reflection/ActorRef.h"

#pragma warning(push, 0)
#include "ThirdParty/nlohmann/json.hpp"
#pragma warning(pop)

#include "NSlib/Core/Types.h"
#include <string>
#include <string_view>

namespace NS
{
    //! 数 3 個の [x,y,z] を読む。形式が違えば出力を変更せず false
    [[nodiscard]] bool ReadVector3(const nlohmann::json& value, NS::Vector3& out) noexcept;
    //! 数 4 個の [x,y,z,w] を読む。形式が違えば出力を変更せず false
    [[nodiscard]] bool ReadQuaternion(const nlohmann::json& value, NS::Quaternion& out) noexcept;

    //! entry から値を型付きで読む。不在・型不一致は fallback
    [[nodiscard]] float FieldFloat(const nlohmann::json& entry, std::string_view name, float fallback) noexcept;
    [[nodiscard]] int FieldInt(const nlohmann::json& entry, std::string_view name, int fallback) noexcept;
    [[nodiscard]] NS::uint32 FieldUnsigned(const nlohmann::json& entry,
                                           std::string_view name,
                                           NS::uint32 fallback) noexcept;
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

    //! entry へ値を書く。Vector3 は [x,y,z]、Quaternion は [x,y,z,w]
    //! ActorRef は {"ref":id} の形で書く
    void SetField(nlohmann::json& entry, std::string_view name, float value);
    void SetField(nlohmann::json& entry, std::string_view name, int value);
    void SetField(nlohmann::json& entry, std::string_view name, bool value);
    void SetField(nlohmann::json& entry, std::string_view name, const NS::Vector3& value);
    void SetField(nlohmann::json& entry, std::string_view name, const NS::Quaternion& value);
    void SetField(nlohmann::json& entry, std::string_view name, std::string_view value);
    //! 文字列リテラル (const char*) を string_view 版へ通す。無いと bool 版へ落ちて true が書かれる
    void SetField(nlohmann::json& entry, std::string_view name, const char* value);
    void SetField(nlohmann::json& entry, std::string_view name, ActorRef value);

} // namespace NS
