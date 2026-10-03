#include "Game/Player/PlayerAppearance.h"

#include "Game/Level/CollisionInput.h"
#include "Game/Player.h"
#include "Game/Player/PlayerJudges.h"
#include "Game/Player/PlayerParams.h"
#include "Runtime/Core/Logger.h"
#include "Runtime/Graphics/StaticMesh.h"
#include "Runtime/Object/Actor.h"
#include "Runtime/Object/AssetManager.h"
#include "Runtime/Object/Components/Body.h"
#include "Runtime/Object/Components/Model.h"
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
    // 配置物を組む経路では参照の引き当てが Player::ForEachPart の並びに回る。Model が自分の参照から mesh
    // を差した後に差し直す
    PlayerAppearance::PlayerAppearance() noexcept : NS::Obj::Component() {}

    const PlayerParams& PlayerAppearance::Tuning() const noexcept
    {
        if (const ::Player* ownerPlayer = NS::Obj::Cast<::Player>(Owner()))
        {
            return ownerPlayer->Params();
        }
        static const PlayerParams defaults;
        return defaults;
    }

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
        if (::Player* ownerPlayer = NS::Obj::Cast<::Player>(Owner()))
        {
            m_body = &ownerPlayer->Body();
            m_actor = ownerPlayer;
            m_input = &ownerPlayer->ChargeControl();
        }
    }

    void PlayerAppearance::AdvanceSpin() noexcept
    {
        m_spinDegreesThisFrame = 0.0f;
        NS::Obj::Model* renderer = nullptr;
        if (Owner() != nullptr)
        {
            renderer = Owner()->ModelPart();
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
        if (!m_actor->CanMoveBody())
        {
            // 当たりの止めで身体を動かせない間は、絵も止める
            return;
        }

        if (m_actor->IsBodySlamming())
        {
            SetRollAxisToward(m_actor->BodySlamVelocity(), m_spinAxis);
            m_spinSpeed = Tuning().m_bodySlamSpinSpeed;
        }
        else if (m_input != nullptr && m_input->Judge().IsHeld())
        {
            // 放せば出る向きへ回す。溜めて放した突進は狙いの線の向きへ、タップと線の無い時は AimDirection の向きへ出る
            // 狙いが決まらないフレームは前の軸で回し続ける
            NS::Core::Vector3 aim = m_actor->AimDirection();
            NS::Game::Level::AimLine line{};
            if (m_input->IsCharging() && m_input->TryGetAimLine(line))
            {
                aim = line.direction;
            }
            SetRollAxisToward(aim, m_spinAxis);
            m_spinSpeed =
                Tuning().m_emptyChargeSpinSpeed +
                (Tuning().m_fullChargeSpinSpeed - Tuning().m_emptyChargeSpinSpeed) * m_input->Judge().Charge01();
        }
        else if (m_actor->IsRebounding())
        {
            // 弾かれた向きへ前転する。真正面の当たりでは突進と逆向きになる
            // 反動の間は空中の操作で速度の向きが変わっても、弾かれた向きから取った軸のまま回す
            SetRollAxisToward(m_actor->ReboundDirection(), m_spinAxis);
            m_spinSpeed = Tuning().m_bodySlamSpinSpeed;
        }
        // 放した後の空中と、反動に入らずに突進が終わった後は、直前のフレームの軸と速さのまま回る

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
        if (m_body == nullptr)
        {
            return;
        }
        if (m_actor->IsCurled())
        {
            Curl();
        }
        else
        {
            Uncurl();
        }
        AdvanceSpin();
        AdvanceLandingSquash();
    }

    void PlayerAppearance::AdvanceLandingSquash() noexcept
    {
        if (!m_actor->CanMoveBody())
        {
            // 当たりの止めで身体を動かせない間は、潰れの戻しも止める
            return;
        }
        NS::Obj::Model* renderer = nullptr;
        if (Owner() != nullptr)
        {
            renderer = Owner()->ModelPart();
        }
        if (renderer == nullptr)
        {
            return;
        }

        // 反動の出口の条件が成り立ったフレームが着地。移動の後に回るので、このフレームの接地を見る
        // 次のフレームに立ちへ移るので、1 回の反動で 1 フレームだけ成り立つ
        // 戻すフレーム数が 0 以下では戻す手段が無く、潰れたまま残るので潰さない
        float vertical = 1.0f;
        if (m_actor->IsRebounding() && PlayerJudgeLand::Judge(m_body->IsGrounded(), m_body->VerticalVelocity()) &&
            Tuning().m_landingSquashRecoverSteps > 0)
        {
            m_landingSquashRemaining = Tuning().m_landingSquashRecoverSteps;
            vertical = Tuning().m_landingSquash;
        }
        else if (m_landingSquashRemaining > 0)
        {
            --m_landingSquashRemaining;
            if (m_landingSquashRemaining == 0)
            {
                // 補間の残差を残さない。元の形をそのまま書く
                (void)renderer->SetDrawScale(NS::Core::Vector3{1.0f, 1.0f, 1.0f});
                return;
            }
            const float total = static_cast<float>(Tuning().m_landingSquashRecoverSteps);
            const float elapsed = total - static_cast<float>(m_landingSquashRemaining);
            vertical = Tuning().m_landingSquash + (1.0f - Tuning().m_landingSquash) * (elapsed / total);
        }
        else
        {
            return;
        }

        // 水平は体積を保つ 1 ÷ √縦
        const float horizontal = 1.0f / std::sqrt(vertical);
        if (!renderer->SetDrawScale(NS::Core::Vector3{horizontal, vertical, horizontal}))
        {
            NS_LOG_WARN(
                Game, "PlayerAppearance: 着地の潰れが有限の正でなく、潰さなかった: {}", Tuning().m_landingSquash);
            m_landingSquashRemaining = 0;
        }
    }

    void PlayerAppearance::ResolveAssets(NS::Obj::AssetManager& assets)
    {
        NS::Gfx::Mesh* standingPlaceholder = nullptr;
        NS::Gfx::Mesh* ballPlaceholder = nullptr;
        const NS::Obj::Body* player = nullptr;
        if (Owner() != nullptr)
        {
            if (::Player* ownerPlayer = NS::Obj::Cast<::Player>(Owner()))
            {
                player = &ownerPlayer->Body();
            }
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
            NS_LOG_WARN(Game, "PlayerAppearance: 同居する身体の部品が無く、仮の形の寸法を決められない");
        }

        m_standingMesh = ResolveLook(assets, Tuning().m_standingMeshRef, standingPlaceholder);
        m_ballMesh = ResolveLook(assets, Tuning().m_ballMeshRef, ballPlaceholder);
        ShowCurrentLook();
    }

    void PlayerAppearance::ShowCurrentLook() noexcept
    {
        if (Owner() == nullptr)
        {
            return;
        }
        NS::Obj::Model* renderer = Owner()->ModelPart();
        if (renderer == nullptr)
        {
            return;
        }

        NS::Gfx::Mesh* mesh = m_standingMesh;
        if (m_curled)
        {
            mesh = m_ballMesh;
        }
        // 引き当て前は Model が自分の参照から差した mesh を残す
        if (mesh == nullptr)
        {
            return;
        }
        renderer->SetMesh(mesh);
    }

    NS_CLASS(PlayerAppearance)
} // namespace NS::Game::Player
