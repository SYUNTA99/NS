#include "Game/Level/GameScene.h"

#include "Game/Level/LevelMessages.h"
#include "Game/PlayerActor.h"
#include "NSlib/Audio/SoundLoop.h"
#include "NSlib/CoreObject/ActorList.h"
#include "NSlib/CoreObject/Asset/AssetManager.h"
#include "NSlib/CoreObject/Asset/SoundClip.h"
#include "NSlib/CoreObject/Reflection/SoundName.h"
#include "NSlib/CoreObject/Scene/SceneJson.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/Core/Windows/Clock.h"

#include <vector>

namespace GL
{
    namespace Level
    {
        GameScene::GameScene() = default;

        GameScene::~GameScene() = default;

        void GameScene::OnStartPlay()
        {
            for (NS::SceneActor* actor : PlacedActors(nullptr))
            {
                (void)SendMsgCourseStart(actor);
            }
            RestartCourse();
            (void)StartMusic(); // 前のプレイの曲は Scene が OnStopPlay で止めたので必ず始まる
        }

        void GameScene::OnStopPlay()
        {
            // 暗転とアクターの入力の止めは、続く組み直し (Scene::ClearWorld) が暗転を取り消し、アクターごと捨てる
            m_sequences.CancelAll();
            m_playerDead = false;
            m_respawnPending = false;
            m_goalReached = false;
            StopMusic();
        }

        bool GameScene::NotifyGoal(float fadeOutSeconds, float fadeInSeconds) noexcept
        {
            if (m_sequences.IsRunning() || m_goalReached)
            {
                return false;
            }
            m_fadeOutSeconds = fadeOutSeconds;
            m_fadeInSeconds = fadeInSeconds;
            m_goalReached = true;
            return true;
        }

        void GameScene::StepGame()
        {
            // 音の機器を繋ぎ直すと曲が止まるので、鳴らし直す。鳴っている間は何もしない
            if (m_music)
            {
                m_music->Play();
            }
            if (m_sequences.IsRunning())
            {
                m_sequences.Tick(NS::OS::FrameTimer::FixedDelta());
            }
            else if (m_goalReached)
            {
                m_sequences.Start(ClearSequence());
            }
            // ゴールは触れ始めに 1 回だけ届く。クリアの流れの最中に届いた物は捨てる
            m_goalReached = false;

            if (m_playerDead)
            {
                m_playerDead = false;
                m_respawnPending = false;
                RestartCourse();
            }
            if (m_respawnPending)
            {
                m_respawnPending = false;
                RespawnActors();
            }
        }

        void GameScene::RestartCourse()
        {
            SendRestart(nullptr);
        }

        void GameScene::RespawnActors()
        {
            SendRestart(FindPlayer(Actors()));
        }

        bool GameScene::StartMusic()
        {
            if (m_musicStarted)
            {
                return false;
            }
            m_musicStarted = true;
            const NS::SceneEnvironment& environment = Environment();
            // 当たりの下見のシーンは装置を受け取らないので、曲を読まない
            if (GetAudioDevice() == nullptr || environment.music.name.empty())
            {
                return true;
            }
            const NS::SoundClip* sound = NS::AssetManager::Get().LoadSoundClip(NS::SoundPathOf(environment.music.name));
            if (sound == nullptr || !sound->IsValid())
            {
                return true;
            }
            // 曲の音は AssetManager が Clear まで持ち、シーンはそれより先に壊れるので、ループが音より長く生きない
            m_music = std::make_unique<NS::Audio::SoundLoop>(*sound->Clip());
            m_music->SetVolume(environment.musicVolume);
            m_music->Play();
            return true;
        }

        void GameScene::StopMusic()
        {
            m_musicStarted = false;
            m_music.reset();
        }

        std::vector<NS::SceneActor*> GameScene::PlacedActors(const NS::SceneActor* skip)
        {
            // 送る先は控えた並びで回す。受け手が戻る間にアクターの並びが変わっても崩れない
            std::vector<NS::SceneActor*> receivers;
            receivers.reserve(Actors().ActorCount());
            for (NS::SceneActor* actor : Actors())
            {
                if (actor != skip)
                {
                    receivers.push_back(actor);
                }
            }
            return receivers;
        }

        void GameScene::SendRestart(const NS::SceneActor* skip)
        {
            // 止め始めたのが出し直すアクターなら、止めを残すと出し直したアクターが止まったまま待つ。
            // 出し直さないアクター (リスポーンの自機) が始めた止めと、同じ歩に頼んだ止めは、そのアクターが続けるので残す。
            // skip が nullptr (全員を出し直す) の時は、どちらの関数も false を返す
            if (!IsStopSceneMover(skip) && !IsStopSceneRequester(skip))
            {
                CancelStopScene();
            }
            for (NS::SceneActor* actor : PlacedActors(skip))
            {
                (void)SendMsgCourseRestart(actor);
            }
        }

        void GameScene::SendInputLock(bool locked)
        {
            if (::PlayerActor* player = FindPlayer(Actors()))
            {
                (void)SendMsgInputLock(player, locked);
            }
        }

        NS::Coroutine GameScene::ClearSequence()
        {
            // 世界は止めず操作だけ止める。暗転の間も重力とカメラは動いたまま
            SendInputLock(true);

            Fade().BeginOut(m_fadeOutSeconds);
            co_await NS::WaitUntil{[this] { return Fade().IsBlack(); }};

            // 全黒の裏でやり直すので、出現位置への瞬間移動が黒に隠れる
            RestartCourse();

            Fade().BeginIn(m_fadeInSeconds);
            co_await NS::WaitUntil{[this] { return !Fade().IsFading(); }};

            SendInputLock(false);
        }
    } // namespace Level
} // namespace GL
