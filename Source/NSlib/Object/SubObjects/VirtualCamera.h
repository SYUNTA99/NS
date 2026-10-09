#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/Object/SubObject.h"

namespace NS::Obj
{
    //! 仮想カメラが返す 1 フレーム分のカメラ姿勢 + 投影設定。CameraManager が実 Camera へそのまま書く
    struct CameraPose
    {
        NS::Vector3 position{0.0f, 0.0f, -5.0f};                         // カメラ位置
        NS::Vector3 target{0.0f, 0.0f, 0.0f};                            // 注視点
        NS::Vector3 up{0.0f, 1.0f, 0.0f};                                // アップベクトル
        NS::Radians fovY{NS::ToRadians(NS::Degrees{60.0f})}; // 垂直視野角
        float nearPlane = 0.1f;                                                // ニアクリップ距離
        float farPlane = 1000.0f;                                              // ファークリップ距離
        // 射影の後の画面のずれ。縦横とも画面の幅を 2
        // とした座標で、右と上が正。位置と向きを変えずに画面ごと動かす揺れが書く
        NS::Vector2 screenOffset{0.0f, 0.0f};

        //! t=0 で a、t=1 で b の線形補間。up は補間後に正規化する。CameraManager の vcam 切替ブレンドが使う
        [[nodiscard]] static CameraPose Lerp(const CameraPose& a, const CameraPose& b, float t) noexcept
        {
            CameraPose pose{};
            pose.position = NS::Vector3::Lerp(a.position, b.position, t);
            pose.target = NS::Vector3::Lerp(a.target, b.target, t);
            NS::Vector3 up = NS::Vector3::Lerp(a.up, b.up, t);
            up.Normalize();
            pose.up = up;
            pose.fovY = NS::Radians{NS::Lerp(a.fovY.value, b.fovY.value, t)};
            pose.nearPlane = NS::Lerp(a.nearPlane, b.nearPlane, t);
            pose.farPlane = NS::Lerp(a.farPlane, b.farPlane, t);
            pose.screenOffset = NS::Vector2::Lerp(a.screenOffset, b.screenOffset, t);
            return pose;
        }
    };

    //! @brief 実カメラを持たない「仮想カメラ」基底
    //! @details 描画も実 Camera 所有もせず、EvaluatePose(alpha) で位置 / 注視点 / up と
    //! 投影設定をまとめた目標 pose を返すだけ。CameraManager が登録済みの
    //! vcam から最高優先度の active なものを選び、その pose を 1 個の実 SceneCamera へ書く
    //! 状態は fixed step の OnUpdate で進め、最終姿勢は EvaluatePose で返す。follow 系は
    //! render 時に alpha で補間 target を追うため、姿勢決定を pose 返却へ分離する
    //! カメラの窓口から管理役へ OnStart で自分を登録し、OnEndPlay で外す
    //! 依存: NS, NS::Obj::SubObject
    class VirtualCamera : public SubObject
    {
    public:
        VirtualCamera() noexcept = default;
        ~VirtualCamera() noexcept override = default;

        //! カメラの窓口から管理役へ自分を登録する。派生で上書きするなら基底のこれを呼ぶ
        void OnAppear() override { VirtualCamera::OnStart(); }
        void OnKill() noexcept override { VirtualCamera::OnEndPlay(); }
        void OnStart() override;

        //! 管理役から自分を外す。派生で上書きするなら基底のこれを呼ぶ
        void OnEndPlay() override;

        //! この vcam の最終姿勢を返す。alpha は補間係数で、follow 系が補間 target に使い free-fly は無視してよい
        [[nodiscard]] virtual CameraPose EvaluatePose(float alpha) const noexcept = 0;

        //! CameraManager の選択優先度。大きいほど優先、同値は登録順。active な vcam の中から最大が選ばれる
        void SetVcamPriority(int priority) noexcept { m_vcamPriority = priority; }
        [[nodiscard]] int VcamPriority() const noexcept { return m_vcamPriority; }

        //! 投影設定。vcam ごとに保持し EvaluatePose の pose へ載せる。play=far100 / editor=far200 等の差を吸収する
        void SetFovY(NS::Radians fov) noexcept { m_fovY = fov; }
        [[nodiscard]] NS::Radians FovY() const noexcept { return m_fovY; }
        void SetNearPlane(float nearPlane) noexcept { m_nearPlane = nearPlane; }
        [[nodiscard]] float NearPlane() const noexcept { return m_nearPlane; }
        void SetFarPlane(float farPlane) noexcept { m_farPlane = farPlane; }
        [[nodiscard]] float FarPlane() const noexcept { return m_farPlane; }

        // 姿勢は派生と CameraManager
        // が決めるので保存する調整値は無い。派生のリフレクション鎖の中継点として型名だけ登録する
        NS_REFLECT_NONE(VirtualCamera, SubObject)

    protected:
        //! 派生が position/target/up を渡すと、保持中の投影設定を載せた CameraPose を返す
        [[nodiscard]] CameraPose MakePose(const NS::Vector3& position,
                                          const NS::Vector3& target,
                                          const NS::Vector3& up) const noexcept
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
        int m_vcamPriority = 0;                                                  // CameraManager の選択優先度
        NS::Radians m_fovY{NS::ToRadians(NS::Degrees{60.0f})}; // 垂直視野角
        float m_nearPlane = 0.1f;                                                // ニアクリップ距離
        float m_farPlane = 1000.0f;                                              // ファークリップ距離
    };
} // namespace NS::Obj
