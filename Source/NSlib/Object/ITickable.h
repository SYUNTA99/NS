#pragma once

namespace NS::Obj
{
    //! @brief 配置物の部品でないのに、世界の更新の決まった所で毎フレーム動く物
    //! @details ActorList::AddTicker で段を決めて登録する。同じ段の中では、その段の Actor が配置の並びで先に動き、
    //! 登録物がその後に登録順で動く。登録物は段の Actor が出した物を受けてまとめる役で、
    //! HitSensorDirector (Sensors)・CourseDirector (Course)・CameraManager (Camera)・開いている UIActor (UI)・
    //! SceneRenderer のエフェクトの世界 (Effects) が居る
    //! オデッセイの ExecuteDirector へ登録する IUseExecutor に当たる
    class ITickable
    {
    public:
        //! 固定ステップで 1 回呼ばれる。シミュレーションが止まっている間は呼ばれない
        virtual void OnTick() = 0;

    protected:
        ~ITickable() = default;
    };
} // namespace NS::Obj
