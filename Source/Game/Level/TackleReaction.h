#pragma once

#include "Game/Level/LevelMessages.h"
#include "Runtime/Core/Math.h"
#include "Runtime/Object/Component.h"

namespace NS::Game::Level
{
    //! @brief 体当たりを受けた物の応じ方。置物に載せ、置物が受けた体当たりの知らせをここへ渡す
    //! @details 問いには自分の重さ・耐久・置かれているか・体の外接箱を答える
    //! 止めの頭で置かれていれば飛ぶ向きへ食い込み、止めの間は食い込み位置の周りで往復し、描く形だけを縮める
    //! 明けで元の位置と形へ戻り、床に跡を残してから、飛ぶか壊れる。飛んでいた物は食い込まず今の位置から飛ぶ
    //! 飛び方の曲線は当てた側が決めて知らせに載せる。どう飛ぶかの実際の動きは LaunchedBody が受け持つ
    //! 依存: LaunchedBody, LaunchEffects, Breakable, ImpactMark, NS::Obj::RigidBody, NS::Obj::MeshRenderer, NS::Obj::HitSensor
    class TackleReaction : public NS::Obj::Component
    {
    public:
        // 物理の更新が済んでから、止めの間の往復を置く
        TackleReaction() noexcept;

        //! 体当たりの相手としての答えを書く。受けられない時 (止まっている部品・壊れた後) は false
        [[nodiscard]] bool Answer(TackleTargetAnswer& outAnswer) const;

        //! 止めの頭。置かれていれば食い込み、形を縮め、止めの間の往復を始める
        void BeginFreeze(const TackleFreezeDesc& desc);

        //! 明け。元の位置と形へ戻り、床に跡を残してから飛ぶか壊れる
        void Release(const TackleReleaseDesc& desc);

        //! 止めの最中か
        [[nodiscard]] bool IsFrozen() const noexcept { return m_frozen; }

        //! 止めの間の往復を置く
        void OnUpdate() override;

        //! 縮めた形を戻して外れる
        void OnEndPlay() override;

        NS_REFLECT_BEGIN(TackleReaction, NS::Obj::Component)
        NS_REFLECT_FIELD(m_markProbeDistance, "跡の床探しの距離")
        NS_REFLECT_END()

    private:
        // 縮めた描く形を元へ戻す。縮めていなければ何もしない
        void RestoreShape();
        // 止めを終える。置かれていれば元の位置へ戻し、形も戻す
        void EndFreeze();
        // 自分の真下の床に跡を出す。床が見つからなければ出さない
        void SpawnMark();

        float m_markProbeDistance = 64.0f; // 跡の床を真下へ探す上限。これより下に床が無ければ跡を出さない

        bool m_frozen = false;                          // 止めの最中か
        bool m_placed = false;                          // 止めの頭で置かれていたか。食い込み・往復・元位置はこの時だけ
        bool m_shapeHeld = false;                       // 描く形を縮めたまま止めている最中か
        bool m_justBegan = false;                       // 止めの頭のフレームか。そのフレームは往復を置かない
        NS::Core::Vector3 m_home{0.0f, 0.0f, 0.0f};     // 止めの頭の位置。明けで厳密にここへ戻す
        NS::Core::Vector3 m_impactDir{1.0f, 0.0f, 0.0f}; // 食い込みと往復の軸
        float m_pushInDistance = 0.0f;                  // 食い込む距離
        float m_shakeAmplitude = 0.0f;                  // 往復の振れ幅
        int m_remaining = 0;                            // 止めの残りフレーム数
        int m_total = 0;                                // 止めのフレーム数。往復の減衰の分母
        int m_overrun = 0;                              // 止めの数え終わりから明けを待ったフレーム数
    };
} // namespace NS::Game::Level
