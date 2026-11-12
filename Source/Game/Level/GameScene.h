#pragma once

#include "Game/Level/HitScreenDirector.h"
#include "NSlib/Core/Coroutine.h"
#include "NSlib/CoreObject/Scene/Scene.h"

#include <memory>
#include <vector>

namespace NS
{
    class SceneActor;
} // namespace NS

namespace GL
{
    namespace Level
    {
        //! コースを遊ぶシーン。死とゴールに応じてコースをやり直す
        //! クリア時は操作を止め、世界を動かしたまま暗転・再開・明転する。コースの中身は .scene のデータで、
        //! どのコースもこのクラスで立てる
        class GameScene final : public NS::Scene
        {
        public:
            GameScene();
            ~GameScene() override;

            GameScene(const GameScene&) = delete;
            GameScene& operator=(const GameScene&) = delete;
            GameScene(GameScene&&) = delete;
            GameScene& operator=(GameScene&&) = delete;

            //! NS::SceneCast が GameScene を見分けるタグ
            [[nodiscard]] static const void* StaticKind() noexcept
            {
                static const char tag = 0;
                return &tag;
            }
            [[nodiscard]] const void* Kind() const noexcept override { return StaticKind(); }

            //! 次の StepGame でコースをやり直す
            void NotifyPlayerDead() noexcept { m_playerDead = true; }

            //! 次の StepGame で、自機以外のアクターを開始時の姿へ戻す
            void RequestRespawnActors() noexcept { m_respawnPending = true; }

            //! @brief 次の StepGame でクリア演出を始める。演出中と、もう受けた後は無視する
            //! @return 今の呼び出しで受けたか。ゴールに触れている間は毎フレーム届くので、true は触れ始めの 1 回だけ
            [[nodiscard]] bool NotifyGoal(float fadeOutSeconds, float fadeInSeconds) noexcept;

            [[nodiscard]] bool IsClearing() const noexcept { return m_sequences.IsRunning(); }

            //! コースを最初からやり直す。全てのアクターへ MsgCourseRestart を送る
            //! 受け手は StartPlay の時に控えた姿へ自分で戻る
            void RestartCourse();

            //! 自機の他のアクターへ MsgCourseRestart を送り、開始の時の姿へ戻す。自機の位置と状態は変えない
            void RespawnActors();

            //! 当たりの画面の演出 (歪みの輪・床の波)。当たりの反応の項目が頼む
            [[nodiscard]] HitScreenDirector& HitScreen() noexcept { return m_hitScreen; }
            [[nodiscard]] const HitScreenDirector& HitScreen() const noexcept { return m_hitScreen; }

        protected:
            //! 全てのアクターへ MsgCourseStart を送って今の姿を控えさせ、RestartCourse と同じ処理で始める。
            //! 続けてシーンの設定の曲を鳴らし始める
            void OnStartPlay() override;

            //! 組み直す前に、知らせ・クリアの流れ・曲を戻す
            void OnStopPlay() override;

            //! 知らせを受けた分だけ流れを進める
            void StepGame() override;

            //! 当たりの画面の演出を 1 フレーム進め、音を聞く点を今の自機とカメラに合わせる。シーンが止まっている歩でも進む
            void StepViewEffects() override;

            //! 当たりの画面の演出を全部消す。前の世界のアクターが頼んだ輪と波を次の世界へ持ち越さない
            void ClearViewEffects() override { m_hitScreen.Clear(); }

        private:
            [[nodiscard]] NS::Coroutine ClearSequence();

            void SendInputLock(bool locked);

            //! skip の他のアクター。skip が nullptr なら全員
            [[nodiscard]] std::vector<NS::SceneActor*> PlacedActors(const NS::SceneActor* skip);

            //! PlacedActors の全員へ MsgCourseRestart を送る
            void SendRestart(const NS::SceneActor* skip);

            //! 位置から鳴らす音を聞く点を、位置は自機、向きはカメラの視点にする。OnStartPlay と StepViewEffects が呼ぶ。
            //! 自機が居ない歩と、音の装置を受け取っていないシーンでは何もしない
            void UpdateSoundListener();

            float m_fadeOutSeconds = 0.0f;
            float m_fadeInSeconds = 0.0f;

            NS::CoroutineRunner m_sequences; // クリアの流れ
            bool m_playerDead = false;       // 次の StepGame でやり直すか
            bool m_respawnPending = false;
            bool m_goalReached = false; // 次の StepGame でクリアの流れを始めるか
            HitScreenDirector m_hitScreen{*GetSceneRenderer()};
        };

        //! actor が入っている GameScene の当たりの画面の演出。actor が GameScene に入っていなければ nullptr
        [[nodiscard]] HitScreenDirector* HitScreenOf(const NS::SceneActor* actor) noexcept;
    } // namespace Level
} // namespace GL
