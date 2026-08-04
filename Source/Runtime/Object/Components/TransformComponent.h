#pragma once

#include "Runtime/Math/Math.h"
#include "Runtime/Object/Component.h"
#include "Runtime/Object/Reflection/Reflection.h"
#include "Runtime/Object/Scene/SceneData.h"
#include "Runtime/Object/Transform.h"

#include <string_view>

namespace NS::Object
{
    /// @brief 位置・回転・スケールの実体を持ち、反射経路 (Inspector / undo / 直列化) へ載せるコンポーネント
    /// @details 器はコンストラクタでこれを 1 つ積み、GameObject::Root() はここが持つ Transform を指す
    /// 回転は Transform が quaternion で持ち、反射欄だけ Euler 度で読み書きする
    /// 依存: NS::Math
    class TransformComponent : public Component
    {
    public:
        TransformComponent() noexcept = default;

        /// 実体の Transform。器の Root() はこれを貸しているだけ
        [[nodiscard]] Transform& Root() noexcept { return m_transform; }
        [[nodiscard]] const Transform& Root() const noexcept { return m_transform; }

        void SetPosition(const NS::Math::Vector3& position) noexcept;
        [[nodiscard]] NS::Math::Vector3 Position() const noexcept;

        void SetRotationEulerDegrees(const NS::Math::Vector3& eulerDegrees) noexcept;
        [[nodiscard]] NS::Math::Vector3 RotationEulerDegrees() const noexcept;

        void SetScale(const NS::Math::Vector3& scale) noexcept;
        [[nodiscard]] NS::Math::Vector3 Scale() const noexcept;

        NS_REFLECT_BEGIN(TransformComponent, Component)
        NS_REFLECT_ACCESSOR(NS::Math::Vector3, "Position", Position(), SetPosition)
        NS_REFLECT_ACCESSOR(NS::Math::Vector3, "Rotation (deg)", RotationEulerDegrees(), SetRotationEulerDegrees)
        NS_REFLECT_ACCESSOR(NS::Math::Vector3, "Scale", Scale(), SetScale)
        NS_REFLECT_END()

    private:
        Transform m_transform; // 器の Root() が指す実体
    };

    // 配置物データに書かれた transform も同じ物を指すので、 live クラスと同じ場所に置く

    /// TransformComponent の反射型名。 transform エントリの照合に使う共有定数
    inline constexpr std::string_view k_TransformTypeName = "TransformComponent";

    /// 忠実な捕捉が root 回転を厳密なクォータニオンで運ぶ控え欄。 保存時は落として Euler だけ残す
    inline constexpr std::string_view k_RotationQuatFieldName = "Rotation (quat)";

    /// ObjectData の transform を読み書きする唯一の窓口。 実体は components 内の TransformComponent エントリで、
    /// 回転は Euler 度で持つが、 ここでは quaternion で受け渡して消費側を無改変に保つ
    /// Set は対象エントリが無ければ EnsureTransformComponent で 1 つ起こす
    [[nodiscard]] NS::Math::Vector3 ObjectPosition(const ObjectData& object) noexcept;
    void SetObjectPosition(ObjectData& object, const NS::Math::Vector3& position) noexcept;
    [[nodiscard]] NS::Math::Quaternion ObjectRotation(const ObjectData& object) noexcept;
    void SetObjectRotation(ObjectData& object, const NS::Math::Quaternion& rotation) noexcept;
    [[nodiscard]] NS::Math::Vector3 ObjectScale(const ObjectData& object) noexcept;
    void SetObjectScale(ObjectData& object, const NS::Math::Vector3& scale) noexcept;

    /// object に TransformComponent エントリが無ければ既定値(位置0 / 回転0 / スケール1)で 1 つ足して返す
    /// 既にあればそれを返す。 factory と load 直後に通し、 全 object が transform を必ず 1 つ持つ不変を保つ
    nlohmann::json& EnsureTransformComponent(ObjectData& object);
} // namespace NS::Object
