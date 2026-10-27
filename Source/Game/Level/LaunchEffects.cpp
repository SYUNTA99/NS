#include "Game/Level/LaunchEffects.h"
#include "NSlib/Core/Types.h"

#include "NSlib/Core/Assert.h"
#include "NSlib/CoreObject/IGet/IGetEffect.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace GL
{
    namespace Level
    {
        namespace
        {
            using NS::Quaternion;
            using NS::Vector3;
            // 尾の絵 launch.trail の、段の色の節を出す動的入力の番号。段ごとに 1 行の表で、段を足したら行を足す
            [[nodiscard]] std::size_t TrailColorInputFor(HitTier tier) noexcept
            {
                switch (tier)
                {
                case HitTier::Center:
                    return 0;
                case HitTier::Wide:
                    return 1;
                }
                // 番号から作った段の外の値は、中心近くの色を出さない
                return 1;
            }


            // 絵の +Y を向きへ回す回転
            [[nodiscard]] Quaternion TurnUpTo(const Vector3& direction) noexcept
            {
                return Quaternion::FromToRotation(Vector3{0.0f, 1.0f, 0.0f}, direction);
            }

            [[nodiscard]] NS::Gfx::EffectPlayDesc PlayAt(const Vector3& position,
                                                         const Quaternion& rotation,
                                                         const Vector3& scale) noexcept
            {
                NS::Gfx::EffectPlayDesc desc;
                desc.position = position;
                desc.rotation = rotation;
                desc.scale = scale;
                return desc;
            }

            [[nodiscard]] Vector3 Uniform(float value) noexcept
            {
                return Vector3{value, value, value};
            }
        } // namespace

        void LaunchEffects::Preload(const NS::IGetEffect* user)
        {
            NS_ASSERT(Game, user != nullptr, "LaunchEffects::Preload: 絵を出すアクターが空");
            NS::PreloadEffects(user, {m_launchTrailAsset, m_launchLandDustAsset});
        }

        void LaunchEffects::BeginTrail(const NS::IGetEffect* user,
                                       const TackleReleaseDesc& release,
                                       const PropFlight& flight,
                                       const MapObjParams* params,
                                       const NS::Sphere& sphere)
        {
            NS_ASSERT(Game, user != nullptr, "LaunchEffects::BeginTrail: 絵を出すアクターが空");
            NS_ASSERT(Game, params != nullptr, "置物の調整値が渡されていない");
            // 尾は 1 本ずつ持つ。前の尾が残っていればここで消す
            if (m_trail != 0)
            {
                NS::StopEffect(user, m_trail);
                m_trail = 0;
            }
            if (!flight.arc)
            {
                return; // 曲線の上を飛んでいない物には付けない
            }

            // 軽い物ほど遠くへ速く飛ぶので、尾も長く残す。重い物は短い
            const float trailFrames =
                static_cast<float>(m_trailFramesBase) + m_trailFramesPerLaunch * std::max(release.launchScale, 0.0f);
            m_trailFrames = std::max(1, static_cast<int>(std::lround(trailFrames)));
            // 威力 1 で質量だけの大きさ。強く飛ばした物ほど高く上がって強く落ちるので威力でも伸ばす
            const float mass = params->Mass();
            const float powerGrowth =
                std::max(0.0f, 1.0f + m_landDustPerPower * (std::max(release.power, 0.0f) - 1.0f));
            m_landDustScale = (m_landDustBase + m_landDustPerRootMass * std::sqrt(std::max(mass, 0.0f))) * powerGrowth;

            // 再生の大きさは自分の直径。帯の幅は絵の定義が直径への割合で持つ。物理への登録に依らず、変換と欄から作る
            m_trailScale = 2.0f * sphere.radius;
            const Vector3& launchDir = release.arc.direction;
            m_launchDir = NS::NormalizedOr(Vector3{launchDir.x, 0.0f, launchDir.z}, Vector3{1.0f, 0.0f, 0.0f});

            // 帯の頭は置物の今の位置に置く。置物が一覧で自機より後ろなら、同じフレームの置物の見た目の段の
            // Update が動いた先へ付け直すので、一覧の順に依らず頭は置物と合う
            // 帯の点の +Y を飛ぶ向きへ回す。揃えないと、横から見た時に幅が道に沿って潰れる
            NS::Gfx::EffectPlayDesc desc =
                PlayAt(flight.position, TurnUpTo(NS::NormalizedOr(flight.velocity, m_launchDir)), Uniform(m_trailScale));
            // 0 番が橙、1 番が大きな外れの灰。2 番が点の寿命
            desc.dynamicInputs[0] = 0.0f;
            desc.dynamicInputs[1] = 0.0f;
            desc.dynamicInputs[TrailColorInputFor(release.tier)] = 1.0f;
            desc.dynamicInputs[2] = static_cast<float>(m_trailFrames);
            desc.dynamicInputs[3] = 0.0f;
            m_trail = NS::PlayEffect(user, m_launchTrailAsset, desc);
            NS::SetEffectAmount(user, m_trail, static_cast<float>(m_trailFrames));
        }

        void LaunchEffects::Update(const NS::IGetEffect* user, const PropFlight& flight)
        {
            NS_ASSERT(Game, user != nullptr, "LaunchEffects::Update: 絵を出すアクターが空");
            if (m_trail == 0)
            {
                return;
            }
            if (flight.arc)
            {
                NS::SetEffectTransform(user,
                                       m_trail,
                                       flight.position,
                                       TurnUpTo(NS::NormalizedOr(flight.velocity, m_launchDir)),
                                       Uniform(m_trailScale));
                return;
            }
            EndTrail(user);
        }

        void LaunchEffects::EndTrail(const NS::IGetEffect* user)
        {
            // 帯と輪はここで消す。点は世界に残るので、通った道が柱のように残る
            NS::StopEffect(user, m_trail);
            m_trail = 0;
        }

        void LaunchEffects::CancelTrail(const NS::IGetEffect* user)
        {
            NS_ASSERT(Game, user != nullptr, "LaunchEffects::CancelTrail: 絵を出すアクターが空");
            EndTrail(user);
        }

        void LaunchEffects::NotifyLanding(const NS::IGetEffect* user, const Vector3& position, const Vector3& normal)
        {
            NS_ASSERT(Game, user != nullptr, "LaunchEffects::NotifyLanding: 絵を出すアクターが空");
            EndTrail(user);
            const Vector3 at = position + normal * m_dustRingLift;
            const NS::uint32 dust = NS::PlayEffect(
                user,
                m_launchLandDustAsset,
                PlayAt(
                    at, TurnUpTo(normal), Uniform(m_landDustScale / std::max(m_landDustAssetRadius, NS::k_Epsilon))));
            NS::SetEffectAmount(user, dust, m_landDustScale);
            NS::StopEffectAfter(user, dust, std::max(m_landDustLife, 1));
        }
    } // namespace Level
} // namespace GL
