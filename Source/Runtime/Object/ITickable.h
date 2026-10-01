#pragma once

namespace NS::Obj
{
    //! @brief 配置物の部品でないのに、世界の更新の決まった所で毎フレーム動く物
    //! @details CourseDirector と HitSensorDirector は ObjectList::AddTicker で段を決めて登録する。
    //! 同じ段の中では Actor より先に動く
    //! CameraManager は登録せず、Scene::OnUpdate が Camera の段の後に直接呼ぶ
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
