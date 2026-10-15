#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/CoreObject/Reflection/Reflection.h"
#include "NSlib/CoreObject/SubObject.h"

#include <string_view>

namespace NS
{
    //! Transform のリフレクション欄の名前。直列化の JSON キーそのもので、
    //! 欄を名指しで書く消費側と綴りがずれないよう定数で共有する
    inline constexpr const char* k_PositionFieldName = "位置";
    inline constexpr const char* k_RotationFieldName = "回転";
    inline constexpr const char* k_ScaleFieldName = "スケール";

    //! @brief 個体の姿勢を持つ部品。Snapshot で控えた固定更新間の姿勢を描画時に補間する
    //! @details SceneActor::Root() はこの部品を返す。持ち主なしでも作れる
    class Transform : public SubObject
    {
    public:
        Transform() noexcept = default;

        //! シーン内の位置
        void SetPosition(const NS::Vector3& position) noexcept;
        //! シーン内の回転
        void SetRotation(const NS::Quaternion& rotation) noexcept;
        //! @brief 個体の大きさ。値をそのまま書く
        //! @details Inspector とシーンの JSON から来る値は、欄から書く SetScaleField が下限で止めてから書く
        void SetScale(const NS::Vector3& scale) noexcept;
        //! @brief 今の位置と前フレームの位置を同じだけずらす
        //! @details 補間には動きとして映らない。前フレームから今までに動いた分はそのまま補間される
        void ShiftPosition(const NS::Vector3& delta) noexcept;

        [[nodiscard]] const NS::Vector3& Position() const noexcept { return m_position; }
        [[nodiscard]] const NS::Quaternion& Rotation() const noexcept { return m_rotation; }
        [[nodiscard]] const NS::Vector3& Scale() const noexcept { return m_scale; }

        //! 補間用の前の値。Snapshot が控える
        [[nodiscard]] const NS::Vector3& PreviousPosition() const noexcept { return m_previousPosition; }
        [[nodiscard]] const NS::Quaternion& PreviousRotation() const noexcept { return m_previousRotation; }
        [[nodiscard]] const NS::Vector3& PreviousScale() const noexcept { return m_previousScale; }

        //! 現在 PRS を previous に退避する。Scene::OnUpdate 先頭で全 Transform に一括実行する
        //! 各個体の更新前に同じ固定フレームの値を控える
        void Snapshot() noexcept;

        [[nodiscard]] NS::Matrix WorldMatrix() const noexcept;
        //! 個体の前後の姿勢を補間したシーン内の行列
        [[nodiscard]] NS::Matrix InterpolatedWorldMatrix(float alpha) const noexcept;

        NS_REFLECT_BEGIN(Transform, SubObject)
        NS_REFLECT_ACCESSOR(NS::Vector3, k_PositionFieldName, Position(), SetPosition)
        NS_REFLECT_ACCESSOR(NS::Quaternion, k_RotationFieldName, Rotation(), SetRotation)
        NS_REFLECT_ACCESSOR(NS::Vector3, k_ScaleFieldName, Scale(), SetScaleField)
        NS_REFLECT_END()

    private:
        //! 欄から来る大きさを書く。0 と負は描画と当たり判定を壊すので、最小の正の値で止める
        void SetScaleField(const NS::Vector3& scale) noexcept;

        NS::Vector3 m_position{0.0f, 0.0f, 0.0f};
        NS::Quaternion m_rotation{};
        NS::Vector3 m_scale{1.0f, 1.0f, 1.0f};

        NS::Vector3 m_previousPosition{0.0f, 0.0f, 0.0f};
        NS::Quaternion m_previousRotation{};
        NS::Vector3 m_previousScale{1.0f, 1.0f, 1.0f};
    };

    //! 根の部品 Transform の部品名。保存の鍵そのもので、名指す所は全てこれを使う
    inline constexpr std::string_view k_TransformSubObjName = "Transform";
} // namespace NS
