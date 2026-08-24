#pragma once

#include "Runtime/Object/Component.h"

// コヨーテ猶予のデバッグ描画 (開発ビルド限定)
// F2 で表示を切り替え、 縁の猶予範囲とジャンプ実行点を線で描く

#if !defined(NS_SHIPPING)

namespace NS::Game::Level
{
    //! @brief コヨーテ猶予のデバッグ線を毎フレーム積む
    //! @details 線は DebugDraw に溜まり、 scene の標準描画が吐き出す
    //! プレイヤーに載せる。描く相手は自分の owner
    class CoyoteDebugComponent : public NS::Object::Component
    {
    public:
        void OnUpdate() override;

        // 状態は保存しない。型検索で引けるよう型名だけ登録する
        NS_REFLECT_NONE(CoyoteDebugComponent, NS::Object::Component)

    private:
        bool m_draw = true; // F2 で切替
    };

} // namespace NS::Game::Level

#endif
