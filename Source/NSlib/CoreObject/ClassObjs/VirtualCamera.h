#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/CoreObject/Object.h"

namespace NS
{
    //! 仮想カメラが返す 1 フレーム分のカメラ姿勢 + 投影設定。CameraManager が実 Camera へそのまま書く
    struct CameraPose
    {
        Vector3 position{0.0f, 0.0f, -5.0f};
        Vector3 target{0.0f, 0.0f, 0.0f};
        Vector3 up{0.0f, 1.0f, 0.0f};
        Radians fovY{ToRadians(Degrees{60.0f})};
        float nearPlane = 0.1f;
        float farPlane = 1000.0f;
        // 射影の後の画面のずれ。縦横とも画面の幅を 2
        // とした座標で、右と上が正。位置と向きを変えずに画面ごと動かす揺れが書く
        Vector2 screenOffset{0.0f, 0.0f};

        //! t=0 で a、t=1 で b の線形補間。up は補間後に正規化する。CameraManager の vcam 切替ブレンドが使う
        [[nodiscard]] static CameraPose Lerp(const CameraPose& a, const CameraPose& b, float t) noexcept
        {
            CameraPose pose{};
            pose.position = Vector3::Lerp(a.position, b.position, t);
            pose.target = Vector3::Lerp(a.target, b.target, t);
            Vector3 up = Vector3::Lerp(a.up, b.up, t);
            up.Normalize();
            pose.up = up;
            pose.fovY = Radians{NS::Lerp(a.fovY.value, b.fovY.value, t)};
            pose.nearPlane = NS::Lerp(a.nearPlane, b.nearPlane, t);
            pose.farPlane = NS::Lerp(a.farPlane, b.farPlane, t);
            pose.screenOffset = Vector2::Lerp(a.screenOffset, b.screenOffset, t);
            return pose;
        }
    };

    //! 実カメラを変更せず、CameraManager が選ぶ目標姿勢を返す部品
    //! 状態は固定更新で進め、描画時の EvaluatePose で補間する
    class VirtualCamera : public Object
    {
    public:
        //! 最終姿勢を返す。alpha は追従対象の補間係数で、自由視点では使わない
        [[nodiscard]] virtual CameraPose EvaluatePose(float alpha) const noexcept = 0;

        //! CameraManager の選択優先度。大きいほど優先、同値は登録順。active な vcam の中から最大が選ばれる
        void SetVcamPriority(int priority) noexcept { m_vcamPriority = priority; }
        [[nodiscard]] int VcamPriority() const noexcept { return m_vcamPriority; }

        //! 投影設定。vcam ごとに保持し EvaluatePose の pose へ載せる。play=far100 / editor=far200 等の差を吸収する
        void SetFovY(Radians fov) noexcept { m_fovY = fov; }
        [[nodiscard]] Radians FovY() const noexcept { return m_fovY; }
        void SetNearPlane(float nearPlane) noexcept { m_nearPlane = nearPlane; }
        [[nodiscard]] float NearPlane() const noexcept { return m_nearPlane; }
        //! 非有限と近い面以下の値は投影行列を壊すので、捨てて元の値を残す。値は JSON と Inspector の欄から来る
        void SetFarPlane(float farPlane) noexcept
        {
            if (HasPositiveMagnitude(farPlane - m_nearPlane))
            {
                m_farPlane = farPlane;
            }
        }
        [[nodiscard]] float FarPlane() const noexcept { return m_farPlane; }

        // 姿勢は派生と CameraManager
        // が決めるので保存する調整値は無い。派生のリフレクション鎖の中継点として型名だけ登録する
        NS_REFLECT_NONE(VirtualCamera, Object)

    protected:
        //! 派生が position/target/up を渡すと、保持中の投影設定を載せた CameraPose を返す
        [[nodiscard]] CameraPose MakePose(const Vector3& position,
                                          const Vector3& target,
                                          const Vector3& up) const noexcept
        {
            CameraPose pose{};
            pose.position = position;
            pose.target = target;
            pose.up = up;
            pose.fovY = m_fovY;
            pose.nearPlane = m_nearPlane;
            pose.farPlane = m_farPlane;
            return pose;
        }

    private:
        int m_vcamPriority = 0;
        Radians m_fovY{ToRadians(Degrees{60.0f})};
        float m_nearPlane = 0.1f;
        float m_farPlane = 1000.0f;
    };
} // namespace NS
