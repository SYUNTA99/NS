#include "Game/Player/PlayerAppearance.h"

#include "Game/Level/CollisionInput.h"
#include "Game/Player/PlayerComponent.h"
#include "Runtime/Core/Logger.h"
#include "Runtime/Graphics/StaticMesh.h"
#include "Runtime/Object/AssetManager.h"
#include "Runtime/Object/Components/MeshRenderer.h"
#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"
#include "Runtime/Platform/Clock.h"
#include <cmath>

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

    // 上と向きの外積を水平の回転軸として axis へ書く。正の角度で上面がその向きへ倒れる前転になる
    // 長さの無い向きと、水平成分に非数・無限大を含む向きは採らず、axis を書き換えない
    // TryNormalizeHorizontal は長さの 2 乗が非数だと下限との比較が偽になって通すので、先に有限かを見る
    void SetRollAxisToward(const NS::Core::Vector3& direction, NS::Core::Vector3& axis) noexcept
    {
        const float lengthSq = direction.x * direction.x + direction.z * direction.z;
        if (!std::isfinite(lengthSq))
        {
            return;
        }
        NS::Core::Vector3 forward{};
        if (!NS::Core::TryNormalizeHorizontal(direction, forward))
        {
            return;
        }
        axis = NS::Core::Vector3{forward.z, 0.0f, -forward.x};
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
            m_input = Owner()->FindComponent<NS::Game::Level::CollisionInput>();
        }
    }

    void PlayerAppearance::AdvanceSpin() noexcept
    {
        m_spinDegreesThisFrame = 0.0f;
        NS::Obj::MeshRenderer* renderer = nullptr;
        if (Owner() != nullptr)
        {
            renderer = Owner()->FindComponent<NS::Obj::MeshRenderer>();
        }

        if (!m_curled)
        {
            // 立ち姿は回さず、玉の回転と軸と速さも捨てる。丸まり直した玉へ前の玉の値を持ち越さない
            m_spin = NS::Core::Quaternion::Identity;
            m_spinAxis = k_FirstSpinAxis;
            m_spinSpeed = 0.0f;
            // 前のフレームの値も揃える。今の値だけを戻すと、持ち替えたフレームの立ち姿が、玉の姿勢から戻る途中の
            // 傾きで描かれる
            if (renderer != nullptr)
            {
                renderer->SnapLocalRotation(m_spin);
            }
            return;
        }
        if (!m_player->IsActive())
        {
            // 当たりの止めで移動が止まっている間は、絵も止める
            return;
        }

        if (m_player->IsBodySlamming())
        {
            SetRollAxisToward(m_player->BodySlamVelocity(), m_spinAxis);
            m_spinSpeed = m_bodySlamSpinSpeed;
        }
        else if (m_input != nullptr && m_input->Judge().IsHeld())
        {
            // 狙いが決まらないフレームは前の軸で回し続ける
            SetRollAxisToward(m_player->AimDirection(), m_spinAxis);
            m_spinSpeed =
                m_emptyChargeSpinSpeed + (m_fullChargeSpinSpeed - m_emptyChargeSpinSpeed) * m_input->Judge().Charge01();
        }
        // 放した後の空中と飛ばされている間は、直前のフレームの軸と速さのまま回る

        const float degrees = m_spinSpeed * NS::Platform::FrameTimer::FixedDelta();
        const float radians = NS::Core::ToRadians(NS::Core::Degrees{degrees}).value;
        const NS::Core::Quaternion turn = NS::Core::Quaternion::CreateFromAxisAngle(m_spinAxis, radians);
        // 既に回った姿勢の後に今の軸の回転を足す。SimpleMath の q1 * q2 は q1 の後に q2 で、軸は根の空間で固定
        m_spin = m_spin * turn;
        m_spin.Normalize();
        m_spinDegreesThisFrame = degrees;

        if (renderer != nullptr)
        {
            renderer->SetLocalRotation(m_spin);
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
        AdvanceSpin();
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
