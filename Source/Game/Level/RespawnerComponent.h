#pragma once

#include "Runtime/Object/Component.h"

namespace NS::Game::Level
{
    //! @brief 命が尽きた owner を出現位置へ戻す。プレイヤーに載せる
    //! @details 判定が出そろった後、同じ LateUpdate 内で走行を最初からやり直す
    //! 何で死んだかは知らない。リスタートへ演出を挟みたくなったらここにシーケンスを足す
    class RespawnerComponent : public NS::Object::Component
    {
    public:
        RespawnerComponent() noexcept;

        void OnUpdate() override;

        //! 走行を最初からやり直す。出現位置は凍結スナップショットからその都度読む
        //! クリアシーケンスの finisher も全黒の裏でこれを呼ぶ
        void RestartRun() noexcept;

        // 状態は保存しない。型検索で引けるよう型名だけ登録する
        NS_REFLECT_NONE(RespawnerComponent, NS::Object::Component)
    };
} // namespace NS::Game::Level
