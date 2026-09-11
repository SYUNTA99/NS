#include "Game/Level/FinisherComponent.h"

#include "Game/Level/GoalComponent.h"
#include "Game/Level/RespawnerComponent.h"
#include "Game/Level/ScreenFadeComponent.h"
#include "Runtime/Core/Clock.h"
#include "Runtime/Object/Components/PlayerInputComponent.h"
#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/Scene/Scene.h"
#include "Runtime/Object/World.h"

namespace NS::Game::Level
{
    // 判定より後の帯。接触と同じ LateUpdate で演出を始める
    FinisherComponent::FinisherComponent() noexcept : NS::Object::Component(NS::Object::TickPriority::LateUpdate + 10)
    {}

    ScreenFadeComponent* FinisherComponent::Fade() noexcept
    {
        return Owner()->FindComponent<ScreenFadeComponent>();
    }

    void FinisherComponent::OnUpdate()
    {
        if (m_sequences.IsRunning())
        {
            m_sequences.Tick(NS::Core::FrameTimer::FixedDelta());
            return;
        }

        auto* scene = Owner()->OwningScene();
        if (scene == nullptr || Fade() == nullptr)
            return;

        bool goalReached = false;
        scene->World().ForEachComponent<GoalComponent>([&goalReached](GoalComponent& goal) {
            if (goal.Reached())
                goalReached = true;
        });
        if (goalReached)
            m_sequences.Start(ClearSequence());
    }

    void FinisherComponent::SetPlayerInputActive(bool active) noexcept
    {
        if (auto* input = Owner()->FindComponent<NS::Object::PlayerInputComponent>())
            input->SetActive(active);
    }

    NS::Core::Coroutine FinisherComponent::ClearSequence()
    {
        // 世界は止めず入力だけ切る。暗転の間も重力とカメラは動いたまま
        SetPlayerInputActive(false);

        Fade()->BeginOut(k_FadeOutSeconds);
        co_await NS::Core::WaitUntil{[this] { return Fade()->IsBlack(); }};

        // 全黒の裏でやり直すので、出現位置への瞬間移動が黒に隠れる。手順は同じ object の respawner が持つ
        if (auto* respawner = Owner()->FindComponent<RespawnerComponent>())
            respawner->RestartRun();
        Fade()->BeginIn(k_FadeInSeconds);
        co_await NS::Core::WaitUntil{[this] { return !Fade()->IsFading(); }};

        SetPlayerInputActive(true);
    }
} // namespace NS::Game::Level
