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
    class PlayerComponent;

    //! @brief 自機の立ち姿と玉の 2 つの見た目を持ち、丸まる時に同居する MeshRenderer の mesh を持ち替える
    //! @details 見た目の欄が空なら仮の形を使う。立ち姿は当たりのカプセルと同じ寸法のカプセル、玉は同じ半径の球
    //! 寸法は PlayerComponent が移動に使うカプセルから引き、見た目の側には数を持たない
    //! 欄に ContentRoot 相対の参照を書けば、そのファイルの mesh を使う。引き当てられない参照は仮の形へ戻す
    //! 根の Transform は書かない。位置は移動、スケールは構えと衝突の潰れが持つ
    //! 丸まっているかの正は同居する PlayerComponent が持ち、毎フレームそれを見た目へ写す
    //! 優先度は Update 帯の +50。配置物を組む経路 (ObjectFromJson / StartSpawned) では参照の引き当てが
    //! component の並び順に回るので、MeshRenderer (Update) が自分の参照から mesh を差した後にこちらが差す
    //! 依存: PlayerComponent, NS::Obj::MeshRenderer, NS::Obj::AssetManager
    // TODO: Scene::ApplyFromJson は値の変わった component だけ引き直す。MeshRenderer の値の undo で mesh が
    // 組み込みの cube に戻り、CapsuleCollider の寸法の変更に見た目が付いてこない。編集へ戻る時の LoadJson で直る
    class PlayerAppearance : public NS::Obj::Component
    {
    public:
        PlayerAppearance() noexcept;

        //! @brief 玉の見た目へ持ち替える。既に玉なら何もしない
        //! @details 同居する PlayerComponent があれば、次の OnUpdate でその丸まりに合わせ直す
        void Curl() noexcept;
        //! @brief 立ち姿へ戻す。既に立ち姿なら何もしない
        //! @details 同居する PlayerComponent があれば、次の OnUpdate でその丸まりに合わせ直す
        void Uncurl() noexcept;
        //! 玉の見た目の場合 true、それ以外の場合は false
        [[nodiscard]] bool IsCurled() const noexcept { return m_curled; }

        //! 2 つの見た目を引き当て、今の姿の mesh を同居する MeshRenderer へ差す
        void ResolveAssets(NS::Obj::AssetManager& assets) override;

        //! 同居する PlayerComponent を控える。見つからなければ以後は丸まりを写さない
        void OnStart() override;
        //! PlayerComponent の丸まりを見た目へ写す
        void OnUpdate() override;

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
        const PlayerComponent* m_player = nullptr; // 丸まりの正。非所有
    };
} // namespace NS::Game::Player
