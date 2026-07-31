#include "Game/Level/CoyoteDebugComponent.h"

#include "Runtime/App/Application.h"
#include "Runtime/Graphics/DebugDraw.h"
#include "Runtime/Math/Math.h"
#include "Runtime/Platform/Input.h"
#include "Runtime/Platform/Keyboard.h"

#if !defined(NS_SHIPPING)

#include "Game/Level/BlockObject.h"
#include "Game/Level/LedgeEdges.h"
#include "Runtime/Object/Components/CharacterMovementComponent.h"
#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/Scene/Scene.h"
#include "Runtime/Object/World.h"

namespace NS::Game::Level
{
    namespace
    {
        void DrawCoyoteDebugInfo(NS::Object::World& world, NS::Object::GameObject* player)
        {
            const NS::Math::Color ledgeColor{0.65f, 0.30f, 1.0f, 1.0f};
            const NS::Math::Color limitColor{1.0f, 0.20f, 0.90f, 1.0f};

            NS::Object::CharacterMovementComponent* movement = nullptr;
            if (player != nullptr)
            {
                movement = player->FindComponent<NS::Object::CharacterMovementComponent>();
            }

            float coyoteReach = 0.0f;
            if (movement != nullptr)
            {
                coyoteReach = movement->MaxSpeed() * movement->CoyoteTime();
            }

            // コヨーテ猶予範囲の描画 (縁のAABB天面ベース)
            std::vector<NS::Math::OBB> solidBoxes;
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
                const NS::Math::Vector3 off{edge.outward.x * coyoteReach, 0.0f, edge.outward.z * coyoteReach};
                const NS::Math::Vector3 outerA{edge.a.x + off.x, edge.a.y, edge.a.z + off.z};
                const NS::Math::Vector3 outerB{edge.b.x + off.x, edge.b.y, edge.b.z + off.z};

                NS::Graphics::DebugDraw::Line(edge.a, edge.b, ledgeColor);
                NS::Graphics::DebugDraw::Line(edge.a, outerA, ledgeColor);
                NS::Graphics::DebugDraw::Line(edge.b, outerB, ledgeColor);
                NS::Graphics::DebugDraw::Line(outerA, outerB, limitColor);

                for (int hatch = 1; hatch <= 2; ++hatch)
                {
                    const float t = static_cast<float>(hatch) / 3.0f;
                    const NS::Math::Vector3 ha{edge.a.x + off.x * t, edge.a.y, edge.a.z + off.z * t};
                    const NS::Math::Vector3 hb{edge.b.x + off.x * t, edge.b.y, edge.b.z + off.z * t};
                    NS::Graphics::DebugDraw::Line(ha, hb, ledgeColor);
                }
            }

            if (movement == nullptr)
            {
                return;
            }

            // プレイヤー状態のデバッグ描画
            const NS::Math::Vector3 center = player->Root().Position();

            // 接地判定マーカー (頭上にボックス表示)
            const float headTop = center.y + movement->CapsuleHalfHeight() + movement->CapsuleRadius();
            const NS::Math::Color groundedColor = [movement]() -> NS::Math::Color {
                if (movement->IsGrounded())
                {
                    return NS::Math::Color{0.2f, 1.0f, 0.2f, 1.0f};
                }
                return NS::Math::Color{1.0f, 1.0f, 0.2f, 1.0f};
            }();

            const NS::Math::AABB groundedMarker(NS::Math::Vector3{center.x, headTop + 0.45f, center.z},
                                                NS::Math::Vector3{0.18f, 0.18f, 0.18f});

            NS::Graphics::DebugDraw::AABB(groundedMarker, groundedColor);

            // ジャンプ実行点のマーカー
            const NS::Math::Color coyoteColor{1.0f, 0.15f, 0.15f, 1.0f};
            for (const auto& marker : movement->CoyoteJumpMarkers())
            {
                NS::Graphics::DebugDraw::Line(marker.edge, marker.jump, coyoteColor);
                const NS::Math::Vector3 tickTop{marker.jump.x, marker.jump.y + 0.6f, marker.jump.z};
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
        // プレイヤーに載る component なので、 描く相手は自分の持ち主
        DrawCoyoteDebugInfo(scene->World(), Owner());
    }

} // namespace NS::Game::Level

#endif
