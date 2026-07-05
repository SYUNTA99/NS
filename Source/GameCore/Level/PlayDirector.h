#pragma once

/// @file PlayDirector.h
/// @brief プレイ進行役の GameObject。 進行の振る舞いは配下 Component が担う

#include "Framework/Scene/GameObject.h"
#include "GameCore/Level/ClearFadeComponent.h"
#include "GameCore/Level/PlayFlowComponent.h"

namespace NS::GameCore::Level
{
    /// プレイ進行を束ねる進行役。 scene が 1 体所有し、 編集中は Component を寝かせて止める
    class PlayDirector : public NS::Scene::GameObject
    {
    public:
        PlayDirector();
        ~PlayDirector() override = default;

        PlayDirector(const PlayDirector&) = delete;
        PlayDirector& operator=(const PlayDirector&) = delete;
        PlayDirector(PlayDirector&&) = delete;
        PlayDirector& operator=(PlayDirector&&) = delete;

        [[nodiscard]] PlayFlowComponent& Flow() noexcept { return *m_flow; }
        [[nodiscard]] ClearFadeComponent& Fade() noexcept { return *m_fade; }

    private:
        PlayFlowComponent* m_flow = nullptr;
        ClearFadeComponent* m_fade = nullptr;
    };

} // namespace NS::GameCore::Level
