#pragma once

#include "NSlib/Core/Coroutine.h"
#include "NSlib/CoreObject/Object.h"
#include "NSlib/CoreObject/System/SceneSystem.h"

#include <vector>

namespace NS
{
    class SceneActor;
    class Scene;
} // namespace NS

namespace GL::Level
{
        //! @brief 死とゴールに応じてコースを再開するシーン共通処理
        //! @details クリア時は操作を止め、世界を動かしたまま暗転・再開・明転する
        class CourseDirector final : public NS::SceneSystem
        {
        public:
            explicit CourseDirector(NS::Scene& scene);
            ~CourseDirector() noexcept override;

            //! 次の OnTick でコースをやり直す
            void NotifyPlayerDead() noexcept { m_playerDead = true; }

            //! 次の OnTick で、自機以外のアクターを開始時の姿へ戻す
            void RequestRespawnActors() noexcept { m_respawnPending = true; }

            //! 次の OnTick でクリア演出を始める。演出中は無視する
            void NotifyGoal(float fadeOutSeconds, float fadeInSeconds) noexcept
            {
                if (m_sequences.IsRunning() || m_goalReached)
                {
                    return;
                }
                m_fadeOutSeconds = fadeOutSeconds;
                m_fadeInSeconds = fadeInSeconds;
                m_goalReached = true;
            }

            [[nodiscard]] bool IsClearing() const noexcept { return m_sequences.IsRunning(); }

            //! @brief 全てのアクターへ MsgCourseStart を送って今の姿を控えさせ、RestartCourse と同じ処理で始める
            void StartCourse();

            //! @brief コースを最初からやり直す。全てのアクターへ MsgCourseRestart を送る
            //! @details 受け手は StartCourse の時に控えた姿へ自分で戻る
            void RestartCourse();

            //! @brief 自機の他のアクターへ MsgCourseRestart を送り、開始の時の姿へ戻す。自機の位置と状態は変えない
            void RespawnActors();

            //! 知らせを受けた分だけ流れを進める
            void OnTick() override;

        private:
            [[nodiscard]] NS::Coroutine ClearSequence();

            void SendInputLock(bool locked);

            //! skip の他のアクター。skip が nullptr なら全員
            [[nodiscard]] std::vector<NS::SceneActor*> PlacedActors(const NS::SceneActor* skip) const;

            //! PlacedActors の全員へ MsgCourseRestart を送る
            void SendRestart(const NS::SceneActor* skip);

            float m_fadeOutSeconds = 0.0f;
            float m_fadeInSeconds = 0.0f;

            NS::Scene& m_scene;
            NS::CoroutineRunner m_sequences; // クリアの流れ
            bool m_playerDead = false;       // 次の段でやり直すか
            bool m_respawnPending = false;
            bool m_goalReached = false; // 次の段でクリアの流れを始めるか
        };
} // namespace GL::Level
