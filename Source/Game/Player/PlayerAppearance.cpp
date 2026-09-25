#include "Game/Player/PlayerAppearance.h"

#include "Game/Player/PlayerComponent.h"
#include "Runtime/Core/Logger.h"
#include "Runtime/Graphics/StaticMesh.h"
#include "Runtime/Object/AssetManager.h"
#include "Runtime/Object/Components/MeshRenderer.h"
#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"

namespace
{
    // 参照が書いてあればそのファイルの mesh、空か引き当てられなければ仮の形を返す
    [[nodiscard]] NS::Gfx::Mesh* ResolveLook(NS::Obj::AssetManager& assets,
                                             const std::string& ref,
                                             NS::Gfx::Mesh* placeholder)
    {
        if (ref.empty())
        {
            return placeholder;
        }
        if (NS::Gfx::Mesh* mesh = NS::Obj::ResolveMeshFromRef(assets, ref))
        {
            return mesh;
        }
        NS_LOG_WARN(Game, "PlayerAppearance: 見た目の参照を引き当てられない。仮の形で描く: {}", ref);
        return placeholder;
    }
} // namespace

namespace NS::Game::Player
{
    // 配置物を組む経路では参照の引き当てが並び順に回る。MeshRenderer (Update) が自分の参照から mesh
    // を差した後に差し直す
    PlayerAppearance::PlayerAppearance() noexcept : NS::Obj::Component(NS::Obj::TickPriority::Update + 50) {}

    void PlayerAppearance::Curl() noexcept
    {
        if (m_curled)
        {
            return;
        }
        m_curled = true;
        ShowCurrentLook();
    }

    void PlayerAppearance::Uncurl() noexcept
    {
        if (!m_curled)
        {
            return;
        }
        m_curled = false;
        ShowCurrentLook();
    }

    void PlayerAppearance::OnStart()
    {
        if (Owner() != nullptr)
        {
            m_player = Owner()->FindComponent<PlayerComponent>();
        }
    }

    void PlayerAppearance::OnUpdate()
    {
        if (m_player == nullptr)
        {
            return;
        }
        if (m_player->IsCurled())
        {
            Curl();
        }
        else
        {
            Uncurl();
        }
    }

    void PlayerAppearance::ResolveAssets(NS::Obj::AssetManager& assets)
    {
        NS::Gfx::Mesh* standingPlaceholder = nullptr;
        NS::Gfx::Mesh* ballPlaceholder = nullptr;
        const PlayerComponent* player = nullptr;
        if (Owner() != nullptr)
        {
            player = Owner()->FindComponent<PlayerComponent>();
        }
        if (player != nullptr)
        {
            // 寸法の正は移動と当たりの裁定が読むカプセルの欄。玉は円柱の長さ 0 のカプセルで、直径が当たりと揃う
            // 立ち姿は丸まりに依らない立ち姿の半長で作る。今の当たりの半長は玉の間 0 で、丸まっている間に引き直すと
            // 立ち姿まで玉になる
            standingPlaceholder = assets.GetOrMakeCapsuleMesh(player->CapsuleRadius(), player->StandingHalfHeight());
            ballPlaceholder = assets.GetOrMakeCapsuleMesh(player->CapsuleRadius(), 0.0f);
        }
        else
        {
            NS_LOG_WARN(Game, "PlayerAppearance: 同居する PlayerComponent が無く、仮の形の寸法を決められない");
        }

        m_standingMesh = ResolveLook(assets, m_standingMeshRef, standingPlaceholder);
        m_ballMesh = ResolveLook(assets, m_ballMeshRef, ballPlaceholder);
        ShowCurrentLook();
    }

    void PlayerAppearance::ShowCurrentLook() noexcept
    {
        if (Owner() == nullptr)
        {
            return;
        }
        NS::Obj::MeshRenderer* renderer = Owner()->FindComponent<NS::Obj::MeshRenderer>();
        if (renderer == nullptr)
        {
            return;
        }

        NS::Gfx::Mesh* mesh = m_standingMesh;
        if (m_curled)
        {
            mesh = m_ballMesh;
        }
        // 引き当て前は MeshRenderer が自分の参照から差した mesh を残す
        if (mesh == nullptr)
        {
            return;
        }
        renderer->SetMesh(mesh);
    }

    NS_CLASS(PlayerAppearance)
} // namespace NS::Game::Player
