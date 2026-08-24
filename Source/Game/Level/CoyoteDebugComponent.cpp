#include "Game/Level/CoyoteDebugComponent.h"

#include "Runtime/App/Application.h"
#include "Runtime/Core/Math.h"
#include "Runtime/Graphics/DebugDraw.h"
#include "Runtime/Platform/Input.h"
#include "Runtime/Platform/Keyboard.h"

#if !defined(NS_SHIPPING)

#include "Game/Level/BlockObject.h"
#include "Game/Level/LedgeEdges.h"
#include "Game/Player/PlayerComponent.h"
#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/Scene/Scene.h"
#include "Runtime/Object/World.h"

namespace NS::Game::Level
{
    namespace
    {
        void DrawCoyoteDebugInfo(NS::Object::World& world, NS::Object::GameObject* player)
        {
            const NS::Core::Color ledgeColor{0.65f, 0.30f, 1.0f, 1.0f};
            const NS::Core::Color limitColor{1.0f, 0.20f, 0.90f, 1.0f};

            NS::Game::Player::PlayerComponent* movement = nullptr;
            if (player != nullptr)
            {
                movement = player->FindComponent<NS::Game::Player::PlayerComponent>();
            }

            float coyoteReach = 0.0f;
            if (movement != nullptr)
            {
                coyoteReach = movement->MaxSpeed() * movement->CoyoteTime();
            }

            // 固形ブロック天面の縁から、コヨーテ猶予の届く範囲を描く
            std::vector<NS::Core::OBB> solidBoxes;
            solidBoxes.reserve(world.ObjectCount());
            for (NS::Object::GameObject* obj : world)
            {
                if (auto obb = SolidBoxWorldOBB(*obj))
                {
                    solidBoxes.push_back(*obb);
                }
            }

            for (const LedgeEdge& edge : ComputeTopLedgeEdges(solidBoxes))
            {
                const NS::Core::Vector3 off{edge.outward.x * coyoteReach, 0.0f, edge.outward.z * coyoteReach};
                const NS::Core::Vector3 outerA{edge.a.x + off.x, edge.a.y, edge.a.z + off.z};
                const NS::Core::Vector3 outerB{edge.b.x + off.x, edge.b.y, edge.b.z + off.z};

                NS::Graphics::DebugDraw::Line(edge.a, edge.b, ledgeColor);
                NS::Graphics::DebugDraw::Line(edge.a, outerA, ledgeColor);
                NS::Graphics::DebugDraw::Line(edge.b, outerB, ledgeColor);
                NS::Graphics::DebugDraw::Line(outerA, outerB, limitColor);

                for (int hatch = 1; hatch <= 2; ++hatch)
                {
                    const float t = static_cast<float>(hatch) / 3.0f;
                    const NS::Core::Vector3 ha{edge.a.x + off.x * t, edge.a.y, edge.a.z + off.z * t};
                    const NS::Core::Vector3 hb{edge.b.x + off.x * t, edge.b.y, edge.b.z + off.z * t};
                    NS::Graphics::DebugDraw::Line(ha, hb, ledgeColor);
                }
            }

            if (movement == nullptr)
            {
                return;
            }

            // ジャンプ実行点のマーカー
            const NS::Core::Color coyoteColor{1.0f, 0.15f, 0.15f, 1.0f};
            for (const auto& marker : movement->CoyoteJumpMarkers())
            {
                NS::Graphics::DebugDraw::Line(marker.edge, marker.jump, coyoteColor);
                const NS::Core::Vector3 tickTop{marker.jump.x, marker.jump.y + 0.6f, marker.jump.z};
                NS::Graphics::DebugDraw::Line(marker.jump, tickTop, coyoteColor);
            }
        }
    } // namespace

    void CoyoteDebugComponent::OnUpdate()
    {
        auto* app = NS::App::Application::Get();
        if (app == nullptr)
        {
            return;
        }

        // F2 で表示切替
        if (!app->Input().UiWantsKeyboard() && app->Input().Keyboard().IsPressed(NS::Platform::Key::F2))
        {
            m_draw = !m_draw;
        }
        if (!m_draw || Owner() == nullptr)
        {
            return;
        }

        auto* scene = Owner()->OwningScene();
        if (scene == nullptr)
        {
            return;
        }
        // プレイヤーに載る component なので、 描く相手は自分の owner
        DrawCoyoteDebugInfo(scene->World(), Owner());
    }

} // namespace NS::Game::Level

#endif
