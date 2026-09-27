#pragma once

#include <Runtime/Core/Math.h>
#include <Runtime/Object/Components/CameraComponent.h>
#include <Runtime/Object/Components/ThirdPersonFollow.h>
#include <Runtime/Object/Components/VirtualCamera.h>
#include <Runtime/Object/Scene/Scene.h>

#include <cmath>

namespace NsTest
{
    //! 試しが置く画面の横と縦の比。ThirdPersonFollow の構図の枠と同じ 16 : 9
    inline constexpr float k_ViewAspect = 16.0f / 9.0f;

    //! @brief 点が姿勢の視野のどこに写るかを返す。試しの側の物差し
    //! @details 視野の中心を 0、端を ±1 とする。横の幅は縦の幅に k_ViewAspect を掛けた幅
    //! @param[in] pose カメラの姿勢
    //! @param[in] point 世界座標の点
    //! @return x が右、y が上の向きの位置
    [[nodiscard]] inline NS::Core::Vector2 ScreenOf(const NS::Obj::CameraPose& pose, const NS::Core::Vector3& point)
    {
        NS::Core::Vector3 forward = pose.target - pose.position;
        forward.Normalize();
        NS::Core::Vector3 right = NS::Core::Cross(NS::Core::Vector3{0.0f, 1.0f, 0.0f}, forward);
        right.Normalize();
        const NS::Core::Vector3 up = NS::Core::Cross(forward, right);
        const NS::Core::Vector3 offset = point - pose.position;
        const float halfHeight = NS::Core::Dot(offset, forward) * std::tan(pose.fovY.value * 0.5f);
        return NS::Core::Vector2{NS::Core::Dot(offset, right) / (halfHeight * k_ViewAspect),
                                 NS::Core::Dot(offset, up) / halfHeight};
    }

    //! @brief ThirdPersonFollow と同じ決め方のカメラの右を返す
    //! @param[in] follow 追従カメラ
    //! @return 水平で長さ 1 の右の向き
    [[nodiscard]] inline NS::Core::Vector3 CameraRight(const NS::Obj::ThirdPersonFollow& follow)
    {
        return NS::Core::Vector3{std::cos(follow.Yaw()), 0.0f, -std::sin(follow.Yaw())};
    }

    //! @brief ThirdPersonFollow と同じ決め方のカメラの上を返す
    //! @param[in] follow 追従カメラ
    //! @return 視線と右に直交する長さ 1 の上の向き
    [[nodiscard]] inline NS::Core::Vector3 CameraUp(const NS::Obj::ThirdPersonFollow& follow)
    {
        const float sp = std::sin(follow.Pitch());
        return NS::Core::Vector3{-sp * std::sin(follow.Yaw()), std::cos(follow.Pitch()), -sp * std::cos(follow.Yaw())};
    }

    //! @brief シーンの実カメラの水平の正面を direction の水平の向きにする
    //! @details 注視点を原点から direction へ 1 m 先の高さ 1 m に、位置を原点から direction の逆へ 6 m の
    //! 高さ 3 m に置く。
    //! 水平の正面だけを決める置き方で、自機を画面に入れる置き方ではない。仮想カメラが実カメラへ書くまで保つ
    //! @param[in,out] scene 実カメラを置き直すシーン
    //! @param[in] direction 正面にする向き。水平で長さ 1
    //! @return 実カメラがあって置き直した場合 true、それ以外の場合は false
    [[nodiscard]] inline bool FaceSceneCamera(NS::Obj::Scene& scene, const NS::Core::Vector3& direction)
    {
        NS::Obj::CameraComponent* camera = scene.MainCamera();
        if (camera == nullptr)
        {
            return false;
        }
        camera->SetPosition(NS::Core::Vector3{-direction.x * 6.0f, 3.0f, -direction.z * 6.0f});
        camera->SetTarget(NS::Core::Vector3{direction.x, 1.0f, direction.z});
        return true;
    }
} // namespace NsTest
