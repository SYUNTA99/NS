#pragma once

#include "NSlib/Core/Coroutine.h"
#include "NSlib/Object/ITickable.h"
#include "NSlib/Object/Scene/SceneObjHolder.h"
#include "NSlib/Object/SubObject.h"

namespace NS::Obj
{
    class Actor;
    class Scene;
} // namespace NS::Obj

namespace GL::Level
{
    //! @brief プレイヤーの死とゴールに応じてコースを進める
    //! @details クリア時は操作停止、暗転、やり直し、明転の順で進む
    //! 暗転はシーンの画面描画機能を呼び、世界は止めない
    //! シーンの組み直しで捨てられ、最初の状態から作り直される
    class CourseDirector final : public NS::Obj::ISceneObj, public NS::Obj::ITickable
    {
    public:
        explicit CourseDirector(NS::Obj::Scene& scene);
        ~CourseDirector() noexcept override;

        //! プレイヤーの命が尽きた知らせ。次の進行役の段でコースを最初からやり直す
        void NotifyPlayerDead() noexcept { m_playerDead = true; }

        //! 置物の出し直しのボタンが押された知らせ。次の進行役の段で、自機の他の配置物を凍結の姿へ戻す
        void RequestRespawnObjects() noexcept { m_respawnPending = true; }

        //! ゴールに着いた知らせ。次の進行役の段でクリアの流れを始める。流れの最中は無視する
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

        //! クリアの流れの最中か
        [[nodiscard]] bool IsClearing() const noexcept { return m_sequences.IsRunning(); }

        //! @brief 今の配置を凍結し、そこから最初の走行をやり直しと同じ道で始める
        //! @details プレイの始まりはここだけを通る。呼ぶのは Game がシーンを立てた直後とエディタのプレイ突入
        //! 1 走目も死んだ後や 2 周目と同じ RestartCourse を通るので、どの周も同じ始まり方になる
        void StartCourse();

        //! @brief コースを最初からやり直す。全ての配置物へ MsgCourseRestart を送る
        //! @details 凍結 (StartCourse が取ったシーンの JSON 文書) にある姿へ、受け手が自分で戻る
        void RestartCourse();

        //! @brief 自機の他の配置物へ MsgCourseRestart を送り、凍結の姿へ戻す。自機の位置と状態は変えない
        void RespawnObjects();

        //! 知らせを受けた分だけ流れを進める
        void OnTick() override;

    private:
        //! クリアの流れ: 操作を止める → 暗転 → 全黒の裏でやり直す → 明転 → 操作を戻す
        [[nodiscard]] NS::Coroutine ClearSequence();

        //! プレイヤーへ操作を止めるか戻す知らせを送る
        void SendInputLock(bool locked);

        //! skip の他の、一時オブジェクトでない配置物へ MsgCourseRestart を送る。skip が nullptr なら全員へ
        void SendRestart(const NS::Obj::Actor* skip);

        float m_fadeOutSeconds = 0.0f;
        float m_fadeInSeconds = 0.0f;

        NS::Obj::Scene& m_scene;
        NS::CoroutineRunner m_sequences;             // クリアの流れ
        bool m_playerDead = false;                   // 次の段でやり直すか
        bool m_respawnPending = false;
        bool m_goalReached = false; // 次の段でクリアの流れを始めるか
    };
} // namespace GL::Level
