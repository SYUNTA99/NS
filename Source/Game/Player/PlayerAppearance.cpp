#include "Game/Player/PlayerAppearance.h"

#include "Game/Player/PlayerStatus.h"
#include "NSlib/Core/Assert.h"
#include "NSlib/Core/Logger.h"
#include "NSlib/CoreObject/ClassObjs/Model.h"
#include <cmath>

namespace
{
    // 参照が書いてあればそのファイルの mesh、空か引き当てられなければ仮の形を返す
    // 上と向きの外積を水平の回転軸として axis へ書く。正の角度で上面がその向きへ倒れる前転になる
    // 長さの無い向きと、水平成分に非数・無限大を含む向きは採らず、axis を書き換えない
    // TryNormalizeHorizontal は長さの 2 乗が非数だと下限との比較が偽になって通すので、先に有限かを見る
    void SetRollAxisToward(const NS::Vector3& direction, NS::Vector3& axis) noexcept
    {
        const float lengthSq = NS::HorizontalLengthSquared(direction);
        if (!std::isfinite(lengthSq))
        {
            return;
        }
        NS::Vector3 forward{};
        if (!NS::TryNormalizeHorizontal(direction, forward))
        {
            return;
        }
        axis = NS::Vector3{forward.z, 0.0f, -forward.x};
    }
} // namespace

namespace GL
{
    namespace Player
    {
        PlayerAppearance::PlayerAppearance()
        {
            NS::LoadAssetField(m_standingMesh, "capsule");
            NS::LoadAssetField(m_ballMesh, "sphere");
        }

        void PlayerAppearance::Curl(NS::Model* renderer) noexcept
        {
            NS_ASSERT(Game, renderer != nullptr, "PlayerAppearance::Curl: 姿を差す Model が空");
            ShowLook(renderer, m_ballMesh, m_ballScale);
        }

        void PlayerAppearance::Uncurl(NS::Model* renderer) noexcept
        {
            NS_ASSERT(Game, renderer != nullptr, "PlayerAppearance::Uncurl: 姿を差す Model が空");
            ShowLook(renderer, m_standingMesh, m_standingScale);
        }

        void PlayerAppearance::ResetDrawScale(NS::Model* renderer) noexcept
        {
            NS_ASSERT(Game, renderer != nullptr, "PlayerAppearance::ResetDrawScale: 倍率を戻す Model が空");
            (void)renderer->SnapDrawScale(NS::Vector3{1.0f, 1.0f, 1.0f});
        }

        void PlayerAppearance::Update(NS::Model* renderer, float dt, const PlayerMotion& motion)
        {
            NS_ASSERT(Game, renderer != nullptr, "PlayerAppearance::Update: 姿を書く Model が空");
            m_spinDegreesThisFrame = 0.0f;

            if (!motion.curled)
            {
                // 立ち姿は回さず、玉の回転と軸と速さも捨てる。丸まり直した玉へ前の玉の値を持ち越さない
                m_spin = NS::Quaternion::Identity;
                m_spinAxis = NS::Vector3::UnitX;
                m_spinSpeed = 0.0f;
                // 前のフレームの値も揃える。今の値だけを戻すと、持ち替えたフレームの立ち姿が、玉の姿勢から戻る途中の
                // 傾きで描かれる
                renderer->SnapLocalRotation(m_spin);
                return;
            }
            if (!motion.canMoveBody)
            {
                // 当たりの止めで身体を動かせない間は、絵も止める
                return;
            }

            if (motion.airDashing || motion.dashRebounding)
            {
                // 進む距離と回る角度を揃えて、今の進む向きへ転がす。真上・真下の間は水平の向きが無く、前の軸のまま
                SetRollAxisToward(motion.velocity, m_spinAxis);
                m_spinSpeed = motion.velocity.Length() * m_dashRollDegreesPerMeter;
            }
            // ほかの間は、直前のフレームの軸と速さのまま回る

            const float degrees = m_spinSpeed * dt;
            const float radians = NS::ToRadians(NS::Degrees{degrees}).value;
            const NS::Quaternion turn = NS::Quaternion::CreateFromAxisAngle(m_spinAxis, radians);
            // 既に回った姿勢の後に今の軸の回転を足す。SimpleMath の q1 * q2 は q1 の後に q2 で、軸は根の空間で固定
            m_spin = m_spin * turn;
            m_spin.Normalize();
            m_spinDegreesThisFrame = degrees;

            renderer->SetLocalRotation(m_spin);
        }

        void PlayerAppearance::ShowLook(NS::Model* renderer, NS::Mesh* mesh, const NS::Vector3& scale) noexcept
        {
            if (!renderer->SetLocalScale(scale))
            {
                NS_LOG_WARN(Game,
                            "PlayerAppearance: 姿の倍率が有限の正でなく、書かなかった: ({}, {}, {})",
                            scale.x,
                            scale.y,
                            scale.z);
            }
            renderer->SetMesh(mesh);
        }
    } // namespace Player
} // namespace GL
