#include "Game/Level/Finisher.h"

#include "Game/Level/Goal.h"
#include "Game/Level/Respawner.h"
#include "Game/Level/ScreenFade.h"
#include "Runtime/Platform/Clock.h"
#include "Runtime/Object/Components/PlayerInput.h"
#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/ObjectList.h"
#include "Runtime/Object/Scene/Scene.h"

namespace NS::Game::Level
{
    // 判定より後の帯。接触と同じ LateUpdate で演出を始める
    Finisher::Finisher() noexcept : NS::Obj::Component(NS::Obj::TickPriority::LateUpdate + 10)
    {}

    ScreenFade* Finisher::Fade() noexcept
    {
        return Owner()->FindComponent<ScreenFade>();
    }

    void Finisher::OnUpdate()
    {
        if (m_sequences.IsRunning())
        {
            m_sequences.Tick(NS::Platform::FrameTimer::FixedDelta());
            return;
        }

        auto* scene = Owner()->OwningScene();
        if (scene == nullptr || Fade() == nullptr)
        {
            return;
        }

        bool goalReached = false;
        scene->Objects().ForEachComponent<Goal>([&goalReached](Goal& goal) {
            if (goal.Reached())
                goalReached = true;
        });
        if (goalReached)
        {
            m_sequences.Start(ClearSequence());
        }
    }

    void Finisher::SetPlayerInputActive(bool active) noexcept
    {
        if (auto* input = Owner()->FindComponent<NS::Obj::PlayerInput>())
        {
            input->SetActive(active);
        }
    }

    NS::Core::Coroutine Finisher::ClearSequence()
    {
        // 世界は止めず入力だけ切る。暗転の間も重力とカメラは動いたまま
        SetPlayerInputActive(false);

        Fade()->BeginOut(k_FadeOutSeconds);
        co_await NS::Core::WaitUntil{[this] { return Fade()->IsBlack(); }};

        // 全黒の裏でやり直すので、出現位置への瞬間移動が黒に隠れる。手順は同じ object の respawner が持つ
        if (auto* respawner = Owner()->FindComponent<Respawner>())
        {
            respawner->RestartRun();
        }

        Fade()->BeginIn(k_FadeInSeconds);
        co_await NS::Core::WaitUntil{[this] { return !Fade()->IsFading(); }};

        SetPlayerInputActive(true);
    }
} // namespace NS::Game::Level
