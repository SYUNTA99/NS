#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/CoreObject/SubObjects/VirtualCamera.h"

//! @brief 試し用の仮想カメラ。差し替えた位置と注視点をそのまま返す
//! @details 遊びはカメラの管理役が仮想カメラから合成した視点を読み、実カメラは読まない
//! 遊びが読む向きを決めたい試しはこれを置く
//! 依存: NS::VirtualCamera
class TestViewCamera final : public NS::VirtualCamera
{
public:
    //! 返す位置と注視点を差し替える
    void SetPose(const NS::Vector3& position, const NS::Vector3& target) noexcept
    {
        m_position = position;
        m_target = target;
    }

    //! 差し替えた位置と注視点を返す。割合は使わない
    [[nodiscard]] NS::CameraPose EvaluatePose(float alpha) const noexcept override
    {
        (void)alpha;
        return MakePose(m_position, m_target, NS::Vector3{0.0f, 1.0f, 0.0f});
    }

    NS_REFLECT_NONE(TestViewCamera, NS::VirtualCamera)

private:
    NS::Vector3 m_position{0.0f, 0.0f, -5.0f}; // 返す位置
    NS::Vector3 m_target{0.0f, 0.0f, 0.0f};    // 返す注視点
};

//! @brief 試し用の仮想カメラの持ち主
//! @details シーンへ湧かすと開始の時に仮想カメラが管理役へ登録される。シーンの外で使う時は管理役へ直に登録する
//! 依存: NS::SceneActor, TestViewCamera
class TestViewCameraHostActor final : public NS::SceneActor
{
public:
    NS_REFLECT_BEGIN(TestViewCameraHostActor, NS::SceneActor)
    NS_REFLECT_SUBOBJ(m_vcam, "Vcam")
    NS_REFLECT_END()

    //! 持っている仮想カメラ
    [[nodiscard]] TestViewCamera& Vcam() noexcept { return *m_vcam; }
    void OnEndPlay() override
    {
        NS::SceneActor::OnEndPlay();
        NS::UnregisterVirtualCamera(this, m_vcam.get());
    }

protected:
    void OnInit() override { CreateSubObj(m_vcam); }
    void OnStart() override
    {
        NS::SceneActor::OnStart();
        NS::RegisterVirtualCamera(this, m_vcam.get());
    }

private:
    std::unique_ptr<TestViewCamera> m_vcam;
};

//! @brief position から target を見る試し用の仮想カメラをシーンへ湧かす
//! @param[in,out] scene 湧かす先のシーン
//! @param[in] position カメラの位置
//! @param[in] target 注視点
//! @return 湧かした仮想カメラ。湧かせなかった場合は nullptr
inline TestViewCamera* PlaceViewCamera(NS::Scene& scene, const NS::Vector3& position, const NS::Vector3& target)
{
    TestViewCameraHostActor* host = scene.SpawnTransient<TestViewCameraHostActor>();
    if (host == nullptr)
    {
        return nullptr;
    }
    host->Vcam().SetPose(position, target);
    return &host->Vcam();
}
