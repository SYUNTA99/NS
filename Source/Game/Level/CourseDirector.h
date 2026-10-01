#pragma once

#include "Runtime/Core/Coroutine.h"
#include "Runtime/Object/Component.h"
#include "Runtime/Object/ITickable.h"
#include "Runtime/Object/Scene/SceneObjHolder.h"

#include <memory>

namespace NS::Obj
{
    class Scene;
}

namespace NS::Game::Level
{
    class ScreenFade;

    //! @brief コースの進行役。シーンに 1 つの物で、プレイヤーの死とゴールに応じてコースの流れを進める
    //! @details プレイヤーは知らせを受けて進行役へ伝えるだけで、流れは知らない
    //! 死んだら同じ段でコースを最初からやり直す。ゴールに着いたらクリアの流れを流す: 操作を止める → 暗転 →
    //! 全黒の裏でやり直す → 明転 → 操作を戻す。世界は止めない
    //! やり直しは全ての配置物へ MsgCourseRestart を送り、戻り方は受け手が決める
    //! 暗転 (ScreenFade) は進行役が開いて持つ UIActor。進行役はシーンの組み直しで捨てられ、最初の状態から作り直される
    class CourseDirector final : public NS::Obj::ISceneObj, public NS::Obj::ITickable
    {
    public:
        explicit CourseDirector(NS::Obj::Scene& scene);
        ~CourseDirector() noexcept override;

        //! プレイヤーの命が尽きた知らせ。次の進行役の段でコースを最初からやり直す
        void NotifyPlayerDead() noexcept { m_playerDead = true; }

        //! ゴールに着いた知らせ。次の進行役の段でクリアの流れを始める。流れの最中は無視する
        void NotifyGoal() noexcept { m_goalReached = true; }

        //! クリアの流れの最中か
        [[nodiscard]] bool IsClearing() const noexcept { return m_sequences.IsRunning(); }

        //! 暗転。テストと演出の確認が読む
        [[nodiscard]] const ScreenFade& Fade() const noexcept { return *m_fade; }

        //! @brief コースを最初からやり直す。全ての配置物へ MsgCourseRestart を送る
        //! @details 凍結 (プレイ開始時のシーンの JSON 文書) にある姿へ、受け手が自分で戻る
        void RestartCourse();

        //! 知らせを受けた分だけ流れを進める
        void OnTick() override;

    private:
        //! クリアの流れ: 操作を止める → 暗転 → 全黒の裏でやり直す → 明転 → 操作を戻す
        [[nodiscard]] NS::Core::Coroutine ClearSequence();

        //! プレイヤーへ操作を止めるか戻す知らせを送る
        void SendInputLock(bool locked);

        // ゴールの達成感を一拍味わわせてから、もう一周へ送り出すテンポ。短いと唐突で長いと待たされる
        static constexpr float k_FadeOutSeconds = 0.4f; // ゴールから全黒になるまでの秒
        static constexpr float k_FadeInSeconds = 0.4f;  // やり直した後に明けるまでの秒

        NS::Obj::Scene& m_scene;
        std::unique_ptr<ScreenFade> m_fade;    // 開いて持つ暗転
        NS::Core::CoroutineRunner m_sequences; // クリアの流れ
        bool m_playerDead = false;             // 次の段でやり直すか
        bool m_goalReached = false;            // 次の段でクリアの流れを始めるか
    };
} // namespace NS::Game::Level
