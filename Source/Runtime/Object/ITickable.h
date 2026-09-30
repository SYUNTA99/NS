#pragma once

namespace NS::Obj
{
    //! @brief 配置物の部品でないのに、世界の更新の決まった所で毎フレーム動く物
    //! @details シーンに 1 つの物 (進行役など) と当たりの調べ役が持つ。ObjectList::AddTicker で帯の位置を決めて登録する
    //! 同じ帯の中では部品より先に動く。オデッセイの ExecuteDirector へ登録する IUseExecutor に当たる
    class ITickable
    {
    public:
        //! 固定ステップで 1 回呼ばれる。シミュレーションが止まっている間は呼ばれない
        virtual void OnTick() = 0;

    protected:
        ~ITickable() = default;
    };
} // namespace NS::Obj
