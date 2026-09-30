#pragma once

#include "Runtime/Object/Actor.h"

namespace NS::Game::Level
{
    //! @brief 動く・反応する置物。岩・箱・樽
    //! @details 見た目・球の当たり・キネマティックの RigidBody・耐久・押し飛ばされた後の動き・物の体のセンサーを持つ
    //! 影は種類の既定値が足す。個体ごとの見た目と重さは個体の上書きで変える
    //! 体当たりは知らせで受け取る。問いには自分の重さと置かれ方を答え、止めと明けで食い込み・縮み・飛ぶ・壊れる
    class MapObj : public NS::Obj::Actor
    {
    public:
        MapObj() noexcept;

        //! 保存形式と TypeRegistry の登録名。読込はこの名前で Actor の型を選ぶ
        [[nodiscard]] const char* ClassName() const noexcept override { return "MapObj"; }

        //! 物の体のセンサーの形を、当たりの球に合わせる
        void InitAfterPlacement() override;

        //! 体当たりの問い・止め・明けと、コースのやり直しに応じる
        //! やり直しでは、プレイ開始時の凍結の自分の位置と向きへ置かれた物として戻る
        bool ReceiveMsg(const NS::Obj::Message& msg, NS::Obj::HitSensor* sender, NS::Obj::HitSensor* receiver) override;
    };
} // namespace NS::Game::Level
