#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/Object/ObjectJson.h"
#include "NSlib/Object/Reflection/Reflection.h"
#include "NSlib/Object/SubObject.h"
#include "NSlib/Object/Transform.h"

#include <string_view>

namespace NS::Obj
{
    //! TransformSubObject のリフレクション欄の名前。直列化の JSON キーそのもので、
    //! 欄を名指しで書く消費側と綴りがずれないよう定数で共有する
    inline constexpr const char* k_PositionFieldName = "位置";
    inline constexpr const char* k_RotationFieldName = "回転";
    inline constexpr const char* k_ScaleFieldName = "スケール";

    //! @brief 位置・回転・スケールの実体を持ち、リフレクション経路 (Inspector / undo / 直列化) へ載せるサブオブジェクト
    //! @details Actor はコンストラクタでこれを 1 つ積み、Root() はここが持つ Transform を指す
    //! 依存: NS
    class TransformSubObject : public SubObject
    {
    public:
        TransformSubObject() noexcept = default;

        //! 実体の Transform。Actor の Root() はこれを貸しているだけ
        [[nodiscard]] Transform& Root() noexcept { return m_transform; }
        [[nodiscard]] const Transform& Root() const noexcept { return m_transform; }

        void SetPosition(const NS::Vector3& position) noexcept;
        [[nodiscard]] NS::Vector3 Position() const noexcept;

        void SetRotation(const NS::Quaternion& rotation) noexcept;
        [[nodiscard]] NS::Quaternion Rotation() const noexcept;

        void SetScale(const NS::Vector3& scale) noexcept;
        [[nodiscard]] NS::Vector3 Scale() const noexcept;

        NS_REFLECT_BEGIN(TransformSubObject, SubObject)
        NS_REFLECT_ACCESSOR(NS::Vector3, k_PositionFieldName, Position(), SetPosition)
        NS_REFLECT_ACCESSOR(NS::Quaternion, k_RotationFieldName, Rotation(), SetRotation)
        NS_REFLECT_ACCESSOR(NS::Vector3, k_ScaleFieldName, Scale(), SetScale)
        NS_REFLECT_END()

    private:
        Transform m_transform; // Actor の Root() が指す実体
    };

    // 配置物データに書かれた transform も同じ物を指すので、live クラスと同じ場所に置く

    //! 根の部品 TransformSubObject の部品名。保存の鍵そのもので、名指す所は全てこれを使う
    inline constexpr std::string_view k_TransformSubObjName = "Transform";

    //! 配置物の JSON の transform を読み書きする唯一の経路。
    //! 実体は subObjects 内の k_TransformSubObjName の欄
    //! Set は対象エントリが無ければ EnsureTransformSubObject で 1 つ作る
    [[nodiscard]] NS::Vector3 ObjectPosition(const nlohmann::json& object) noexcept;
    void SetObjectPosition(nlohmann::json& object, const NS::Vector3& position) noexcept;
    [[nodiscard]] NS::Quaternion ObjectRotation(const nlohmann::json& object) noexcept;
    void SetObjectRotation(nlohmann::json& object, const NS::Quaternion& rotation) noexcept;
    [[nodiscard]] NS::Vector3 ObjectScale(const nlohmann::json& object) noexcept;
    void SetObjectScale(nlohmann::json& object, const NS::Vector3& scale) noexcept;

    //! object に TransformSubObject エントリが無ければ既定値(位置0 / 回転なし / スケール1)で 1 つ足して返す
    //! 既にあればそれを返す。factory と load 直後に通し、全 object が transform を必ず 1 つ持つ不変を保つ
    nlohmann::json& EnsureTransformSubObject(nlohmann::json& object);
} // namespace NS::Obj
