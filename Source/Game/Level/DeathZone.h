#pragma once

#include "NSlib/Object/Actor.h"

namespace NS::Game::Level
{
    //! @brief 触れたプレイヤーを即死させる範囲。奈落の下へ大きく置き、落下死をレベルのデータとして表す
    //! @details 箱の範囲のセンサーを持ち、プレイヤーの体に重なったら MsgInstantDeath
    //! を送る。死んだ後どうするかは受け手が決める
    //! 地形の当たりは持たない。持つと落ちてきたプレイヤーが上面に着地してしまう
    class DeathZone : public NS::Obj::Actor
    {
    public:
        //! 保存形式と TypeRegistry の登録名。読込はこの名前で Actor の型を選ぶ
        NS_REFLECT_NONE(DeathZone, NS::Obj::Actor)

        //! 範囲に入ったプレイヤーの体へ MsgInstantDeath を送る
        void AttackSensor(NS::Obj::HitSensor& self, NS::Obj::HitSensor& other) override;

    protected:
        void Init() override;
    };
} // namespace NS::Game::Level
