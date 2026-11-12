#include "Game/Level/GameScene.h"

#include "Game/Level/LevelMessages.h"
#include "Game/PlayerActor.h"
#include "NSlib/Audio/SoundScene.h"
#include "NSlib/CoreObject/ActorList.h"
#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/CoreObject/ClassObjs/VirtualCamera.h"
#include "NSlib/CoreObject/IGet/IGetCamera.h"
#include "NSlib/CoreObject/Scene/SceneJson.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/Core/Windows/Clock.h"

#include <optional>
#include <vector>

namespace GL
{
    namespace Level
    {
        GameScene::GameScene() = default;

        GameScene::~GameScene() = default;

        HitScreenDirector* HitScreenOf(const NS::SceneActor* actor) noexcept
        {
            NS_ASSERT(Game, actor != nullptr, "HitScreenOf: アクターが null");
            if (GameScene* game = NS::SceneCast<GameScene>(actor->OwningScene()))
            {
                return &game->HitScreen();
            }
            return nullptr;
        }

        void GameScene::OnStartPlay()
        {
            for (NS::SceneActor* actor : PlacedActors(nullptr))
            {
                (void)SendMsgCourseStart(actor);
            }
            RestartCourse();
            // 前のプレイの曲は OnStopPlay で止めたので必ず始まる。RestartCourse は曲に触らず、やり直しても曲が続く
            const NS::SceneEnvironment& environment = Environment();
            (void)Music().Start(GetSoundScene(), environment.music, environment.musicVolume);
            // 最初の歩でアクターが位置から鳴らす音も、今の自機の位置で聞く
            UpdateSoundListener();
        }

        void GameScene::OnStopPlay()
        {
            // 暗転とアクターの入力の止めは、続く組み直し (Scene::ClearWorld) が暗転を取り消し、アクターごと捨てる
            m_sequences.CancelAll();
            m_playerDead = false;
            m_respawnPending = false;
            m_goalReached = false;
            Music().Stop(GetSoundScene());
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

        void GameScene::StepViewEffects()
        {
            m_hitScreen.OnTick();
            // 仮想カメラが進んだ後で、音の Update の前。シーンが止まっている歩でも、聞く向きがカメラに付いていく
            UpdateSoundListener();
        }

        void GameScene::UpdateSoundListener()
        {
            NS::Audio::SoundScene* sounds = GetSoundScene();
            ::PlayerActor* player = FindPlayer(Actors());
            if (sounds == nullptr || player == nullptr)
            {
                return;
            }
            // 聞く位置は自機にする。カメラの位置にすると、カメラが引いた時に自機の音が小さくなる
            NS::Vector3 forward = sounds->Listener().forward;
            NS::Vector3 up = sounds->Listener().up;
            // 視点が無い時は、向きを前のままにする
            if (const std::optional<NS::CameraPose> view = NS::GetCameraViewPose(this))
            {
                forward = view->target - view->position;
                up = view->up;
            }
            sounds->SetListener(player->TransformObj().Position(), forward, up);
        }

        void GameScene::RestartCourse()
        {
            SendRestart(nullptr);
        }

        void GameScene::RespawnActors()
        {
            SendRestart(FindPlayer(Actors()));
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
