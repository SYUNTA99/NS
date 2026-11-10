#pragma once

#include "NSlib/Core/Interpolated.h"
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

    //! 個体の姿勢を持つ部品。Snapshot で控えた固定更新間の姿勢を描画時に補間する
    //! SceneActor::Root() はこの部品を返す。持ち主なしでも作れる
    class Transform : public Object
    {
    public:
        Transform() noexcept = default;

        //! シーン内の位置
        void SetPosition(const Vector3& position) noexcept;
        //! シーン内の回転
        void SetRotation(const Quaternion& rotation) noexcept;
        //! 個体の大きさ。値をそのまま書く
        //! Inspector とシーンの JSON から来る値は、欄から書く SetScaleField が下限で止めてから書く
        void SetScale(const Vector3& scale) noexcept;
        //! 今の位置と前フレームの位置を同じだけずらす
        //! 補間には動きとして映らない。前フレームから今までに動いた分はそのまま補間される
        void ShiftPosition(const Vector3& delta) noexcept;
        //! 今の位置と前フレームの位置を同じ位置にする。移った分は補間に動きとして映らない
        void Teleport(const Vector3& position) noexcept;

        [[nodiscard]] const Vector3& Position() const noexcept { return m_position.Current(); }
        [[nodiscard]] const Quaternion& Rotation() const noexcept { return m_rotation.Current(); }
        [[nodiscard]] const Vector3& Scale() const noexcept { return m_scale.Current(); }

        //! 補間用の前の値。Snapshot が控える
        [[nodiscard]] const Vector3& PreviousPosition() const noexcept { return m_position.Previous(); }
        [[nodiscard]] const Quaternion& PreviousRotation() const noexcept { return m_rotation.Previous(); }
        [[nodiscard]] const Vector3& PreviousScale() const noexcept { return m_scale.Previous(); }

        //! 現在 PRS を previous に退避する。Scene::OnUpdate 先頭で全 Transform に一括実行する
        //! 各個体の更新前に同じ固定フレームの値を控える
        void Snapshot() noexcept;

        //! 現在と補間前の姿勢を一緒に写す。追従先と同じ軌道で補間される
        void CopyFrom(const Transform& source) noexcept;

        [[nodiscard]] Matrix WorldMatrix() const noexcept;
        //! 個体の前後の姿勢を補間したシーン内の行列
        [[nodiscard]] Matrix InterpolatedWorldMatrix(float alpha) const noexcept;

        NS_REFLECT_BEGIN(Transform, Object)
        NS_GLOBAL_ACCESSOR(Vector3, k_PositionFieldName, Position(), SetPosition)
        NS_GLOBAL_ACCESSOR(Quaternion, k_RotationFieldName, Rotation(), SetRotation)
        NS_GLOBAL_ACCESSOR(Vector3, k_ScaleFieldName, Scale(), SetScaleField)
        NS_REFLECT_END()

    private:
        //! 欄から来る大きさを書く。0 と負は描画と当たり判定を壊すので、最小の正の値で止める
        void SetScaleField(const Vector3& scale) noexcept;

        Interpolated<Vector3> m_position{Vector3{0.0f, 0.0f, 0.0f}};
        Interpolated<Quaternion> m_rotation{Quaternion{}};
        Interpolated<Vector3> m_scale{Vector3{1.0f, 1.0f, 1.0f}};
    };
} // namespace NS
