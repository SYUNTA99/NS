#include "Game/Level/CourseDirector.h"

#include "Game/Level/LevelMessages.h"
#include "Game/PlayerActor.h"
#include "NSlib/CoreObject/ActorList.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/Windows/Clock.h"

#include <vector>

namespace GL
{
    namespace Level
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
            if (m_sequences.IsRunning())
            {
                m_sequences.Tick(NS::OS::FrameTimer::FixedDelta());
            }
            else if (m_goalReached)
            {
                m_sequences.Start(ClearSequence());
            }
            // ゴールに触れている間は毎フレーム知らせが来る。流れの最中の知らせは捨てる
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
                RespawnObjects();
            }
        }

        void CourseDirector::StartCourse()
        {
            // 凍結はエディタがプレイの終わりに編集の姿へ戻すために読む。やり直しの戻り先は各自の控え
            (void)m_scene.BeginPlayBaseline();
            for (NS::SceneActor* actor : PlacedActors(nullptr))
            {
                (void)SendMsgCourseStart(*actor);
            }
            RestartCourse();
        }

        void CourseDirector::RestartCourse()
        {
            SendRestart(nullptr);
        }

        void CourseDirector::RespawnObjects()
        {
            SendRestart(FindPlayer(m_scene.Objects()));
        }

        std::vector<NS::SceneActor*> CourseDirector::PlacedActors(const NS::SceneActor* skip) const
        {
            // 送る先は控えた並びで回す。受け手が戻る間に配置物の並びが変わっても崩れない
            // 一時オブジェクト (跡) は開始の時の姿を持たないので送らない
            std::vector<NS::SceneActor*> receivers;
            receivers.reserve(m_scene.Objects().ObjectCount());
            for (NS::SceneActor* actor : m_scene.Objects())
            {
                if (!actor->IsTransient() && actor != skip)
                {
                    receivers.push_back(actor);
                }
            }
            return receivers;
        }

        void CourseDirector::SendRestart(const NS::SceneActor* skip)
        {
            for (NS::SceneActor* actor : PlacedActors(skip))
            {
                (void)SendMsgCourseRestart(*actor);
            }
        }

        void CourseDirector::SendInputLock(bool locked)
        {
            if (::PlayerActor* player = FindPlayer(m_scene.Objects()))
            {
                (void)SendMsgInputLock(*player, locked);
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
    } // namespace Level
} // namespace GL
