#pragma once

#include "Game/Level/HitZones.h"
#include "Game/Level/LaunchEffects.h"
#include "Game/Level/LevelMessages.h"
#include "Game/Level/MapObjParams.h"
#include "Runtime/Object/Actor.h"
#include "Runtime/Object/Components/HitSensor.h"
#include "Runtime/Object/Components/Model.h"
#include "Runtime/Object/Components/SphereCollision.h"
#include "Runtime/Object/StateMachine.h"

namespace NS::Game::Level
{
    //! @brief 動く・反応する置物。岩・箱・樽
    //! 影は種類の既定値が足す。個体ごとの見た目と重さは個体の上書きで変える
    class MapObj : public NS::Obj::Actor
    {
    public:
        MapObj() noexcept;
        void ForEachPart(const PartVisitor& visitor) const override;

        //! 保存形式と TypeRegistry の登録名。読込はこの名前で Actor の型を選ぶ
        NS_REFLECT_NONE(MapObj, NS::Obj::Actor)

        //! 配置が揃った後に、当たりの球を物理へ入れる
        void InitAfterPlacement() override;

        //! @brief 状態機械の段と身体の段を 1 回ずつ進める
        //! @details 状態機械を 1 固定ステップ進め、当たりの球が動いていれば物理へ置き直す
        void UpdateMotion();
        void OnEndPlay() override;
        //! 体当たりの止めの最中の場合 true、それ以外の場合は false
        [[nodiscard]] bool IsFrozen() const noexcept;
        //! @brief 押し飛ばされて曲線か転がりの最中かを返す
        //! @details 飛んでいる途中に止めが掛かった間も含む
        //! @return 飛んでいる場合 true、それ以外の場合は false
        [[nodiscard]] bool IsFlying() const noexcept;
        //! 飛んでいる中でも曲線の上にいる場合 true、それ以外の場合は false
        [[nodiscard]] bool IsArc() const noexcept;
        //! 自分で動かしている速度。単位は m/s。置かれている間は 0
        [[nodiscard]] NS::Core::Vector3 Velocity() const noexcept { return m_velocity; }
        [[nodiscard]] const MapObjParams& Params() const noexcept { return m_params; }

        //! 体当たりの問い・止め・明けと、コースのやり直しに応じる
        //! やり直しでは、プレイ開始時の凍結の自分の位置と向きへ置かれた物として戻る
        bool ReceiveMsg(const NS::Obj::Message& msg, NS::Obj::HitSensor* sender, NS::Obj::HitSensor* receiver) override;

    protected:
        //! 発光の層の歩を始める
        void ObserveStep() override;
        //! 当たりの球が動いていれば物理へ置き直す
        void BodyStep() override;
        //! 発光の層を進める。HitReaction は進めない
        void VisualStep() override;

    private:
        class RestingState;
        class FreezeState;
        class LaunchedState;
        class ArcState;
        class RollingState;

        void BeginFreeze(const TackleFreezeDesc& desc);
        void EndFreeze();
        void StepFreeze();
        void Release(const TackleReleaseDesc& desc);
        void ResetTo(const NS::Core::Vector3& position, const NS::Core::Quaternion& rotation);
        void StepLaunched(float dt);
        void StepArc(float dt);
        void StepRolling(float dt);
        void MoveLaunched(float dt);
        bool ProbeFloor(float distance, NS::Core::Vector3& outNormal);
        void Land(const NS::Core::Vector3& normal);
        void SyncCollision();
        void SpawnMark();
        [[nodiscard]] NS::Core::Vector3 ArcOffset(float seconds) const noexcept;

        [[nodiscard]] NS::Obj::SphereCollision& Sphere() noexcept
        {
            return *static_cast<NS::Obj::SphereCollision*>(CollisionPart());
        }
        MapObjParams m_params;
        HitZones m_hitZones;
        LaunchEffects m_effects;
        NS::Obj::StateMachine<MapObj>* m_states = nullptr; // 基底が所有する。コンストラクタが預けた直後から有効
        NS::Obj::SubStateMachine<MapObj> m_motion;
        TackleFreezeDesc m_freeze;
        NS::Core::Vector3 m_freezeHome{};
        bool m_freezePlaced = true;
        LaunchArc m_arc;
        NS::Core::Vector3 m_arcUp{0.0f, 1.0f, 0.0f};
        NS::Core::Vector3 m_arcForward{1.0f, 0.0f, 0.0f};
        NS::Core::Vector3 m_velocity{};
        float m_arcSeconds = 0.0f;
        float m_restAge = 0.0f;
        bool m_hasLaunched = false;
        bool m_arcDeflected = false;
        NS::Core::Sphere m_syncedSphere{}; // 最後に当たりへ置いた球 (世界座標)
        bool m_hasSyncedSphere = false;    // m_syncedSphere を一度でも置いたか
    };
} // namespace NS::Game::Level
