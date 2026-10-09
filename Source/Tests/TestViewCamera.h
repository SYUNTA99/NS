#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/Object/Actor.h"
#include "NSlib/Object/Scene/Scene.h"
#include "NSlib/Object/SubObjects/VirtualCamera.h"

//! @brief 試し用の仮想カメラ。差し替えた位置と注視点をそのまま返す
//! @details 遊びはカメラの管理役が仮想カメラから合成した視点を読み、実カメラは読まない
//! 遊びが読む向きを決めたい試しはこれを置く
//! 依存: NS::Obj::VirtualCamera
class TestViewCamera final : public NS::Obj::VirtualCamera
{
public:
    //! 返す位置と注視点を差し替える
    void SetPose(const NS::Vector3& position, const NS::Vector3& target) noexcept
    {
        m_position = position;
        m_target = target;
    }

    //! 差し替えた位置と注視点を返す。割合は使わない
    [[nodiscard]] NS::Obj::CameraPose EvaluatePose(float alpha) const noexcept override
    {
        (void)alpha;
        return MakePose(m_position, m_target, NS::Vector3{0.0f, 1.0f, 0.0f});
    }

    NS_REFLECT_NONE(TestViewCamera, NS::Obj::VirtualCamera)

private:
    NS::Vector3 m_position{0.0f, 0.0f, -5.0f}; // 返す位置
    NS::Vector3 m_target{0.0f, 0.0f, 0.0f};    // 返す注視点
};

//! @brief 試し用の仮想カメラの持ち主
//! @details シーンへ湧かすと開始の時に仮想カメラが管理役へ登録される。シーンの外で使う時は管理役へ直に登録する
//! 依存: NS::Obj::Actor, TestViewCamera
class TestViewCameraHost final : public NS::Obj::Actor
{
public:
    //! 持っている仮想カメラ
    [[nodiscard]] TestViewCamera& Vcam() noexcept { return *m_vcam; }

protected:
    void Init() override { m_vcam = CreateSubObj<TestViewCamera>("Vcam"); }

private:
    TestViewCamera* m_vcam = nullptr;
};

//! @brief position から target を見る試し用の仮想カメラをシーンへ湧かす
//! @param[in,out] scene 湧かす先のシーン
//! @param[in] position カメラの位置
//! @param[in] target 注視点
//! @return 湧かした仮想カメラ。湧かせなかった場合は nullptr
inline TestViewCamera* PlaceViewCamera(NS::Obj::Scene& scene, const NS::Vector3& position, const NS::Vector3& target)
{
    TestViewCameraHost* host = scene.SpawnTransient<TestViewCameraHost>();
    if (host == nullptr)
    {
        return nullptr;
    }
    host->Vcam().SetPose(position, target);
    return &host->Vcam();
}
