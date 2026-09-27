#pragma once

#include "Runtime/Core/Math.h"
#include "Runtime/Object/Component.h"
#include "Runtime/Object/Reflection/Reflection.h"

#include <string>

namespace NS::Gfx
{
    class Mesh;
}

namespace NS::Game::Level
{
    class CollisionInput;
}

namespace NS::Game::Player
{
    class PlayerComponent;

    //! @brief 自機の立ち姿と玉の 2 つの見た目を持ち、丸まる時に同居する MeshRenderer の mesh を持ち替える
    //! @details 見た目の欄が空なら仮の形を使う。立ち姿は当たりのカプセルと同じ寸法のカプセル、玉は同じ半径の球
    //! 寸法は PlayerComponent が移動に使うカプセルから引き、見た目の側には数を持たない
    //! 欄に ContentRoot 相対の参照を書けば、そのファイルの mesh を使う。引き当てられない参照は仮の形へ戻す
    //! 根の Transform は書かない。位置は移動、スケールは構えと衝突の潰れが持つ。着地の潰れは描く形だけを変える
    //! 丸まっているかの正は同居する PlayerComponent が持ち、毎フレームそれを見た目へ写す
    //! 玉の間は同居する MeshRenderer の局所の回転を回す。溜めている間は狙いの線の向きへ、溜めに入る前に
    //! 押している間と線の無い時は PlayerComponent::AimDirection へ、突進中は進む向きへ、
    //! 反動の間は弾かれた向きへ前転する
    //! 反動のまま着地したフレームに、同居する MeshRenderer の描く時だけの倍率で縦に潰し、決めたフレーム数で戻す。
    //! 跳びの着地は潰さない
    //! 溜め量の正は同居する CollisionInput の判定で、ここは読むだけ
    //! 優先度は Update 帯の +50。配置物を組む経路 (ObjectFromJson / StartSpawned) では参照の引き当てが
    //! component の並び順に回るので、MeshRenderer (Update) が自分の参照から mesh を差した後にこちらが差す
    //! 依存: PlayerComponent, NS::Game::Level::CollisionInput, NS::Obj::MeshRenderer, NS::Obj::AssetManager
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

        //! 同居する PlayerComponent と CollisionInput を控える。PlayerComponent が無ければ以後は丸まりを写さない
        void OnStart() override;
        //! PlayerComponent の丸まりを見た目へ写し、玉の回転と着地の潰れを 1 フレーム進める
        void OnUpdate() override;

        //! 直前の OnUpdate で玉が回った角度を度で返す。立ち姿と止めの間は 0
        [[nodiscard]] float SpinDegreesThisFrame() const noexcept { return m_spinDegreesThisFrame; }
        //! @brief 玉を回している軸を返す
        //! @details 根の空間の水平の単位ベクトル。正の角度で、玉の上面が 軸 × 上 の向きへ倒れる前転になる
        //! @return 直前の OnUpdate で回した軸。止めの間は止まる前の軸、立ち姿では (1, 0, 0)
        [[nodiscard]] NS::Core::Vector3 SpinAxis() const noexcept { return m_spinAxis; }

        NS_REFLECT_BEGIN(PlayerAppearance, NS::Obj::Component)
        NS_REFLECT_FIELD(m_standingMeshRef, "立ち姿のメッシュ")
        NS_REFLECT_FIELD(m_ballMeshRef, "玉のメッシュ")
        NS_REFLECT_FIELD(m_emptyChargeSpinSpeed, "溜め 0 の回る速さ")
        NS_REFLECT_FIELD(m_fullChargeSpinSpeed, "溜めきりの回る速さ")
        NS_REFLECT_FIELD(m_bodySlamSpinSpeed, "突進中の回る速さ")
        NS_REFLECT_FIELD(m_landingSquash, "着地の潰れ")
        NS_REFLECT_FIELD(m_landingSquashRecoverSteps, "着地の潰れを戻すフレーム数")
        NS_REFLECT_END()

    private:
        // 今の姿の mesh を同居する MeshRenderer へ差す
        void ShowCurrentLook() noexcept;
        // 玉の回転を 1 フレーム進め、同居する MeshRenderer の局所の回転へ書く
        void AdvanceSpin() noexcept;
        // 反動の着地で描く形を潰すか、潰れを 1 フレーム戻し、同居する MeshRenderer の描く時だけの倍率へ書く
        void AdvanceLandingSquash() noexcept;

        // 保存・編集される参照文字列。空は仮の形。ResolveAssets が実体を当てる
        std::string m_standingMeshRef{};
        std::string m_ballMeshRef{};
        NS::Gfx::Mesh* m_standingMesh = nullptr; // AssetManager 所有
        NS::Gfx::Mesh* m_ballMesh = nullptr;     // AssetManager 所有
        bool m_curled = false;
        const PlayerComponent* m_player = nullptr;                // 丸まりの正。非所有
        const NS::Game::Level::CollisionInput* m_input = nullptr; // 溜め量の正。無い配置物もある。非所有

        // 回る速さは 3 つとも 1 フレーム 180 度未満 (1/60 秒のフレームで 10800 度/秒未満) で使う。超えると描く時の
        // 補間が短い側を通り、逆回りに見える
        float m_emptyChargeSpinSpeed = 360.0f; // 押した直後 (溜め 0) に玉が回る速さ。度/秒
        float m_fullChargeSpinSpeed = 1440.0f; // 溜めきった時に玉が回る速さ。度/秒
        float m_bodySlamSpinSpeed = 1800.0f;   // 突進中と反動の間に玉が転がる速さ。度/秒

        float m_landingSquash = 0.8f;        // 反動の着地のフレームの縦の倍率。水平は 1 ÷ √縦
        int m_landingSquashRecoverSteps = 6; // 着地の潰れから元の形へ戻すフレーム数。0 以下なら潰さない

        // 立ち姿の間の軸。丸まった直後に狙いが決まらなければこの軸で回る
        static constexpr NS::Core::Vector3 k_FirstSpinAxis{1.0f, 0.0f, 0.0f};

        NS::Core::Quaternion m_spin = NS::Core::Quaternion::Identity; // 玉の今の回転
        NS::Core::Vector3 m_spinAxis = k_FirstSpinAxis;               // 直前のフレームに回した軸
        float m_spinSpeed = 0.0f;                                     // 直前のフレームに回した速さ。度/秒
        float m_spinDegreesThisFrame = 0.0f;                          // 直前の OnUpdate で回った角度。度
        int m_landingSquashRemaining = 0; // 着地の潰れを戻し切るまでの残りフレーム数。0 は潰れていない
    };
} // namespace NS::Game::Player
