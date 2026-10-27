#include "Game/Level/CourseDirector.h"

#include "Game/Level/LevelMessages.h"
#include "Game/PlayerActor.h"
#include "NSlib/Audio/SoundLoop.h"
#include "NSlib/CoreObject/ActorList.h"
#include "NSlib/CoreObject/Asset/AssetManager.h"
#include "NSlib/CoreObject/Asset/SoundClip.h"
#include "NSlib/CoreObject/Reflection/SoundName.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/Scene/SceneJson.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/Core/Windows/Clock.h"

#include <vector>

namespace GL::Level
{
        CourseDirector::CourseDirector(NS::Scene& scene) : m_scene(scene)
        {
            m_scene.AddSystemTick(*this, NS::UpdatePhase::Course);
        }

        CourseDirector::~CourseDirector() noexcept
        {
            // 進行中のクリア演出も取り消す
            if (m_sequences.IsRunning())
            {
                m_scene.Fade().Cancel();
            }
            m_sequences.CancelAll();
        }

        void CourseDirector::OnTick()
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

        void CourseDirector::StartCourse()
        {
            for (NS::SceneActor* actor : PlacedActors(nullptr))
            {
                (void)SendMsgCourseStart(actor);
            }
            RestartCourse();
        }

        void CourseDirector::RestartCourse()
        {
            SendRestart(nullptr);
        }

        void CourseDirector::RespawnActors()
        {
            SendRestart(FindPlayer(m_scene.Actors()));
        }

        bool CourseDirector::StartMusic()
        {
            if (m_musicStarted)
            {
                return false;
            }
            m_musicStarted = true;
            const NS::SceneEnvironment& environment = m_scene.Environment();
            // 当たりの下見のシーンは装置を受け取らないので、曲を読まない
            if (m_scene.GetAudioDevice() == nullptr || environment.music.name.empty())
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

        void CourseDirector::StopMusic()
        {
            m_musicStarted = false;
            m_music.reset();
        }

        std::vector<NS::SceneActor*> CourseDirector::PlacedActors(const NS::SceneActor* skip) const
        {
            // 送る先は控えた並びで回す。受け手が戻る間にアクターの並びが変わっても崩れない
            std::vector<NS::SceneActor*> receivers;
            receivers.reserve(m_scene.Actors().ActorCount());
            for (NS::SceneActor* actor : m_scene.Actors())
            {
                if (actor != skip)
                {
                    receivers.push_back(actor);
                }
            }
            return receivers;
        }

        void CourseDirector::SendRestart(const NS::SceneActor* skip)
        {
            // 止め始めたのが出し直すアクターなら、止めを残すと出し直したアクターが止まったまま待つ。
            // 出し直さないアクター (リスポーンの自機) が始めた止めは、そのアクターが続けているので残す
            if (skip == nullptr || !m_scene.IsOthersHolder(skip))
            {
                m_scene.ReleaseOthersHold();
            }
            for (NS::SceneActor* actor : PlacedActors(skip))
            {
                (void)SendMsgCourseRestart(actor);
            }
        }

        void CourseDirector::SendInputLock(bool locked)
        {
            if (::PlayerActor* player = FindPlayer(m_scene.Actors()))
            {
                (void)SendMsgInputLock(player, locked);
            }
        }

        NS::Coroutine CourseDirector::ClearSequence()
        {
            // 世界は止めず操作だけ止める。暗転の間も重力とカメラは動いたまま
            SendInputLock(true);

            m_scene.Fade().BeginOut(m_fadeOutSeconds);
            co_await NS::WaitUntil{[this] { return m_scene.Fade().IsBlack(); }};

            // 全黒の裏でやり直すので、出現位置への瞬間移動が黒に隠れる
            RestartCourse();

            m_scene.Fade().BeginIn(m_fadeInSeconds);
            co_await NS::WaitUntil{[this] { return !m_scene.Fade().IsFading(); }};

            SendInputLock(false);
        }
} // namespace GL::Level
