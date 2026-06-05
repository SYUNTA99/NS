#include "Framework/Scene/HazardComponent.h"

#include "Framework/Core/LogCategories.h"
#include "Framework/Core/Logger.h"
#include "Game/Level/PlayState.h"

namespace NS::Scene
{
    HazardComponent::HazardComponent() noexcept {}

    void HazardComponent::OnPlayerOverlap(NS::Game::Level::PlayState& playState) noexcept
    {
        if (playState.playerHealth <= 0)
            return;

        --playState.playerHealth;
        if (playState.playerHealth <= 0)
        {
            playState.playerHealth = 0;
            playState.deathTriggered = true;
            NS_LOG_INFO(::NS::Core::LogCat::Game,
                        "ハザード接触で死亡 (placeholder、 将来 HUD / 回復 / 演出に置換予定)");
        }
    }
} // namespace NS::Scene
