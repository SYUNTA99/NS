#pragma once

#include "NSlib/Core/Coroutine.h"
#include "NSlib/Object/SubObject.h"
#include "NSlib/Object/ITickable.h"
#include "NSlib/Object/Scene/SceneObjHolder.h"

#include <memory>

namespace NS::Obj
{
    class Scene;
    class ScreenFade;
} // namespace NS::Obj

namespace NS::Game::Level
{
    //! @brief コースの進行役。シーンに 1 つの物で、プレイヤーの死とゴールに応じてコースの流れを進める
    //! @details プレイヤーは知らせを受けて進行役へ伝えるだけで、流れは知らない
    //! 死んだら同じ段でコースを最初からやり直す。ゴールに着いたらクリアの流れを流す: 操作を止める → 暗転 →
    //! 全黒の裏でやり直す → 明転 → 操作を戻す。世界は止めない
    //! やり直しは全ての配置物へ MsgCourseRestart を送り、戻り方は受け手が決める
    //! 暗転は Object 層の ScreenFade を開いて持つ。クリアで暗転する理由を知るのは CourseDirector だけ
    //! シーンの組み直しで捨てられ、最初の状態から作り直される
    class CourseDirector final : public NS::Obj::ISceneObj, public NS::Obj::ITickable
    {
    public:
        explicit CourseDirector(NS::Obj::Scene& scene);
        ~CourseDirector() noexcept override;

        //! プレイヤーの命が尽きた知らせ。次の進行役の段でコースを最初からやり直す
        void NotifyPlayerDead() noexcept { m_playerDead = true; }

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

        //! 暗転。テストと演出の確認が読む
        [[nodiscard]] const NS::Obj::ScreenFade& Fade() const noexcept { return *m_fade; }

        //! @brief 今の配置を凍結し、そこから最初の走行をやり直しと同じ道で始める
        //! @details プレイの始まりはここだけを通る。呼ぶのは Game がシーンを立てた直後とエディタのプレイ突入
        //! 1 走目も死んだ後や 2 周目と同じ RestartCourse を通るので、どの周も同じ始まり方になる
        void StartCourse();

        //! @brief コースを最初からやり直す。全ての配置物へ MsgCourseRestart を送る
        //! @details 凍結 (StartCourse が取ったシーンの JSON 文書) にある姿へ、受け手が自分で戻る
        void RestartCourse();

        //! 知らせを受けた分だけ流れを進める
        void OnTick() override;

    private:
        //! クリアの流れ: 操作を止める → 暗転 → 全黒の裏でやり直す → 明転 → 操作を戻す
        [[nodiscard]] NS::Coroutine ClearSequence();

        //! プレイヤーへ操作を止めるか戻す知らせを送る
        void SendInputLock(bool locked);

        float m_fadeOutSeconds = 0.0f;
        float m_fadeInSeconds = 0.0f;

        NS::Obj::Scene& m_scene;
        std::unique_ptr<NS::Obj::ScreenFade> m_fade; // 開いて持つ暗転
        NS::CoroutineRunner m_sequences;             // クリアの流れ
        bool m_playerDead = false;                   // 次の段でやり直すか
        bool m_goalReached = false;                  // 次の段でクリアの流れを始めるか
    };
} // namespace NS::Game::Level
