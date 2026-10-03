#include "Game/Player/PlayerParams.h"
#include "Game/Player.h"
#include "Game/Player/PlayerAppearance.h"

#include "Runtime/Object/Reflection/TypeRegistry.h"

namespace NS::Game::Player
{
    PlayerParams::PlayerParams() noexcept
    {
        m_chargeFactorCurve.count = 2;
        m_chargeFactorCurve.keys[0] = NS::Obj::Curve::Key{0.0f, 1.0f};
        m_chargeFactorCurve.keys[1] = NS::Obj::Curve::Key{1.0f, 2.0f};
    }

    void PlayerParams::ResolveAssets(NS::Obj::AssetManager& assets)
    {
        if (::Player* ownerPlayer = NS::Obj::Cast<::Player>(Owner()))
        {
            ownerPlayer->Appearance().ResolveAssets(assets);
        }
    }

    NS_CLASS(PlayerParams)
} // namespace NS::Game::Player
