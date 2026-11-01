#pragma once

#include "NSlib/Core/Coroutine.h"
#include "NSlib/CoreObject/Scene/Scene.h"

#include <memory>
#include <vector>

namespace NS
{
    class SceneActor;

    namespace Audio
    {
        class SoundLoop;
    } // namespace Audio
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

            //! @brief シーンの設定の曲をループで鳴らし始める。RestartCourse は曲に触らないので、やり直しても曲が続く
            //! @details シーンが音の装置を持たない時と、曲が空の時は音を出さずに始めた事にする
            //! @return 今の呼び出しで始めたか。StopMusic までの間にもう一度呼ぶと false
            bool StartMusic();

            //! 曲を止める。次の StartMusic は最初から鳴らす
            void StopMusic();

        protected:
            //! 全てのアクターへ MsgCourseStart を送って今の姿を控えさせ、RestartCourse と同じ処理で始める。
            //! 続けてシーンの設定の曲を鳴らし始める
            void OnStartPlay() override;

            //! 組み直す前に、知らせ・クリアの流れ・曲を戻す
            void OnStopPlay() override;

            //! 知らせを受けた分だけ流れを進める
            void StepGame() override;

        private:
            [[nodiscard]] NS::Coroutine ClearSequence();

            void SendInputLock(bool locked);

            //! skip の他のアクター。skip が nullptr なら全員
            [[nodiscard]] std::vector<NS::SceneActor*> PlacedActors(const NS::SceneActor* skip);

            //! PlacedActors の全員へ MsgCourseRestart を送る
            void SendRestart(const NS::SceneActor* skip);

            float m_fadeOutSeconds = 0.0f;
            float m_fadeInSeconds = 0.0f;

            NS::CoroutineRunner m_sequences; // クリアの流れ
            bool m_playerDead = false;       // 次の StepGame でやり直すか
            bool m_respawnPending = false;
            bool m_goalReached = false; // 次の StepGame でクリアの流れを始めるか
            // 曲はアクターの音でなくコースの寿命の物なので、アクターの部品 Sound を通さずに持つ
            std::unique_ptr<NS::Audio::SoundLoop> m_music; // 音を出せない時は nullptr
            bool m_musicStarted = false;                   // StartMusic から StopMusic までの間か
        };
    } // namespace Level
} // namespace GL
