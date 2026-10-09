#pragma once

#include "Game/Level/HitZones.h"
#include "Game/Level/LaunchEffects.h"
#include "Game/Level/LevelMessages.h"
#include "Game/Level/MapObjParams.h"
#include "NSlib/Object/Actor.h"
#include "NSlib/Object/StateMachine.h"
#include "NSlib/Object/SubObjects/HitSensor.h"
#include "NSlib/Object/SubObjects/Model.h"
#include "NSlib/Object/SubObjects/SphereCollision.h"

#include <optional>

namespace GL::Level
{
    //! @brief 動く・反応する置物。岩・箱・樽
    //! 影は種類の既定値が足す。個体ごとの見た目と重さは個体の上書きで変える
    class MapObj : public NS::Obj::Actor
    {
    public:
        MapObj() noexcept;

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
        [[nodiscard]] NS::Vector3 Velocity() const noexcept { return m_velocity; }
        [[nodiscard]] const MapObjParams& Params() const noexcept { return *m_params; }

        //! 体当たりの問い・止め・明けと、コースのやり直しに応じる
        //! やり直しでは、プレイ開始時の凍結の自分の位置と向きへ置かれた物として戻る
        bool ReceiveMsg(const NS::Obj::Message& msg, NS::Obj::HitSensor* sender, NS::Obj::HitSensor* receiver) override;

    protected:
        void OnInit() override;
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
        // 止めを解く。置かれていたか状態機械が終わっていれば Resting、他は Launched
        void LeaveFreeze();
        void StepFreeze();
        void AdvanceShake();
        // 止めの間の揺れを止め、描く時だけのずれを 0 へ戻す
        void StopShake();
        void AdvanceGhost();
        // 残像を止め、左右の写しと明けの写しを消す
        void StopGhost();
        // 衝撃の震えを 1 フレーム進めて描く所へ書く。長さの終わりで振れ幅 0 を書いて止める
        void AdvanceTremor();
        // 衝撃の震えを止め、振れ幅 0 を書く
        void StopTremor();
        void Release(const TackleReleaseDesc& desc);
        void ResetTo(const NS::Vector3& position, const NS::Quaternion& rotation);
        void StepLaunched(float dt);
        void StepArc(float dt);
        void StepRolling(float dt);
        void MoveLaunched(float dt);
        bool ProbeFloor(float distance, NS::Vector3& outNormal);
        void Land(const NS::Vector3& normal);
        void SyncCollision();
        void SpawnMark();
        [[nodiscard]] NS::Vector3 ArcOffset(float seconds) const noexcept;

        [[nodiscard]] NS::Obj::SphereCollision& Sphere() noexcept
        {
            return *static_cast<NS::Obj::SphereCollision*>(CollisionSubObj());
        }
        MapObjParams* m_params = nullptr;
        HitZones* m_hitZones = nullptr;
        LaunchEffects* m_effects = nullptr;
        NS::Obj::StateMachine<MapObj>* m_states = nullptr; // 基底が所有する。OnInit が預けた直後から有効
        NS::Obj::SubStateMachine<MapObj> m_motion;
        TackleFreezeDesc m_freeze;
        // 止めの間の揺れ。知らせを受けたフレームの止めの 1 歩を 1 フレーム目に数える
        struct ShakeRun
        {
            TackleShakeDesc desc{};
            int frame = 0;
            bool active = false;
        };
        ShakeRun m_shake;
        // 残像。知らせを受けたフレームの見た目の 1 歩を 1 フレーム目に数え、止めの後も状態に依らず進める
        struct GhostRun
        {
            TackleGhostDesc desc{};
            int frame = 0;
            bool active = false;
        };
        GhostRun m_ghost;
        // 衝撃の震え。知らせを受けたフレームの見た目の 1 歩を 0 フレーム目に数え、状態に依らず進める
        struct TremorRun
        {
            TackleTremorDesc desc{};
            int elapsed = -1;
            bool active = false;
        };
        TremorRun m_tremor;
        NS::Vector3 m_freezeHome{};
        bool m_freezePlaced = true;
        LaunchArc m_arc;
        NS::Vector3 m_arcUp{0.0f, 1.0f, 0.0f};
        NS::Vector3 m_arcForward{1.0f, 0.0f, 0.0f};
        NS::Vector3 m_velocity{};
        float m_arcSeconds = 0.0f;
        float m_restAge = 0.0f;
        bool m_hasLaunched = false;
        bool m_arcDeflected = false;
        int m_hopsLeft = 0;                       // 外れで着地した後に残っている跳ねの回数。外れでなければ 0
        int m_hopIndex = 0;                       // 次の跳ねが何回目か。0 から数える
        std::uint32_t m_hopSeed = 0;              // 跳ね方の種
        std::optional<NS::Sphere> m_syncedSphere; // 最後に当たりへ置いた世界座標の球。置く前は空
    };
} // namespace GL::Level
