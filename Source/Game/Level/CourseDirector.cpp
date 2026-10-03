#include "Game/Level/CourseDirector.h"

#include "Game/Level/LevelMessages.h"
#include "Game/Player.h"
#include "Runtime/Object/Actor.h"
#include "Runtime/Object/ObjectList.h"
#include "Runtime/Object/Scene/Scene.h"
#include "Runtime/Object/ScreenFade.h"
#include "Runtime/Platform/Clock.h"

#include <vector>

namespace NS::Game::Level
{
    CourseDirector::CourseDirector(NS::Obj::Scene& scene)
        : m_scene(scene), m_fade(std::make_unique<NS::Obj::ScreenFade>())
    {
        m_fade->Open(scene);
        m_scene.Objects().AddTicker(this, NS::Obj::UpdatePhase::Course);
    }

    CourseDirector::~CourseDirector() noexcept
    {
        m_scene.Objects().RemoveTicker(this);
        // 流れを途中のまま捨てる。暗転は閉じて画面の一覧から外す
        m_sequences.CancelAll();
        m_fade->Close();
    }

    void CourseDirector::OnTick()
    {
        if (m_sequences.IsRunning())
        {
            m_sequences.Tick(NS::Platform::FrameTimer::FixedDelta());
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
            RestartCourse();
        }
    }

    void CourseDirector::StartCourse()
    {
        // 凍結がやり直しの戻り先なので、取ってからやり直す
        (void)m_scene.BeginPlayBaseline();
        RestartCourse();
    }

    void CourseDirector::RestartCourse()
    {
        // 送る先は控えた並びで回す。受け手が戻る間に配置物の並びが変わっても崩れない
        // 一時オブジェクト (破片・跡) は凍結に無いので送らない
        const nlohmann::json& baseline = m_scene.PlayBaseline();
        std::vector<NS::Obj::Actor*> receivers;
        receivers.reserve(m_scene.Objects().ObjectCount());
        for (NS::Obj::Actor* actor : m_scene.Objects())
        {
            if (!actor->IsTransient())
            {
                receivers.push_back(actor);
            }
        }
        for (NS::Obj::Actor* actor : receivers)
        {
            (void)SendMsgCourseRestart(*actor, baseline);
        }
    }

    void CourseDirector::SendInputLock(bool locked)
    {
        if (::Player* player = FindPlayer(m_scene.Objects()))
        {
            (void)SendMsgInputLock(*player, locked);
        }
    }

    NS::Core::Coroutine CourseDirector::ClearSequence()
    {
        // 世界は止めず操作だけ止める。暗転の間も重力とカメラは動いたまま
        SendInputLock(true);

        m_fade->BeginOut(k_FadeOutSeconds);
        co_await NS::Core::WaitUntil{[this] { return m_fade->IsBlack(); }};

        // 全黒の裏でやり直すので、出現位置への瞬間移動が黒に隠れる
        RestartCourse();

        m_fade->BeginIn(k_FadeInSeconds);
        co_await NS::Core::WaitUntil{[this] { return !m_fade->IsFading(); }};

        SendInputLock(false);
    }
} // namespace NS::Game::Level
