#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/CoreObject/Reflection/Reflection.h"
#include "NSlib/CoreObject/Object.h"

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
    class Transform : public Object
    {
    public:
        Transform() noexcept = default;

        //! シーン内の位置
        void SetPosition(const Vector3& position) noexcept;
        //! シーン内の回転
        void SetRotation(const Quaternion& rotation) noexcept;
        //! @brief 個体の大きさ。値をそのまま書く
        //! @details Inspector とシーンの JSON から来る値は、欄から書く SetScaleField が下限で止めてから書く
        void SetScale(const Vector3& scale) noexcept;
        //! @brief 今の位置と前フレームの位置を同じだけずらす
        //! @details 補間には動きとして映らない。前フレームから今までに動いた分はそのまま補間される
        void ShiftPosition(const Vector3& delta) noexcept;

        [[nodiscard]] const Vector3& Position() const noexcept { return m_position; }
        [[nodiscard]] const Quaternion& Rotation() const noexcept { return m_rotation; }
        [[nodiscard]] const Vector3& Scale() const noexcept { return m_scale; }

        //! 補間用の前の値。Snapshot が控える
        [[nodiscard]] const Vector3& PreviousPosition() const noexcept { return m_previousPosition; }
        [[nodiscard]] const Quaternion& PreviousRotation() const noexcept { return m_previousRotation; }
        [[nodiscard]] const Vector3& PreviousScale() const noexcept { return m_previousScale; }

        //! 現在 PRS を previous に退避する。Scene::OnUpdate 先頭で全 Transform に一括実行する
        //! 各個体の更新前に同じ固定フレームの値を控える
        void Snapshot() noexcept;

        [[nodiscard]] Matrix WorldMatrix() const noexcept;
        //! 個体の前後の姿勢を補間したシーン内の行列
        [[nodiscard]] Matrix InterpolatedWorldMatrix(float alpha) const noexcept;

        NS_REFLECT_BEGIN(Transform, Object)
        NS_ACCESSOR(Vector3, k_PositionFieldName, Position(), SetPosition)
        NS_ACCESSOR(Quaternion, k_RotationFieldName, Rotation(), SetRotation)
        NS_ACCESSOR(Vector3, k_ScaleFieldName, Scale(), SetScaleField)
        NS_REFLECT_END()

    private:
        //! 欄から来る大きさを書く。0 と負は描画と当たり判定を壊すので、最小の正の値で止める
        void SetScaleField(const Vector3& scale) noexcept;

        Vector3 m_position{0.0f, 0.0f, 0.0f};
        Quaternion m_rotation{};
        Vector3 m_scale{1.0f, 1.0f, 1.0f};

        Vector3 m_previousPosition{0.0f, 0.0f, 0.0f};
        Quaternion m_previousRotation{};
        Vector3 m_previousScale{1.0f, 1.0f, 1.0f};
    };

    //! 根の部品 Transform の部品名。保存の鍵そのもので、名指す所は全てこれを使う
    inline constexpr std::string_view k_TransformClassObjName = "Transform";
} // namespace NS
