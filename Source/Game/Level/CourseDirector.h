#pragma once

#include "NSlib/Core/Coroutine.h"
#include "NSlib/Object/SubObject.h"
#include "NSlib/Object/System/SceneSystem.h"

namespace NS::Obj
{
    class Actor;
    class Scene;
} // namespace NS::Obj

namespace GL::Level
{
    //! @brief 死とゴールに応じてコースを再開するシーン共通処理
    //! @details クリア時は操作を止め、世界を動かしたまま暗転・再開・明転する
    class CourseDirector final : public NS::Obj::SceneSystem
    {
    public:
        explicit CourseDirector(NS::Obj::Scene& scene);
        ~CourseDirector() noexcept override;

        //! 次の OnTick でコースをやり直す
        void NotifyPlayerDead() noexcept { m_playerDead = true; }

        //! 次の OnTick で、自機以外の配置物を開始時の姿へ戻す
        void RequestRespawnObjects() noexcept { m_respawnPending = true; }

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

        //! 現在の配置を戻り先として保存し、RestartCourse と同じ処理で始める
        void StartCourse();

        //! @brief コースを最初からやり直す。全ての配置物へ MsgCourseRestart を送る
        //! @details 凍結 (StartCourse が取ったシーンの JSON 文書) にある姿へ、受け手が自分で戻る
        void RestartCourse();

        //! @brief 自機の他の配置物へ MsgCourseRestart を送り、凍結の姿へ戻す。自機の位置と状態は変えない
        void RespawnObjects();

        //! 知らせを受けた分だけ流れを進める
        void OnTick() override;

    private:
        [[nodiscard]] NS::Coroutine ClearSequence();

        void SendInputLock(bool locked);

        //! skip の他の、一時オブジェクトでない配置物へ MsgCourseRestart を送る。skip が nullptr なら全員へ
        void SendRestart(const NS::Obj::Actor* skip);

        float m_fadeOutSeconds = 0.0f;
        float m_fadeInSeconds = 0.0f;

        NS::Obj::Scene& m_scene;
        NS::CoroutineRunner m_sequences; // クリアの流れ
        bool m_playerDead = false;       // 次の段でやり直すか
        bool m_respawnPending = false;
        bool m_goalReached = false; // 次の段でクリアの流れを始めるか
    };
} // namespace GL::Level
