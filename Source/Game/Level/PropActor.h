#pragma once

#include "Game/Level/HitZones.h"
#include "Game/Level/LaunchEffects.h"
#include "Game/Level/LevelMessages.h"
#include "Game/Level/MapObjParams.h"
#include "Game/Level/PropFlight.h"
#include "Game/Level/TackleGhost.h"
#include "NSlib/Core/Types.h"
#include "NSlib/CoreObject/ClassObjs/HitSensor.h"
#include "NSlib/CoreObject/ClassObjs/Model.h"
#include "NSlib/CoreObject/ClassObjs/SphereCollision.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/CoreObject/StateMachineOf.h"

#include <optional>

namespace GL
{
    namespace Level
    {
        //! @brief 動く・反応する置物。岩・箱・樽
        // 個体ごとの見た目と重さは個体の上書きで変える
        class PropActor : public NS::SceneActor
        {
        public:
            PropActor() noexcept;

            NS_REFLECT_BEGIN(PropActor, NS::SceneActor)
            NS_CLASSOBJ(m_params, "Params")
            NS_CLASSOBJ(m_hitZones, "HitZones")
            NS_CLASSOBJ(m_effects, "LaunchEffects")
            NS_REFLECT_END()

            //! 尾と粉の絵を先に読む。残像は Model と同じく、出ている間だけ描く物へ入れる
            void OnStart() override;
            //! 1 固定ステップを本番と同じ順で進める。試しが段を回さずに置物を動かす
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
            //! 今どう飛んでいるか。呼ばれた時の状態機械から組む
            [[nodiscard]] PropFlight Flight() const noexcept;
            [[nodiscard]] MapObjParams& Params() noexcept { return *m_params; }
            [[nodiscard]] const MapObjParams& Params() const noexcept { return *m_params; }
            [[nodiscard]] HitZones& Zones() noexcept { return *m_hitZones; }
            [[nodiscard]] const HitZones& Zones() const noexcept { return *m_hitZones; }
            [[nodiscard]] LaunchEffects& Effects() noexcept { return *m_effects; }
            [[nodiscard]] const LaunchEffects& Effects() const noexcept { return *m_effects; }
            [[nodiscard]] const TackleGhost& Ghost() const noexcept { return m_tackleGhost; }

        protected:
            //! 体当たりの問い・止め・明けと、コースのやり直しに応じる
            //! やり直しでは、プレイ開始時の凍結の自分の位置と向きへ置かれた物として戻る
            bool ReceiveMsg(const NS::Message& msg, NS::HitSensor* sender, NS::HitSensor* receiver) override;

            void OnInit() override;
            void OnAppear() override;
            void OnDisappear() noexcept override;
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
            void AdvanceGhost();
            void StopGhost();
            void Release(const TackleReleaseDesc& desc);
            void ResetTo(const NS::Vector3& position, const NS::Quaternion& rotation);
            void StepLaunched(float dt);
            void StepArc(float dt);
            void StepRolling(float dt);
            void MoveLaunched(float dt);
            bool ProbeFloor(float distance, NS::Vector3& outNormal);
            void Land(const NS::Vector3& normal);
            void SpawnMark();
            [[nodiscard]] NS::Vector3 ArcOffset(float seconds) const noexcept;

            [[nodiscard]] NS::SphereCollision& Sphere() noexcept
            {
                return *static_cast<NS::SphereCollision*>(CollisionClassObj());
            }
            std::unique_ptr<MapObjParams> m_params;
            std::unique_ptr<HitZones> m_hitZones;
            std::unique_ptr<LaunchEffects> m_effects;
            NS::StateMachineOf<PropActor>* m_states = nullptr; // 基底が所有する。OnInit が預けた直後から有効
            NS::SubStateMachine<PropActor> m_motion;
            TackleFreezeDesc m_freeze;
            // 残像。知らせを受けたフレームの見た目の 1 歩を 1 フレーム目に数え、止めの後も状態に依らず進める
            struct GhostRun
            {
                TackleGhostDesc desc{};
                int frame = 0;
                bool active = false;
            };
            GhostRun m_ghost;
            TackleGhost m_tackleGhost{this}; // 体当たりの残像。Model を借りて描く
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
            int m_hopsLeft = 0; // 外れで着地した後に残っている跳ねの回数。外れでなければ 0
            int m_hopIndex = 0; // 次の跳ねが何回目か。0 から数える
            NS::uint32 m_hopSeed = 0;
        };
    } // namespace Level
} // namespace GL
