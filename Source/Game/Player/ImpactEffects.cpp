#include "Game/Player/ImpactEffects.h"

#include "Game/Level/ImpactOutcome.h"
#include "Game/Level/ImpactResolver.h"
#include "Game/Player.h"
#include "Game/Player/PlayerJudges.h"
#include "NSlib/Core/AABB.h"
#include "NSlib/Object/Actor.h"
#include "NSlib/Object/SubObjects/Body.h"
#include "NSlib/Object/SubObjects/Collider.h"
#include "NSlib/Object/Reflection/TypeRegistry.h"
#include "NSlib/Object/Scene/Scene.h"
#include "NSlib/Windows/Clock.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string_view>

namespace NS::Game::Player
{
    namespace
    {
        using NS::Quaternion;
        using NS::Vector3;

        [[nodiscard]] Vector3 NormalizedOr(const Vector3& v, const Vector3& fallback) noexcept
        {
            const float length = v.Length();
            if (!(length > NS::k_Epsilon))
            {
                return fallback;
            }
            return v / length;
        }

        // 絵の +Y を向きへ回す回転
        [[nodiscard]] Quaternion TurnUpTo(const Vector3& direction) noexcept
        {
            return Quaternion::FromToRotation(Vector3{0.0f, 1.0f, 0.0f}, direction);
        }

        // 絵の +Z (輪の面の法線) を向きへ回す回転
        [[nodiscard]] Quaternion TurnNormalTo(const Vector3& direction) noexcept
        {
            return Quaternion::FromToRotation(Vector3{0.0f, 0.0f, 1.0f}, direction);
        }

        [[nodiscard]] float EaseOutCubic(float t) noexcept
        {
            const float clamped = std::clamp(t, 0.0f, 1.0f);
            const float rest = 1.0f - clamped;
            return 1.0f - rest * rest * rest;
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

        // 出ている層の姿勢を置き直す。描画の無い世界と、読めなかった絵では何もしない
        void PlaceLayer(NS::Gfx::EffectScene* effects,
                        const EffectLayerList& layers,
                        std::uint32_t id,
                        const Vector3& position,
                        const Quaternion& rotation,
                        const Vector3& scale) noexcept
        {
            if (effects == nullptr)
            {
                return;
            }
            const EffectLayerRecord* record = layers.Find(id);
            if (record == nullptr)
            {
                return;
            }
            effects->SetTransform(record->handle, position, rotation, scale);
        }

    } // namespace

    ImpactEffects::ImpactEffects() noexcept : NS::Obj::SubObject() {}

    void ImpactEffects::OnStart()
    {
        if (::Player* ownerPlayer = NS::Obj::Cast<::Player>(Owner()))
        {
            m_resolver = &ownerPlayer->Resolver();
            m_player = ownerPlayer;
        }
        NS::Gfx::EffectScene* effects = EffectsOf(*this);
        if (effects == nullptr)
        {
            return;
        }
        // 読めない絵は Play が無効なハンドルを返し、記録だけ残る。警告は EffectScene が名前ごとに 1 回出す
        for (const std::string_view name : {m_coreAsset,
                                            m_streakAsset,
                                            m_ringAsset,
                                            m_sparksAsset,
                                            m_embersAsset,
                                            m_glowAsset,
                                            m_recoilAsset,
                                            m_dustAsset,
                                            m_reboundTrailAsset,
                                            m_landDustAsset})
        {
            static_cast<void>(effects->Preload(name));
        }
    }

    void ImpactEffects::OnUpdate()
    {
        NS::Gfx::EffectScene* effects = EffectsOf(*this);
        m_layers.BeginStep(effects);
        // 頼みは同じフレームの決定の段で事象が置く。読んだら消し、次のフレームへ持ち越さない
        const bool hitRequested = m_hitRequested;
        const bool flightRequested = m_flightRequested;
        m_hitRequested = false;
        m_flightRequested = false;
        if (m_resolver == nullptr)
        {
            return;
        }
        if (hitRequested)
        {
            BeginHit(effects, m_resolver->LastImpact());
        }
        // 同じフレームに置かれた 2 つの頼みは、当たりの絵の後に飛びの絵を出す
        if (m_plan.active && flightRequested)
        {
            m_plan.releaseStep = m_layers.Step();
            BeginFlight(effects);
        }
        AdvanceHit(effects);
        AdvanceFlight(effects);
        AdvanceLanding(effects);
    }

    // 段で変わる絵を段ごとに 1 行で埋める。段を足したら行を足す
    // 入力の番号は段の番号と別に持つ。段の番号をそのまま使うと、3 番の段を足した時に核の出始めの大きさ (3 番)
    // を上書きする
    void ImpactEffects::ApplyTierRow(ImpactShape& shape,
                                     const NS::Game::Level::ImpactRecord& impact,
                                     float power) const noexcept
    {
        switch (impact.tier)
        {
        case NS::Game::Level::HitTier::Center:
        {
            shape.coreInput = 0;
            shape.coreHold = CoreHoldMotion::Pulse;
            // 核の留まりは止めと結ぶ。止めの短い当たりは明けの前に落ち始め、明けの後に光が残らない
            shape.holdLastFrame = std::max(1, std::min(m_coreHoldMax, impact.hitStopSteps - 1));
            shape.sparkStartFrame = std::max(m_centerSparkStart, 0);
            // 手本の弾きは光の塊が消える 11 に火の粉が接触点から新しく弾ける。核が落ちるフレームに揃える
            shape.emberStartFrame = shape.holdLastFrame + 1;
            shape.streakLength = m_streakLengthBase + m_streakLengthPerPower * power;
            shape.ringRadius = m_ringRadiusBase + m_ringRadiusPerPower * power;
            float share = 0.0f;
            if (m_powerMax > m_powerMin)
            {
                share = std::clamp((power - m_powerMin) / (m_powerMax - m_powerMin), 0.0f, 1.0f);
            }
            else if (power >= m_powerMin)
            {
                share = 1.0f;
            }
            const float count =
                static_cast<float>(m_sparkCountMin) + static_cast<float>(m_sparkCountMax - m_sparkCountMin) * share;
            shape.sparkCount = std::max(1, static_cast<int>(std::lround(count)));
            shape.sparkSpeed = m_sparkSpeedBase + m_sparkSpeedPerLaunch * std::max(impact.launchScale, 0.0f);
            shape.sparkHeading = SparkHeading::Launch;
            shape.sparkCountInput = 0;
            shape.emberCount =
                std::max(1, static_cast<int>(std::lround(static_cast<float>(shape.sparkCount) * m_emberShare)));
            // 照りは相手の足元の床に出す。飛んでいた相手の下に床は無い
            if (impact.targetPlaced)
            {
                shape.glowDiameter = m_glowDiameterBase + m_glowDiameterPerPower * power;
            }
            shape.recoilCount = m_recoilCount;
            shape.recoilCountInput = 0;
            return;
        }
        case NS::Game::Level::HitTier::Wide:
            break;
        }
        // 大きな外れの行。番号から作った段の外の値もこの行で出し、中心近くの層を足さない
        shape.coreInput = 2;
        shape.coreHold = CoreHoldMotion::Settle;
        shape.holdLastFrame = std::max(m_wideCoreHoldLast, 0);
        shape.coreCut = true;
        shape.sparkCount = m_wideSparkCount;
        shape.sparkSpeed = m_wideSparkSpeed;
        shape.sparkScale = m_wideSparkScale;
        shape.sparkHeading = SparkHeading::Scrape;
        shape.sparkCountInput = 1;
        shape.recoilCount = m_wideRecoilCount;
        shape.recoilCountInput = 2;
    }

    ImpactShape ImpactEffects::ShapeFor(const NS::Game::Level::ImpactRecord& impact) const noexcept
    {
        ImpactShape shape;
        shape.tier = impact.tier;
        const float power = std::max(impact.power, 0.0f);

        shape.coreDiameter = std::min(m_coreDiameterMax, m_coreDiameterBase + m_coreDiameterPerPower * power);
        ApplyTierRow(shape, impact, power);
        // 量は数と速さの積。数は威力で、速さは飛ばしの比 (重い相手ほど遅い) で決まるので、積は威力の順と
        // 同じ威力での質量の順の両方に並ぶ。速さだけでは質量 8 の溜めきりが質量 1 の通常突進より小さく出た
        shape.sparkAmount = static_cast<float>(shape.sparkCount) * shape.sparkSpeed;

        shape.recoilLength = m_recoilLengthBase + m_recoilLengthPerRebound * std::max(impact.reboundScale, 0.0f);

        // 粉は相手の足元の床に出す。飛んでいた相手の下に床は無い
        if (impact.targetPlaced)
        {
            const float mass = std::max(impact.targetMass, 0.0f);
            shape.dustCount = m_dustCountBase + static_cast<int>(std::lround(std::min(mass, m_dustCountMassLimit)));
            // 威力 1 で質量だけの大きさ。強い当たりほど床を大きく巻き上げ、段の順にも並ぶ
            const float powerGrowth = std::max(0.0f, 1.0f + m_dustScalePerPower * (power - 1.0f));
            shape.dustScale = (m_dustScaleBase + m_dustScalePerRootMass * std::sqrt(mass)) * powerGrowth;
        }

        return shape;
    }

    Vector3 ImpactEffects::MissSparkHeading(float u, float v, const Vector3& slamDirection) const noexcept
    {
        Vector3 forward{};
        if (!NS::TryNormalizeHorizontal(slamDirection, forward))
        {
            return Vector3{};
        }
        const Vector3 right{forward.z, 0.0f, -forward.x};
        const Vector3 side = NormalizedOr(right * u + Vector3{0.0f, v, 0.0f}, Vector3{});
        if (!(side.Length() > 0.5f))
        {
            return Vector3{};
        }
        const Vector3 normal = NS::Game::Level::MissSurfaceNormal(u, v, 2.0f, forward);
        const Vector3 slide = NormalizedOr(forward - normal * NS::Dot(forward, normal), side);
        const float sideShare = std::clamp(m_missSparkSideShare, 0.0f, 1.0f);
        return NormalizedOr(side * sideShare + slide * (1.0f - sideShare), side);
    }

    float ImpactEffects::LandDustRadiusFor(float fallSpeed) const noexcept
    {
        return m_landDustRadiusBase + m_landDustRadiusPerFallSpeed * std::max(fallSpeed, 0.0f);
    }

    Vector3 ImpactEffects::ReboundTrailHeading(const Vector3& velocity,
                                               const Vector3& ballCenter,
                                               const std::optional<Vector3>& cameraPosition) const noexcept
    {
        const Vector3 heading = NormalizedOr(velocity, Vector3{0.0f, 1.0f, 0.0f});
        if (!cameraPosition.has_value())
        {
            return heading;
        }
        const Vector3 sight = NormalizedOr(ballCenter - cameraPosition.value(), Vector3{});
        if (!(sight.Length() > 0.5f))
        {
            return heading;
        }
        // 筋は飛ぶ向きの逆へ伸びる。視線と直角な成分が画面の上に写る向き、視線に沿う成分は奥行き
        const Vector3 back = heading * -1.0f;
        const float along = NS::Dot(back, sight);
        const Vector3 across = back - sight * along;
        const float acrossLength = across.Length();
        // 真っ直ぐ視線に沿って飛ぶ時は画面の上の向きが決まらないので起こさない
        const float minAcross = std::clamp(m_reboundStreakMinAcross, 0.0f, 1.0f);
        if (acrossLength >= minAcross || !(acrossLength > NS::k_Epsilon))
        {
            return heading;
        }
        // 画面の上の向きと、奥か手前かは保つ
        float keptAlong = std::sqrt(1.0f - minAcross * minAcross);
        if (along < 0.0f)
        {
            keptAlong = -keptAlong;
        }
        const Vector3 leaned = sight * keptAlong + across * (minAcross / acrossLength);
        return leaned * -1.0f;
    }

    void ImpactEffects::BeginHit(NS::Gfx::EffectScene* effects, const NS::Game::Level::ImpactRecord& impact)
    {
        // 前の当たりの層が残っていても、次の核は当たりの絵の頭から出す。前の粉は親を止めて短くする
        FinishHeldLayers(effects);
        // 頂点の前に次に当てたら、その反動はここで終わる。尾は輪ごと消す
        if (m_flight.reboundTrail != 0)
        {
            m_layers.Stop(effects, m_flight.reboundTrail);
            m_flight.reboundTrail = 0;
        }
        for (const std::uint32_t dust : m_dusts)
        {
            m_layers.StopRoot(effects, dust);
        }
        m_dusts.clear();

        HitPlan plan;
        plan.active = true;
        plan.freezeStep = m_layers.Step();
        plan.shape = ShapeFor(impact);
        plan.targetId = impact.targetId;

        const Vector3 self = Owner()->Root().Position();
        float radius = 0.0f;
        if (m_player != nullptr)
        {
            radius = m_player->Collider().CapsuleRadius();
        }
        const Vector3 horizontalForward{impact.impactDir.x, 0.0f, impact.impactDir.z};
        plan.launchDir = NormalizedOr(horizontalForward, Vector3{1.0f, 0.0f, 0.0f});
        const Vector3 toTarget = NormalizedOr(impact.targetPos - self, plan.launchDir);
        plan.contact = self + toTarget * radius;
        plan.awayDir = NormalizedOr(Vector3{toTarget.x, 0.0f, toTarget.z}, plan.launchDir);

        // 横ずれの側: 相手の中心から自機への向きのうち、飛ぶ向きに直角な水平の成分
        const Vector3 away = self - impact.targetPos;
        Vector3 across = away - plan.launchDir * NS::Dot(away, plan.launchDir);
        across.y = 0.0f;
        const Vector3 fallbackSide = NS::Cross(Vector3{0.0f, 1.0f, 0.0f}, plan.launchDir);
        plan.sideDir = NormalizedOr(across, NormalizedOr(fallbackSide, Vector3{0.0f, 0.0f, 1.0f}));
        plan.scrapeDir =
            NormalizedOr(plan.sideDir + plan.launchDir * m_scrapeLaunchWeight + Vector3{0.0f, m_scrapeLiftWeight, 0.0f},
                         plan.sideDir);
        // 外れは、どこで、どっちへ力がそれたかを読ませる。面の上の位置から外した側を取り、段と逸れ方と向きを揃える
        // 位置の無い当たり (面で判定できない体) は前の擦れの向きのまま
        if (m_player != nullptr)
        {
            const Vector3 missHeading = MissSparkHeading(impact.faceU, impact.faceV, m_player->BodySlamDirection());
            if (missHeading.Length() > 0.5f)
            {
                plan.scrapeDir = missHeading;
            }
        }
        plan.selfDir = NormalizedOr(impact.selfVelocity, Vector3{0.0f, 1.0f, 0.0f});

        // 輪の法線は相手の飛ぶ向きを残しつつカメラへ起こす。真横から見て縦の線に潰れない
        Vector3 toCamera{};
        const std::optional<Vector3> camera = CameraPosition();
        if (camera.has_value())
        {
            toCamera = NormalizedOr(camera.value() - plan.contact, Vector3{});
        }
        plan.ringNormal =
            NormalizedOr(plan.launchDir * m_ringLaunchWeight + toCamera * m_ringFaceCamera, plan.launchDir);

        // 床は相手の体の外接箱の底。置かれた相手は床に接している。底は当てた時に相手が答えた値
        plan.floor = Vector3{impact.targetPos.x, impact.targetBottom, impact.targetPos.z};
        plan.floor.y += m_floorLift;
        m_plan = plan;
        m_aim.contact = plan.contact;
        m_aim.sparkDir = plan.launchDir;
        if (plan.shape.sparkHeading == SparkHeading::Scrape)
        {
            m_aim.sparkDir = plan.scrapeDir;
        }
        m_aim.recoilDir = plan.selfDir;
        m_aim.ringNormal = plan.ringNormal;

        const ImpactShape& shape = m_plan.shape;
        // 当たりの絵の頭は放射の線を留まる間の縮んだフレームと同じ大きさで出し、星形と核 (動的入力 3 番が掛かる) だけを
        // 出始めの大きさにする。手本は接触のコマで既に光が広がり、層の広がりが次のコマの 9 割を超える
        NS::Gfx::EffectPlayDesc core =
            PlayAt(m_plan.contact, Quaternion::Identity, Uniform(shape.coreDiameter * m_coreHoldPulse));
        core.dynamicInputs[0] = 0.0f;
        core.dynamicInputs[1] = 0.0f;
        core.dynamicInputs[2] = 0.0f;
        core.dynamicInputs[shape.coreInput] = 1.0f;
        core.dynamicInputs[3] = m_coreBirthScale / std::max(m_coreHoldPulse, NS::k_Epsilon);
        m_plan.core = m_layers.Play(effects, m_coreAsset, core);
        m_layers.SetAmount(m_plan.core, shape.coreDiameter);

        // 照りは核と同じ当たりの絵の頭から。手本の弾きは接触のコマで既に床が照らされ、画面の明るさが最大の 4
        // 割まで上がる
        if (shape.glowDiameter > 0.0f)
        {
            // 板の法線 (+Z) を上へ向け、床に寝かせる
            const Quaternion lieFlat = TurnNormalTo(Vector3{0.0f, 1.0f, 0.0f});
            const float riseDiameter = shape.glowDiameter * std::sqrt(std::clamp(m_glowRiseShare, 0.0f, 1.0f));
            m_plan.glow = m_layers.Play(effects, m_glowAsset, PlayAt(m_plan.floor, lieFlat, Uniform(riseDiameter)));
            m_layers.SetAmount(m_plan.glow, shape.glowDiameter * 0.5f);
        }
    }

    void ImpactEffects::PlaySparks(NS::Gfx::EffectScene* effects)
    {
        const ImpactShape& shape = m_plan.shape;
        // 粒の数は段ごとの節の入力に入れ、他の節は 0。3 番の節はどの段も使わない
        Quaternion heading = TurnUpTo(m_plan.launchDir);
        if (shape.sparkHeading == SparkHeading::Scrape)
        {
            heading = TurnUpTo(m_plan.scrapeDir);
        }
        NS::Gfx::EffectPlayDesc sparks = PlayAt(m_plan.contact, heading, Uniform(shape.sparkScale));
        sparks.dynamicInputs[0] = 0.0f;
        sparks.dynamicInputs[1] = 0.0f;
        sparks.dynamicInputs[3] = 0.0f;
        sparks.dynamicInputs[shape.sparkCountInput] = static_cast<float>(shape.sparkCount);
        // 秒の速さを 1 フレームの距離にして絵へ渡す
        sparks.dynamicInputs[2] = shape.sparkSpeed / 60.0f;
        const std::uint32_t id = m_layers.Play(effects, m_sparksAsset, sparks);
        m_layers.SetAmount(id, shape.sparkAmount);
        m_layers.SetRotation(id, heading);
    }

    void ImpactEffects::PlayEmbers(NS::Gfx::EffectScene* effects)
    {
        const ImpactShape& shape = m_plan.shape;
        // 相手の飛ぶ向きを +Y にし、絵は四方へ散らす。数は 0 番、速さの上限は 1 番 (m/フレーム)
        NS::Gfx::EffectPlayDesc embers = PlayAt(m_plan.contact, TurnUpTo(m_plan.launchDir), Uniform(1.0f));
        embers.dynamicInputs[0] = static_cast<float>(shape.emberCount);
        embers.dynamicInputs[1] = shape.sparkSpeed / 60.0f;
        m_layers.SetAmount(m_layers.Play(effects, m_embersAsset, embers), static_cast<float>(shape.emberCount));
    }

    void ImpactEffects::AdvanceHit(NS::Gfx::EffectScene* effects)
    {
        if (!m_plan.active)
        {
            return;
        }
        const int step = m_layers.Step();
        const int frame = step - m_plan.freezeStep;
        const ImpactShape& shape = m_plan.shape;

        if (!m_plan.sparksPlayed && frame == shape.sparkStartFrame)
        {
            m_plan.sparksPlayed = true;
            PlaySparks(effects);
        }

        if (!m_plan.embersPlayed && shape.emberCount > 0 && frame == shape.emberStartFrame)
        {
            m_plan.embersPlayed = true;
            PlayEmbers(effects);
        }

        if (m_plan.core != 0)
        {
            if (frame >= 1 && frame <= shape.holdLastFrame)
            {
                // 描く位置が変わるのはこのステップの終わりの更新の後。1 フレーム目の絵から最大の大きさで写り、
                // 留まる間は偶数のフレームだけ少し縮める。大きな外れは 1 フレーム目の最大から少しずつ縮める
                float diameter = shape.coreDiameter;
                if (shape.coreHold == CoreHoldMotion::Settle)
                {
                    const float settle =
                        std::pow(std::clamp(m_wideFlashDecay, 0.0f, 1.0f), static_cast<float>(frame - 1));
                    const float floor = std::clamp(m_wideFlashFloor, 0.0f, 1.0f);
                    const float share = floor + (1.0f - floor) * settle;
                    diameter = shape.coreDiameter * std::sqrt(share);
                }
                else if (frame % 2 == 0)
                {
                    diameter = shape.coreDiameter * m_coreHoldPulse;
                }
                PlaceLayer(effects, m_layers, m_plan.core, m_plan.contact, Quaternion::Identity, Uniform(diameter));
                if (frame == 1 && effects != nullptr)
                {
                    // 星形と芯を出始めの大きさから再生の大きさへ戻す
                    const EffectLayerRecord* record = m_layers.Find(m_plan.core);
                    if (record != nullptr)
                    {
                        effects->SetDynamicInput(record->handle, 3, 1.0f);
                    }
                }
            }
            if (shape.coreCut)
            {
                if (frame > shape.holdLastFrame)
                {
                    m_layers.Stop(effects, m_plan.core);
                    m_plan.core = 0;
                }
            }
            else if (frame >= shape.holdLastFrame)
            {
                m_layers.StopRoot(effects, m_plan.core);
                m_plan.core = 0;
            }
        }

        if (shape.streakLength > 0.0f && frame == 1)
        {
            m_plan.streak =
                m_layers.Play(effects,
                              m_streakAsset,
                              PlayAt(m_plan.contact, Quaternion::Identity, Vector3{shape.streakLength, 1.0f, 1.0f}));
            m_layers.SetAmount(m_plan.streak, shape.streakLength);
        }
        if (m_plan.streak != 0)
        {
            const int thinned = shape.holdLastFrame + 1;
            const int fadeStart = shape.holdLastFrame + std::max(m_streakTailFrames, 0);
            if (frame >= fadeStart + std::max(m_streakFadeFrames, 0))
            {
                m_layers.Stop(effects, m_plan.streak);
                m_plan.streak = 0;
            }
            else if (frame >= fadeStart)
            {
                // 親を止めた後は太さを置き直さず、絵の定義の落ちるフレーム数で薄れる
                m_layers.StopRoot(effects, m_plan.streak);
            }
            else if (effects != nullptr)
            {
                // 1 から細りきった太さへ、留まりの終わりの次のフレームまでに細り、その後は細いまま残る。
                // 留まる間は核と同じ偶数のフレームだけ長さを縮める
                const int thinFrame = std::min(frame, thinned);
                const float t = static_cast<float>(thinFrame - 1) / static_cast<float>(std::max(1, thinned - 1));
                const float thickness = 1.0f + (m_streakEndThickness - 1.0f) * t;
                float length = shape.streakLength;
                if (frame <= shape.holdLastFrame && frame % 2 == 0)
                {
                    length = shape.streakLength * m_coreHoldPulse;
                }
                PlaceLayer(effects,
                           m_layers,
                           m_plan.streak,
                           m_plan.contact,
                           Quaternion::Identity,
                           Vector3{length, thickness, 1.0f});
            }
        }
        if (m_plan.glow != 0 && frame >= 1 && frame <= shape.holdLastFrame)
        {
            // 照らす面積を、弱め始めるフレームで最大にし、そこから光の塊が消えた次のフレーム (e + 2) の 0 まで
            // 線形に減らす。加算の円なので、画面の明るさに足す量は面積に沿って減る
            float share = std::clamp(m_glowRiseShare, 0.0f, 1.0f);
            if (frame == m_glowDimStart)
            {
                share = 1.0f;
            }
            else if (frame > m_glowDimStart)
            {
                const float span = static_cast<float>(shape.holdLastFrame + 2 - m_glowDimStart);
                share = static_cast<float>(shape.holdLastFrame + 2 - frame) / span;
            }
            const Quaternion lieFlat = TurnNormalTo(Vector3{0.0f, 1.0f, 0.0f});
            PlaceLayer(
                effects, m_layers, m_plan.glow, m_plan.floor, lieFlat, Uniform(shape.glowDiameter * std::sqrt(share)));
        }
        if (m_plan.glow != 0 && frame >= shape.holdLastFrame)
        {
            m_layers.StopRoot(effects, m_plan.glow);
            m_plan.glow = 0;
        }

        if (shape.ringRadius > 0.0f && frame == std::max(m_ringStart, 0))
        {
            m_plan.ring =
                m_layers.Play(effects,
                              m_ringAsset,
                              PlayAt(m_plan.contact, TurnNormalTo(m_plan.ringNormal), Uniform(m_ringStartRadius)));
            m_layers.SetAmount(m_plan.ring, shape.ringRadius);
        }
        if (m_plan.ring != 0)
        {
            if (frame >= m_ringEnd)
            {
                m_layers.Stop(effects, m_plan.ring);
                m_plan.ring = 0;
            }
            else if (effects != nullptr)
            {
                const float t = static_cast<float>(frame - std::max(m_ringStart, 0)) /
                                static_cast<float>(std::max(m_ringFull - std::max(m_ringStart, 0), 1));
                const float radius = m_ringStartRadius + (shape.ringRadius - m_ringStartRadius) * EaseOutCubic(t);
                PlaceLayer(
                    effects, m_layers, m_plan.ring, m_plan.contact, TurnNormalTo(m_plan.ringNormal), Uniform(radius));
            }
        }

        // 自機の側は飛びの絵の頭から。当たりの絵の頭で相手の側、飛びの絵の頭で自機の側が出て、作用と反作用の順に読める
        if (m_plan.releaseStep >= 0)
        {
            if (!m_plan.dustPlayed && step == m_plan.releaseStep)
            {
                m_plan.dustPlayed = true;
                if (shape.dustCount > 0)
                {
                    m_aim.dustOrigin = m_plan.floor + m_plan.awayDir * (shape.dustScale * m_dustAwayShare);
                    NS::Gfx::EffectPlayDesc dust =
                        PlayAt(m_aim.dustOrigin, Quaternion::Identity, Uniform(shape.dustScale));
                    dust.dynamicInputs[0] = static_cast<float>(shape.dustCount);
                    const std::uint32_t id = m_layers.Play(effects, m_dustAsset, dust);
                    m_layers.SetAmount(id, shape.dustScale);
                    m_dusts.push_back(id);
                }
            }
            // 線の本数が 0 の段は出さずに済ませる
            const bool recoilDue = !m_plan.recoilPlayed && step == m_plan.releaseStep + m_recoilDelay;
            if (recoilDue)
            {
                m_plan.recoilPlayed = true;
            }
            if (recoilDue && shape.recoilCount > 0)
            {
                // 玉の縁のうち反動の向きの逆の点から、反動の向きへ開く扇にする。接触点 (玉の相手の側) から出すと、
                // 線の出た側を玉の後ろと読まれ、上へ弾かれた玉が横へ動いて見えた
                float radius = 0.0f;
                if (m_player != nullptr)
                {
                    radius = m_player->Collider().CapsuleRadius();
                }
                m_aim.recoilOrigin = Owner()->Root().Position() - m_plan.selfDir * radius;
                NS::Gfx::EffectPlayDesc recoil = PlayAt(m_aim.recoilOrigin, TurnUpTo(m_plan.selfDir), Uniform(1.0f));
                recoil.dynamicInputs[0] = 0.0f;
                recoil.dynamicInputs[2] = 0.0f;
                recoil.dynamicInputs[shape.recoilCountInput] = static_cast<float>(shape.recoilCount);
                recoil.dynamicInputs[1] = shape.recoilLength;
                m_layers.SetAmount(m_layers.Play(effects, m_recoilAsset, recoil), shape.recoilLength);
            }
        }

        const bool held = m_plan.core != 0 || m_plan.streak != 0 || m_plan.ring != 0 || m_plan.glow != 0;
        const bool embersDone = m_plan.embersPlayed || shape.emberCount == 0;
        if (!held && embersDone && m_plan.recoilPlayed && m_plan.dustPlayed)
        {
            m_plan.active = false;
        }
    }

    void ImpactEffects::BeginFlight(NS::Gfx::EffectScene* effects)
    {
        const int step = m_layers.Step();
        // 前の当たりの尾が残っていれば、ここで消す。尾は 1 本ずつ持つ
        if (m_flight.reboundTrail != 0)
        {
            m_layers.Stop(effects, m_flight.reboundTrail);
            m_flight.reboundTrail = 0;
        }
        // 貫通した時と反動の初速が 0 の時は反動に入らないので、尾は出さない
        // 外れは弾かれた手応えを出さない。尾の輪と筋は弾き返された印に見える
        bool missed = false;
        if (m_resolver != nullptr)
        {
            missed = m_resolver->LastImpact().tier == NS::Game::Level::HitTier::Wide;
        }
        if (m_player != nullptr && m_player->IsRebounding() && !missed)
        {
            const Vector3 ball = Owner()->Root().Position();
            const Vector3 heading = ReboundTrailHeading(m_player->Body().Velocity(), ball, CameraPosition());
            m_flight.reboundTrail =
                m_layers.Play(effects, m_reboundTrailAsset, PlayAt(ball, TurnUpTo(heading), Uniform(1.0f)));
            m_flight.reboundStartStep = step;
        }
        // 飛ばした相手の飛び出しの尾は、相手が自分で出す (LaunchEffects)
    }

    void ImpactEffects::AdvanceFlight(NS::Gfx::EffectScene* effects)
    {
        const int step = m_layers.Step();
        if (m_flight.reboundTrail != 0 && step != m_flight.reboundStartStep)
        {
            // 頂点の前に反動を抜けた時 (縁を掴んだ時など) は、そこで輪ごと消す
            if (m_player == nullptr || !m_player->IsRebounding() ||
                PlayerJudgeLand::Judge(m_player->Body().IsGrounded(), m_player->Body().VerticalVelocity()))
            {
                m_layers.Stop(effects, m_flight.reboundTrail);
                m_flight.reboundTrail = 0;
            }
            else if (!(m_player->Body().VerticalVelocity() > 0.0f))
            {
                // 頂点は縦の速さが 0 以下になったフレーム。親を止め、筋は頂点の位置と向きのまま薄れる
                // 親を止めた後の筋は置き直しても動かない (Effekseer は親の最後の姿で子を描く)
                m_layers.StopRoot(effects, m_flight.reboundTrail);
                m_layers.StopAfter(m_flight.reboundTrail, std::max(m_reboundTrailFadeSteps, 1));
                m_flight.reboundTrail = 0;
            }
            else
            {
                const Vector3 ball = Owner()->Root().Position();
                const Vector3 heading = ReboundTrailHeading(m_player->Body().Velocity(), ball, CameraPosition());
                PlaceLayer(effects, m_layers, m_flight.reboundTrail, ball, TurnUpTo(heading), Uniform(1.0f));
            }
        }
    }

    void ImpactEffects::AdvanceLanding(NS::Gfx::EffectScene* effects)
    {
        if (m_player == nullptr)
        {
            return;
        }
        if (!m_player->IsRebounding())
        {
            m_landingDustPlayed = false;
        }
        else if (!m_landingDustPlayed &&
                 PlayerJudgeLand::Judge(m_player->Body().IsGrounded(), m_player->Body().VerticalVelocity()))
        {
            // 着地の潰れ (PlayerAppearance) と同じ条件。このフレームの縦の速さは既に 0 なので、前のフレームの控えで測る
            m_landingDustPlayed = true;
            const float radius = LandDustRadiusFor(-m_lastVerticalVelocity);
            // 足元は当たりのカプセルの下端 (中心 − 軸 × (半分の高さ + 半径))
            const NS::Phys::Capsule capsule = m_player->Collider().CapsuleAt(Owner()->Root().Position());
            Vector3 at = capsule.center - capsule.axis * (capsule.halfHeight + capsule.radius);
            at.y += m_dustRingLift;
            const std::uint32_t dust = m_layers.Play(
                effects,
                m_landDustAsset,
                PlayAt(at, Quaternion::Identity, Uniform(radius / std::max(m_landDustAssetRadius, NS::k_Epsilon))));
            m_layers.SetAmount(dust, radius);
            m_layers.StopAfter(dust, std::max(m_landDustLife, 1));
        }
        m_lastVerticalVelocity = m_player->Body().VerticalVelocity();
    }

    std::optional<Vector3> ImpactEffects::CameraPosition() const
    {
        // 揺れを掛ける前の位置。輪と尾は世界に置く物なので、揺れの間に向きが震えない
        const std::optional<NS::Obj::CameraPose> pose = NS::Obj::CameraViewPose(*Owner());
        if (!pose.has_value())
        {
            return std::nullopt;
        }
        return pose->position;
    }

    void ImpactEffects::FinishHeldLayers(NS::Gfx::EffectScene* effects)
    {
        if (!m_plan.active)
        {
            return;
        }
        m_layers.StopRoot(effects, m_plan.core);
        m_layers.StopRoot(effects, m_plan.glow);
        m_layers.Stop(effects, m_plan.streak);
        m_layers.Stop(effects, m_plan.ring);
        m_plan = HitPlan{};
    }

    NS_CLASS(ImpactEffects)
} // namespace NS::Game::Player
