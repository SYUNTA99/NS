#pragma once

#include "Game/Level/HitZones.h"
#include "Game/Level/ImpactMarks.h"
#include "Game/Level/LaunchEffects.h"
#include "Game/Level/LevelMessages.h"
#include "Game/Level/SensorKinds.h"
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
        //! 動く・反応する置物。岩・箱・樽
        // 個体ごとの見た目と重さは個体の上書きで変える
        class PropActor : public NS::SceneActor
        {
        public:
            PropActor() noexcept;

            NS_REFLECT_BEGIN(PropActor, NS::SceneActor)
            NS_CLASSOBJ(m_params, "Params")
            NS_CLASSOBJ(m_hitZones, "HitZones")
            NS_CLASSOBJ(m_effects, "LaunchEffects")
            NS_CLASSOBJ(m_marks, "ImpactMarks")
            NS_REFLECT_END()

            //! 基底の作る物に続けて、尾と粉の絵を先に読む。.actor が替わって絵の名前が替わった時も読む
            void OnAssetsLoaded() override;
            //! 1 固定ステップを本番と同じ順で進める。試しが段を回さずに置物を動かす
            void UpdateMotion();
            void OnEndPlay() override;
            //! 体当たりの止めの最中の場合 true、それ以外の場合は false
            [[nodiscard]] bool IsFrozen() const noexcept;
            //! 押し飛ばされて曲線か転がりの最中かを返す
            //! 飛んでいる途中に止めが掛かった間も含む
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
            [[nodiscard]] ImpactMarks& Marks() noexcept { return *m_marks; }
            [[nodiscard]] const ImpactMarks& Marks() const noexcept { return *m_marks; }
            [[nodiscard]] TackleGhost* Ghost() noexcept { return NS::Cast<TackleGhost>(ChildActor()); }
            [[nodiscard]] const TackleGhost* Ghost() const noexcept { return NS::Cast<TackleGhost>(ChildActor()); }

        protected:
            //! 体当たりの問い・止め・明けと、コースのやり直しに応じる
            //! やり直しでは、プレイ開始時の凍結の自分の位置と向きへ置かれた物として戻る
            bool ReceiveMsg(const NS::Message& msg, NS::HitSensor* sender, NS::HitSensor* receiver) override;

            void OnInit() override;
            //! 残像は Model と同じく、出ている間だけ描く物へ入れる
            void OnAppear() override;
            void OnDisappear() noexcept override;
            //! 発光の層・残像・当たりの跡を進める
            void VisualStep() override;

        private:
            class RestingState;
            class FreezeState;
            class LaunchedState;
            class ArcState;
            class RollingState;

            // 受けた状態の頭の形と動き
            struct TackleFreezeDesc
            {
                NS::Vector3 impactDir{1.0f, 0.0f, 0.0f}; // 飛ぶ水平の向き。食い込みの向き
                float pushInDistance = 0.0f;             // 飛ぶ向きへ食い込ませる距離 (m)
                float squashThickness = 1.0f;            // 飛ぶ向きの厚みの倍率
                float squashHeight = 1.0f;               // 高さの倍率
                bool squash = false;                     // 形を潰すか
            };

            // 当たったの知らせを受ける。段の自分の束を触れた点と 2 つのセンサーで始め、受けた状態に入る
            void ReceiveHit(const TackleHitDesc& hit, const NS::HitSensor* self, const NS::HitSensor* attacker);
            // 受けた当たりの明けの束を始める。当たっていない・当てた側が居ない時は何もしない
            void StartReleaseSet();
            void BeginFreeze(const TackleFreezeDesc& desc);
            void EndFreeze();
            // 止めを解く。置かれていたか状態機械が終わっていれば Resting、他は Launched
            void LeaveFreeze();
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
            [[nodiscard]] const NS::SphereCollision& Sphere() const noexcept
            {
                return *static_cast<const NS::SphereCollision*>(CollisionClassObj());
            }
            // 体のセンサーの世界座標の形。OnInit が当たりの球を作った後にしか問われない
            [[nodiscard]] NS::SensorVolume BodySensorVolume(const NS::Matrix& rootWorld) const noexcept;
            NS::HitSensor* m_bodyHitSensor = nullptr; // 体のセンサー (非所有)。持つのは HitSensorsObj
            std::unique_ptr<MapObjParams> m_params;
            std::unique_ptr<HitZones> m_hitZones;
            std::unique_ptr<LaunchEffects> m_effects;
            std::unique_ptr<ImpactMarks> m_marks;
            NS::StateMachineOf<PropActor>* m_states = nullptr; // 基底が所有する。OnInit が預けた直後から有効
            NS::SubStateMachine<PropActor> m_motion;
            TackleFreezeDesc m_freeze;
            // 受けた当たり。明けの知らせで明けの束を始める時に読み、使い終えると active を下ろす
            struct ReceivedHit
            {
                const NS::HitSensor* attacker = nullptr;
                NS::Vector3 contact{};
                HitTier tier = HitTier::Center;
                bool active = false;
            };
            ReceivedHit m_received;
            struct CoursePose
            {
                NS::Vector3 position;
                NS::Quaternion rotation;
            };
            std::optional<CoursePose> m_coursePose; // MsgCourseStart の時の姿。やり直しで戻る先
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
