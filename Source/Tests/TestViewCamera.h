#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/Object/Actor.h"
#include "NSlib/Object/SubObjects/VirtualCamera.h"
#include "NSlib/Object/Scene/Scene.h"

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
    TestViewCameraHost() { AttachFixedSubObject(m_vcam); }

    //! 基底の部品に続けて仮想カメラを "Vcam" で渡す
    void ForEachSubObj(const SubObjVisitor& visitor) const override
    {
        NS::Obj::Actor::ForEachSubObj(visitor);
        visitor("Vcam", m_vcam);
    }

    //! 持っている仮想カメラ
    [[nodiscard]] TestViewCamera& Vcam() noexcept { return m_vcam; }

private:
    mutable TestViewCamera m_vcam; // 固定の部品。ForEachSubObj が const のまま部品を渡すため mutable
};

//! @brief position から target を見る試し用の仮想カメラをシーンへ湧かす
//! @param[in,out] scene 湧かす先のシーン
//! @param[in] position カメラの位置
//! @param[in] target 注視点
//! @return 湧かした仮想カメラ。湧かせなかった場合は nullptr
inline TestViewCamera* PlaceViewCamera(NS::Obj::Scene& scene,
                                       const NS::Vector3& position,
                                       const NS::Vector3& target)
{
    TestViewCameraHost* host = scene.SpawnTransient<TestViewCameraHost>();
    if (host == nullptr)
    {
        return nullptr;
    }
    host->Vcam().SetPose(position, target);
    return &host->Vcam();
}
