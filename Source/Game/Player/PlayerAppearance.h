#pragma once

#include "Runtime/Object/Component.h"
#include "Runtime/Object/Reflection/Reflection.h"

#include <string>

namespace NS::Gfx
{
    class Mesh;
}

namespace NS::Game::Player
{
    //! @brief 自機の立ち姿と玉の 2 つの見た目を持ち、丸まる時に同居する MeshRenderer の mesh を持ち替える
    //! @details 見た目の欄が空なら仮の形を使う。立ち姿は当たりのカプセルと同じ寸法のカプセル、玉は同じ半径の球
    //! 寸法は PlayerComponent が移動に使うカプセルから引き、見た目の側には数を持たない
    //! 欄に ContentRoot 相対の参照を書けば、そのファイルの mesh を使う。引き当てられない参照は仮の形へ戻す
    //! 根の Transform は書かない。位置は移動、スケールは構えと衝突の潰れが持つ
    //! 優先度は Update 帯の +50。配置物を組む経路 (ObjectFromJson / StartSpawned) では参照の引き当てが
    //! component の並び順に回るので、MeshRenderer (Update) が自分の参照から mesh を差した後にこちらが差す
    //! 依存: PlayerComponent, NS::Obj::MeshRenderer, NS::Obj::AssetManager
    // TODO: Scene::ApplyFromJson は値の変わった component だけ引き直す。MeshRenderer の値の undo で mesh が
    // 組み込みの cube に戻り、CapsuleCollider の寸法の変更に見た目が付いてこない。編集へ戻る時の LoadJson で直る
    class PlayerAppearance : public NS::Obj::Component
    {
    public:
        PlayerAppearance() noexcept;

        //! 玉の見た目へ持ち替える。既に玉なら何もしない
        void Curl() noexcept;
        //! 立ち姿へ戻す。既に立ち姿なら何もしない
        void Uncurl() noexcept;
        //! 玉の見た目の場合 true、それ以外の場合は false
        [[nodiscard]] bool IsCurled() const noexcept { return m_curled; }

        //! 2 つの見た目を引き当て、今の姿の mesh を同居する MeshRenderer へ差す
        void ResolveAssets(NS::Obj::AssetManager& assets) override;

        NS_REFLECT_BEGIN(PlayerAppearance, NS::Obj::Component)
        NS_REFLECT_FIELD(m_standingMeshRef, "立ち姿のメッシュ")
        NS_REFLECT_FIELD(m_ballMeshRef, "玉のメッシュ")
        NS_REFLECT_END()

    private:
        // 今の姿の mesh を同居する MeshRenderer へ差す
        void ShowCurrentLook() noexcept;

        // 保存・編集される参照文字列。空は仮の形。ResolveAssets が実体を当てる
        std::string m_standingMeshRef{};
        std::string m_ballMeshRef{};
        NS::Gfx::Mesh* m_standingMesh = nullptr; // AssetManager 所有
        NS::Gfx::Mesh* m_ballMesh = nullptr;     // AssetManager 所有
        bool m_curled = false;
    };
} // namespace NS::Game::Player
