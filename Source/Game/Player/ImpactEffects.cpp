#include "Game/Player/ImpactEffects.h"

#include "Game/Level/ImpactResolver.h"
#include "Game/Player.h"
#include "Game/Player/PlayerJudges.h"
#include "Game/Player/PlayerParams.h"
#include "Runtime/Core/AABB.h"
#include "Runtime/Object/Actor.h"
#include "Runtime/Object/Components/Body.h"
#include "Runtime/Object/Components/Collider.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"
#include "Runtime/Object/Scene/Scene.h"
#include "Runtime/Platform/Clock.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string_view>

namespace NS::Game::Player
{
    namespace
    {
        using NS::Core::Quaternion;
        using NS::Core::Vector3;
        using NS::Game::Level::HitTier;

        constexpr std::string_view k_Core = "impact.core";
        constexpr std::string_view k_Streak = "impact.streak";
        constexpr std::string_view k_Ring = "impact.ring";
        constexpr std::string_view k_Sparks = "impact.sparks";
        constexpr std::string_view k_Embers = "impact.embers";
        constexpr std::string_view k_Glow = "impact.glow";
        constexpr std::string_view k_Recoil = "impact.recoil";
        constexpr std::string_view k_Dust = "impact.dust";
        constexpr std::string_view k_ReboundTrail = "rebound.trail";
        constexpr std::string_view k_LandDust = "land.dust";

        // 層のフレームの並びは弾きの手本のコマを写し、絵の寿命 (定義) と組で決まる。欄にすると絵とずれる
        // TODO: 層ごとの時間を当たりのタイムラインの帯で見たくなったら、ここの定数を事象に分ける
        // 中心近くの核が最大近くに留まる最後のフレームの上限。手本は 1〜10 に留まり 11 で落ちる
        constexpr int k_CoreHoldMax = 10;
        // 大きな外れの核が留まる最後のフレーム。落ちる 5 フレーム (絵の定義) と合わせて 0〜8 に見える
        // 手本のガードは接触点の橙の光が 8 まで見える。1 では 5 で薄れ 6 に消え、2〜3 フレーム早かった
        constexpr int k_WideCoreHoldLast = 4;
        // 中心近くの火花を出すフレーム。手本の弾きは塊の中の放射の筋が 3 から出る
        // 0 では接触点から上下へ伸びる筋が 1 フレーム目に見え、手本より 2 フレーム早かった
        // 大きな外れは手本のガードの火花と同じく当たりの絵の頭から
        constexpr int k_CenterSparkStart = 3;
        // 大きな外れの火花の向きに入れる、相手の飛ぶ向きの重み。横ずれの側の重みは 1 で、真ん中の 45 度へ擦れる
        // 横ずれの側だけでは横からの絵で奥行きの向きに潰れ、相手の飛ぶ向きが形に出なかった
        constexpr float k_ScrapeLaunchWeight = 1.0f;
        // 大きな外れの火花の向きに入れる、上の重み。横ずれの側と飛ぶ向きと同じ重みで、35 度上へ擦れ上がる
        // 手本のガードの火花の筋は接触点から上と横へ開き、背丈の 0.4〜0.55 倍の範囲に 18 まで残る
        // 水平に擦らせた時は、粒が重力で床の高さへ下がり、撮った絵で 12〜18 に写る粒は数個だった
        constexpr float k_ScrapeLiftWeight = 1.0f;
        // 照りを弱め始めるフレーム。この次のフレームから照らす面積を減らし、光の塊が消えた次のフレームで 0 にする
        // 手本の層だけの明るさは、弾き (最大 4〜5、最大の 9 割以上が 3〜7) とパリィ (最大 1、9 割以上が 1〜2) で
        // 山の位置と長さが割れる。3 はどちらからも 2 フレームの内に入る最大の位置
        // 核の留まりの終わりまで照りを最大のまま置いた時は、最大が 5、最大の 9 割以上が 2〜8 の 7 フレームで、
        // 光の塊が消える 11 の次に最大の 8 割から 2 割へ 1 フレームで落ちた (手本の弾きは 8〜12 で少しずつ下がる)
        constexpr int k_GlowDimStart = 3;
        // 照りが最大に届く前 (当たりの絵の頭から弱め始めるフレームの前まで) の、照らす面積の割合
        // 最大の面積を 3 だけにし、山を 3 に置く。0〜2 も最大の面積では、中心近くの溜めきりの走行で
        // 2 の明るさ (+2.11) が 3 (+2.09) を上回り、山が 2 になった
        // 0.9 は 2 の明るさを約 0.15 下げ、2 と 3 の差 (0.02) を超える割合
        constexpr float k_GlowRiseShare = 0.9f;
        // 輪が出るフレーム・広がりきるフレーム・消えるフレーム。手本は 3〜4 で出て 9〜10 で消える。絵の寿命は 7
        constexpr int k_RingStart = 3;
        constexpr int k_RingFull = 9;
        constexpr int k_RingEnd = 10;
        // 核が落ちた後も光条が細いまま残り、親を止めて薄れ始めるまでのフレーム数。核の留まりの最後 e から数える
        // 手本の弾きの横線は、発光が 10 を最後に消えた後も最大 (6〜8) の 3〜7 割で 13 まで残り、14 で 2〜4 割、
        // 15 で 1 割ほどに落ちて 16 に消える (光条の行の明るさを 2 本の弾きで測った)。e + 3 まで細いまま置き、
        // e + 3 で親を止める
        // e + 5 まで同じ濃さで置いて e + 6 に消していた時は、手本が薄れた 14・15 にも同じ濃さの線が写った
        constexpr int k_StreakTailFrames = 3;
        // 光条が親を止めてから消えるまでのフレーム数。絵の定義の落ちるフレーム数と同じ。e + 4 に 7 割、e + 5 に 3 割の
        // 濃さで写り、e + 6 に消える。並べた絵の手本の横線は 15 まで残り、16 にも薄く見える所がある
        // 2 の時は e + 4 に半分の濃さで写るのが最後で、手本より 1〜2 フレーム早く消えた
        constexpr int k_StreakFadeFrames = 3;
        // 飛びの絵の頭から弾かれ線を出すまでのフレーム数。手本は当てた側が上がり始めて 4 フレーム後に線が出る
        constexpr int k_RecoilDelay = 4;
        // 火花の数を威力で振る範囲。溜め 0 で縁寄りの 0.7 から、溜めきりで真ん中の 2.0 まで
        constexpr float k_PowerMin = 0.7f;
        constexpr float k_PowerMax = 2.0f;
        // 照りを床から浮かせる高さ (m)。面がぴったり重なるとちらつく
        constexpr float k_FloorLift = 0.03f;
        // 輪の法線に入れる、相手の飛ぶ向きの重み。カメラへの重みは欄
        constexpr float k_RingLaunchWeight = 0.6f;
        // 向きが決まらないと見なす長さ
        constexpr float k_TinyLength = 1e-4f;
        // 止めの間に核が最大近くに留まるうち、偶数のフレームに掛ける大きさの割合。1 フレームおきに 15% 縮めて戻し、
        // 体が止まっている間も核の姿を毎フレーム変える。1 のままでは止めの間の絵が前のフレームと同じだった
        // 0.85 は「最大近く」(手本の弾きの 1〜10) を保ちつつ、面積が 28% 変わる割合
        // 光条も同じフレームに長さへ掛け、両端を長さの 7.5% ずつ縮める。細りだけでは横からの絵の線 (2 画素) の
        // 前のフレームとの差が画素の最大で 11〜12 の同じ姿だった。太さに 0.6 を掛けて試すと、2 画素の線は明るさが
        // 177・100・63 の段でしか変わらず、掛けたフレームと掛けないフレームが同じ段に入る 5〜8 は同じ姿のままだった
        // (手本の弾きの横線は画面の端まで伸びて両端が見えず、濃さがフレームごとに 1〜2 割上下する)
        constexpr float k_CoreHoldPulse = 0.85f;
        // 大きな外れの核が留まる間の面積の移り。1 フレーム目の最大から、落ち着く割合へ 1 フレームごとに差を
        // この割合で詰める (2 で 84%、3 で 78%、4 で 76%)。手本のガードの層だけの明るさが +1 の最大から
        // +2〜+4 で 84・78・74% と、下がり幅を詰めながら 74% へ寄る形を写した。核と放射の線が画面に足す量は面積に沿う
        // 中心近くと同じ 1 フレームおきの脈では、1 と 3 が同じ最大になり、最大が 1 フレームに立たなかった
        constexpr float k_WideFlashFloor = 0.74f;
        constexpr float k_WideFlashDecay = 0.4f;
        // 当たりの粉の輪の真ん中を、相手が居た所から自機と逆の側へずらす距離の、粉の大きさへの割合
        // 粉は輪の半径 0.4〜0.6 と塊の半径 (出始め 0.3、飛びの絵の頭の 3 フレーム後に 0.35)
        // を大きさに掛けて広がるので、 ずらさないと飛びの絵の頭の 3
        // フレーム後に塊の縁が相手の中心から自機の側へ大きさの 0.95 倍まで届き、 1.4 m 離れた自機の縁 (相手の中心から
        // 0.75 m) を越えて自機の輪郭を覆った。見たのは大きな外れの横からの絵 0.7 倍ずらすと届くのは大きさの 0.25 倍
        // (中心近くの 1.65 m で 0.41 m) で、自機の縁まで 0.3 m 以上空く
        constexpr float k_DustAwayShare = 0.7f;
        // 反動の尾の親を止めてから消すまでのフレーム数。絵の定義の筋の落ちるフレーム数と同じ
        // 頂点で親を止め、薄れる間に落ち始める。着地まで付けていた時は、頂点の後の 24 フレームも筋が写った
        constexpr int k_ReboundTrailFadeSteps = 8;
        // 着地の粉と飛ばした物の着地の粉の塊の寿命 (絵の定義と同じ)
        // 着地の粉は、同じ素材の床の土ぼこりである当たりの粉の出てから消えるまでと同じ 30。手本の着地の煙は
        // 着地の +25 でもまだ写っている。20 の時は見える最後が +14 で、手本より 11 フレーム以上早く消えた
        constexpr int k_LandDustLife = 30;
        // 粉の輪が再生の大きさ 1 で広がりきる半径 (m)。絵の定義の出始め 0.4 m と外へ進む 0.8 m の和
        constexpr float k_DustRingRadiusAtUnitScale = 1.2f;
        // 粉の輪を置く床からの高さ (m)。塊の中心を浮かせ、カメラへ向く板の下半分が床に切られないようにする
        constexpr float k_DustRingLift = 0.3f;
        // 反動の尾の筋の形 (絵の定義 rebound.trail の Paint 節と同じ)。玉の見た目の半径 (m)、玉の中心から筋の
        // 真ん中までの長さ (m)、下塗りの幅の半分 (m)
        constexpr float k_BallVisualRadius = 0.65f;
        constexpr float k_ReboundStreakMiddle = 1.3f;
        constexpr float k_ReboundStreakHalfWidth = 0.15f;
        // 筋の向きを視線から離しておく最小の傾き (視線と直角な成分の長さ)。0.615 で視線から 38 度
        // 筋の一番太い真ん中が、下塗りの幅ごと玉の輪郭の外に出る傾き。これより視線に近いと筋は玉の陰に入る
        // 溜めきりの反動は頂点の近くでカメラへ向かって飛び、122〜137 フレームは傾きが 0.15〜0.5 で、
        // 輪郭の外に出る筋の長さが 0.14 m 以下 (頂点の前後では 0) だった
        constexpr float k_ReboundStreakMinAcross =
            (k_BallVisualRadius + k_ReboundStreakHalfWidth) / k_ReboundStreakMiddle;

        [[nodiscard]] Vector3 NormalizedOr(const Vector3& v, const Vector3& fallback) noexcept
        {
            const float length = v.Length();
            if (!(length > k_TinyLength))
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

    ImpactEffects::ImpactEffects() noexcept : NS::Obj::Component() {}

    const PlayerParams& ImpactEffects::Tuning() const noexcept
    {
        if (const ::Player* ownerPlayer = NS::Obj::Cast<::Player>(Owner()))
        {
            return ownerPlayer->Params();
        }
        static const PlayerParams defaults;
        return defaults;
    }

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
        for (const std::string_view name :
             {k_Core, k_Streak, k_Ring, k_Sparks, k_Embers, k_Glow, k_Recoil, k_Dust, k_ReboundTrail, k_LandDust})
        {
            static_cast<void>(effects->Preload(name));
        }
    }

    void ImpactEffects::OnUpdate()
    {
        NS::Gfx::EffectScene* effects = EffectsOf(*this);
        m_layers.BeginStep(effects);
        RunScheduledStops(effects);
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
                                     float power,
                                     const PlayerParams& tuning) noexcept
    {
        switch (impact.tier)
        {
        case HitTier::Center:
        {
            shape.coreInput = 0;
            shape.coreHold = CoreHoldMotion::Pulse;
            // 核の留まりは止めと結ぶ。止めの短い当たりは明けの前に落ち始め、明けの後に光が残らない
            shape.holdLastFrame = std::max(1, std::min(k_CoreHoldMax, impact.hitStopSteps - 1));
            shape.sparkStartFrame = k_CenterSparkStart;
            // 手本の弾きは光の塊が消える 11 に火の粉が接触点から新しく弾ける。核が落ちるフレームに揃える
            shape.emberStartFrame = shape.holdLastFrame + 1;
            shape.streakLength = tuning.m_streakLengthBase + tuning.m_streakLengthPerPower * power;
            shape.ringRadius = tuning.m_ringRadiusBase + tuning.m_ringRadiusPerPower * power;
            const float share = std::clamp((power - k_PowerMin) / (k_PowerMax - k_PowerMin), 0.0f, 1.0f);
            const float count = static_cast<float>(tuning.m_sparkCountMin) +
                                static_cast<float>(tuning.m_sparkCountMax - tuning.m_sparkCountMin) * share;
            shape.sparkCount = std::max(1, static_cast<int>(std::lround(count)));
            shape.sparkSpeed =
                tuning.m_sparkSpeedBase + tuning.m_sparkSpeedPerLaunch * std::max(impact.launchScale, 0.0f);
            shape.sparkHeading = SparkHeading::Launch;
            shape.sparkCountInput = 0;
            shape.emberCount =
                std::max(1, static_cast<int>(std::lround(static_cast<float>(shape.sparkCount) * tuning.m_emberShare)));
            // 照りは相手の足元の床に出す。飛んでいた相手の下に床は無い
            if (impact.targetPlaced)
            {
                shape.glowDiameter = tuning.m_glowDiameterBase + tuning.m_glowDiameterPerPower * power;
            }
            shape.recoilCount = tuning.m_recoilCount;
            shape.recoilCountInput = 0;
            return;
        }
        case HitTier::Wide:
            break;
        }
        // 大きな外れの行。番号から作った段の外の値もこの行で出し、中心近くの層を足さない
        shape.coreInput = 2;
        shape.coreHold = CoreHoldMotion::Settle;
        shape.holdLastFrame = k_WideCoreHoldLast;
        shape.sparkCount = tuning.m_wideSparkCount;
        shape.sparkSpeed = tuning.m_wideSparkSpeed;
        shape.sparkHeading = SparkHeading::Scrape;
        shape.sparkCountInput = 1;
        shape.recoilCount = tuning.m_wideRecoilCount;
        shape.recoilCountInput = 2;
    }

    ImpactShape ImpactEffects::ShapeFor(const NS::Game::Level::ImpactRecord& impact) const noexcept
    {
        ImpactShape shape;
        shape.tier = impact.tier;
        const float power = std::max(impact.power, 0.0f);

        shape.coreDiameter =
            std::min(Tuning().m_coreDiameterMax, Tuning().m_coreDiameterBase + Tuning().m_coreDiameterPerPower * power);
        ApplyTierRow(shape, impact, power, Tuning());
        // 量は数と速さの積。数は威力で、速さは飛ばしの比 (重い相手ほど遅い) で決まるので、積は威力の順と
        // 同じ威力での質量の順の両方に並ぶ。速さだけでは質量 8 の溜めきりが質量 1 のタップより小さく出た
        shape.sparkAmount = static_cast<float>(shape.sparkCount) * shape.sparkSpeed;

        shape.recoilLength =
            Tuning().m_recoilLengthBase + Tuning().m_recoilLengthPerRebound * std::max(impact.reboundScale, 0.0f);

        // 粉は相手の足元の床に出す。飛んでいた相手の下に床は無い
        if (impact.targetPlaced)
        {
            const float mass = std::max(impact.targetMass, 0.0f);
            shape.dustCount =
                Tuning().m_dustCountBase + static_cast<int>(std::lround(std::min(mass, Tuning().m_dustCountMassLimit)));
            // 威力 1 で質量だけの大きさ。強い当たりほど床を大きく巻き上げ、段の順にも並ぶ
            const float powerGrowth = std::max(0.0f, 1.0f + Tuning().m_dustScalePerPower * (power - 1.0f));
            shape.dustScale =
                (Tuning().m_dustScaleBase + Tuning().m_dustScalePerRootMass * std::sqrt(mass)) * powerGrowth;
        }

        return shape;
    }

    float ImpactEffects::LandDustRadiusFor(float fallSpeed) const noexcept
    {
        return Tuning().m_landDustRadiusBase + Tuning().m_landDustRadiusPerFallSpeed * std::max(fallSpeed, 0.0f);
    }

    Vector3 ImpactEffects::ReboundTrailHeading(const Vector3& velocity,
                                               const Vector3& ballCenter,
                                               const std::optional<Vector3>& cameraPosition) noexcept
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
        const float along = NS::Core::Dot(back, sight);
        const Vector3 across = back - sight * along;
        const float acrossLength = across.Length();
        // 真っ直ぐ視線に沿って飛ぶ時は画面の上の向きが決まらないので起こさない
        if (acrossLength >= k_ReboundStreakMinAcross || !(acrossLength > k_TinyLength))
        {
            return heading;
        }
        // 画面の上の向きと、奥か手前かは保つ
        float keptAlong = std::sqrt(1.0f - k_ReboundStreakMinAcross * k_ReboundStreakMinAcross);
        if (along < 0.0f)
        {
            keptAlong = -keptAlong;
        }
        const Vector3 leaned = sight * keptAlong + across * (k_ReboundStreakMinAcross / acrossLength);
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
        Vector3 across = away - plan.launchDir * NS::Core::Dot(away, plan.launchDir);
        across.y = 0.0f;
        const Vector3 fallbackSide = NS::Core::Cross(Vector3{0.0f, 1.0f, 0.0f}, plan.launchDir);
        plan.sideDir = NormalizedOr(across, NormalizedOr(fallbackSide, Vector3{0.0f, 0.0f, 1.0f}));
        plan.scrapeDir =
            NormalizedOr(plan.sideDir + plan.launchDir * k_ScrapeLaunchWeight + Vector3{0.0f, k_ScrapeLiftWeight, 0.0f},
                         plan.sideDir);
        plan.selfDir = NormalizedOr(impact.selfVelocity, Vector3{0.0f, 1.0f, 0.0f});

        // 輪の法線は相手の飛ぶ向きを残しつつカメラへ起こす。真横から見て縦の線に潰れない
        Vector3 toCamera{};
        const std::optional<Vector3> camera = CameraPosition();
        if (camera.has_value())
        {
            toCamera = NormalizedOr(camera.value() - plan.contact, Vector3{});
        }
        plan.ringNormal =
            NormalizedOr(plan.launchDir * k_RingLaunchWeight + toCamera * Tuning().m_ringFaceCamera, plan.launchDir);

        // 床は相手の体の外接箱の底。置かれた相手は床に接している。底は当てた時に相手が答えた値
        plan.floor = Vector3{impact.targetPos.x, impact.targetBottom, impact.targetPos.z};
        plan.floor.y += k_FloorLift;
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
            PlayAt(m_plan.contact, Quaternion::Identity, Uniform(shape.coreDiameter * k_CoreHoldPulse));
        core.dynamicInputs[0] = 0.0f;
        core.dynamicInputs[1] = 0.0f;
        core.dynamicInputs[2] = 0.0f;
        core.dynamicInputs[shape.coreInput] = 1.0f;
        core.dynamicInputs[3] = Tuning().m_coreBirthScale / k_CoreHoldPulse;
        m_plan.core = m_layers.Play(effects, k_Core, core);
        SetAmount(m_plan.core, shape.coreDiameter);

        // 照りは核と同じ当たりの絵の頭から。手本の弾きは接触のコマで既に床が照らされ、画面の明るさが最大の 4
        // 割まで上がる
        if (shape.glowDiameter > 0.0f)
        {
            // 板の法線 (+Z) を上へ向け、床に寝かせる
            const Quaternion lieFlat = TurnNormalTo(Vector3{0.0f, 1.0f, 0.0f});
            const float riseDiameter = shape.glowDiameter * std::sqrt(k_GlowRiseShare);
            m_plan.glow = m_layers.Play(effects, k_Glow, PlayAt(m_plan.floor, lieFlat, Uniform(riseDiameter)));
            SetAmount(m_plan.glow, shape.glowDiameter * 0.5f);
        }
    }

    void ImpactEffects::PlaySparks(NS::Gfx::EffectScene* effects)
    {
        const ImpactShape& shape = m_plan.shape;
        // 粒の数は段ごとの節の入力に入れ、他の節は 0。3 番の節はどの段も使わない
        NS::Gfx::EffectPlayDesc sparks;
        if (shape.sparkHeading == SparkHeading::Scrape)
        {
            sparks = PlayAt(m_plan.contact, TurnUpTo(m_plan.scrapeDir), Uniform(1.0f));
        }
        else
        {
            sparks = PlayAt(m_plan.contact, TurnUpTo(m_plan.launchDir), Uniform(1.0f));
        }
        sparks.dynamicInputs[0] = 0.0f;
        sparks.dynamicInputs[1] = 0.0f;
        sparks.dynamicInputs[3] = 0.0f;
        sparks.dynamicInputs[shape.sparkCountInput] = static_cast<float>(shape.sparkCount);
        // 秒の速さを 1 フレームの距離にして絵へ渡す
        sparks.dynamicInputs[2] = shape.sparkSpeed / 60.0f;
        SetAmount(m_layers.Play(effects, k_Sparks, sparks), shape.sparkAmount);
    }

    void ImpactEffects::PlayEmbers(NS::Gfx::EffectScene* effects)
    {
        const ImpactShape& shape = m_plan.shape;
        // 相手の飛ぶ向きを +Y にし、絵は四方へ散らす。数は 0 番、速さの上限は 1 番 (m/フレーム)
        NS::Gfx::EffectPlayDesc embers = PlayAt(m_plan.contact, TurnUpTo(m_plan.launchDir), Uniform(1.0f));
        embers.dynamicInputs[0] = static_cast<float>(shape.emberCount);
        embers.dynamicInputs[1] = shape.sparkSpeed / 60.0f;
        SetAmount(m_layers.Play(effects, k_Embers, embers), static_cast<float>(shape.emberCount));
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
                    const float settle = std::pow(k_WideFlashDecay, static_cast<float>(frame - 1));
                    const float share = k_WideFlashFloor + (1.0f - k_WideFlashFloor) * settle;
                    diameter = shape.coreDiameter * std::sqrt(share);
                }
                else if (frame % 2 == 0)
                {
                    diameter = shape.coreDiameter * k_CoreHoldPulse;
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
            if (frame >= shape.holdLastFrame)
            {
                m_layers.StopRoot(effects, m_plan.core);
                m_plan.core = 0;
            }
        }

        if (shape.streakLength > 0.0f && frame == 1)
        {
            m_plan.streak =
                m_layers.Play(effects,
                              k_Streak,
                              PlayAt(m_plan.contact, Quaternion::Identity, Vector3{shape.streakLength, 1.0f, 1.0f}));
            SetAmount(m_plan.streak, shape.streakLength);
        }
        if (m_plan.streak != 0)
        {
            const int thinned = shape.holdLastFrame + 1;
            const int fadeStart = shape.holdLastFrame + k_StreakTailFrames;
            if (frame >= fadeStart + k_StreakFadeFrames)
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
                const float thickness = 1.0f + (Tuning().m_streakEndThickness - 1.0f) * t;
                float length = shape.streakLength;
                if (frame <= shape.holdLastFrame && frame % 2 == 0)
                {
                    length = shape.streakLength * k_CoreHoldPulse;
                }
                const EffectLayerRecord* record = m_layers.Find(m_plan.streak);
                if (record != nullptr)
                {
                    effects->SetTransform(
                        record->handle, m_plan.contact, Quaternion::Identity, Vector3{length, thickness, 1.0f});
                }
            }
        }
        if (m_plan.glow != 0 && frame >= 1 && frame <= shape.holdLastFrame)
        {
            // 照らす面積を、弱め始めるフレームで最大にし、そこから光の塊が消えた次のフレーム (e + 2) の 0 まで
            // 線形に減らす。加算の円なので、画面の明るさに足す量は面積に沿って減る
            float share = k_GlowRiseShare;
            if (frame == k_GlowDimStart)
            {
                share = 1.0f;
            }
            else if (frame > k_GlowDimStart)
            {
                const float span = static_cast<float>(shape.holdLastFrame + 2 - k_GlowDimStart);
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

        if (shape.ringRadius > 0.0f && frame == k_RingStart)
        {
            m_plan.ring = m_layers.Play(
                effects,
                k_Ring,
                PlayAt(m_plan.contact, TurnNormalTo(m_plan.ringNormal), Uniform(Tuning().m_ringStartRadius)));
            SetAmount(m_plan.ring, shape.ringRadius);
        }
        if (m_plan.ring != 0)
        {
            if (frame >= k_RingEnd)
            {
                m_layers.Stop(effects, m_plan.ring);
                m_plan.ring = 0;
            }
            else if (effects != nullptr)
            {
                const float t = static_cast<float>(frame - k_RingStart) / static_cast<float>(k_RingFull - k_RingStart);
                const float radius =
                    Tuning().m_ringStartRadius + (shape.ringRadius - Tuning().m_ringStartRadius) * EaseOutCubic(t);
                const EffectLayerRecord* record = m_layers.Find(m_plan.ring);
                if (record != nullptr)
                {
                    effects->SetTransform(
                        record->handle, m_plan.contact, TurnNormalTo(m_plan.ringNormal), Uniform(radius));
                }
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
                    m_aim.dustOrigin = m_plan.floor + m_plan.awayDir * (shape.dustScale * k_DustAwayShare);
                    NS::Gfx::EffectPlayDesc dust =
                        PlayAt(m_aim.dustOrigin, Quaternion::Identity, Uniform(shape.dustScale));
                    dust.dynamicInputs[0] = static_cast<float>(shape.dustCount);
                    const std::uint32_t id = m_layers.Play(effects, k_Dust, dust);
                    SetAmount(id, shape.dustScale);
                    m_dusts.push_back(id);
                }
            }
            if (!m_plan.recoilPlayed && step == m_plan.releaseStep + k_RecoilDelay)
            {
                m_plan.recoilPlayed = true;
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
                SetAmount(m_layers.Play(effects, k_Recoil, recoil), shape.recoilLength);
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
        if (m_player != nullptr && m_player->IsRebounding())
        {
            const Vector3 ball = Owner()->Root().Position();
            const Vector3 heading = ReboundTrailHeading(m_player->Body().Velocity(), ball, CameraPosition());
            m_flight.reboundTrail =
                m_layers.Play(effects, k_ReboundTrail, PlayAt(ball, TurnUpTo(heading), Uniform(1.0f)));
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
                StopLater(m_flight.reboundTrail, k_ReboundTrailFadeSteps);
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
            at.y += k_DustRingLift;
            const std::uint32_t dust = m_layers.Play(
                effects, k_LandDust, PlayAt(at, Quaternion::Identity, Uniform(radius / k_DustRingRadiusAtUnitScale)));
            SetAmount(dust, radius);
            StopLater(dust, k_LandDustLife);
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

    void ImpactEffects::StopLater(std::uint32_t id, int lifeSteps)
    {
        m_scheduledStops.push_back(ScheduledStop{.id = id, .step = m_layers.Step() + lifeSteps});
    }

    void ImpactEffects::RunScheduledStops(NS::Gfx::EffectScene* effects)
    {
        const int step = m_layers.Step();
        for (const ScheduledStop& stop : m_scheduledStops)
        {
            if (step >= stop.step)
            {
                m_layers.Stop(effects, stop.id);
            }
        }
        std::erase_if(m_scheduledStops, [step](const ScheduledStop& stop) { return step >= stop.step; });
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

    void ImpactEffects::SetAmount(std::uint32_t id, float amount) noexcept
    {
        m_layers.SetAmount(id, amount);
    }

    NS_CLASS(ImpactEffects)
} // namespace NS::Game::Player
