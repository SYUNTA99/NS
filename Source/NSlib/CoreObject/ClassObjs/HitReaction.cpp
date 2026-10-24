#include "NSlib/CoreObject/ClassObjs/HitReaction.h"

#include "NSlib/Core/Assert.h"
#include "NSlib/Core/Logger.h"
#include "NSlib/CoreObject/ClassObjs/HitSensor.h"
#include "NSlib/CoreObject/ClassObjs/Model.h"
#include "NSlib/CoreObject/ClassObjs/ModelShake.h"
#include "NSlib/CoreObject/IUse/IUseCamera.h"
#include "NSlib/CoreObject/IUse/IUseSceneSystem.h"
#include "NSlib/CoreObject/Reflection/ActorFile.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/CoreObject/System/CameraManager.h"
#include "NSlib/CoreObject/System/HitSensorDirector.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <optional>
#include <random>
#include <set>
#include <type_traits>
#include <utility>

namespace NS
{
    namespace
    {
        template <class Kind>
        concept HasReflectedFields = requires { Kind::StaticReflection(); };

        template <class Kind> void TryMakeItem(std::string_view name, std::optional<HitReactionItem>& result)
        {
            if (!result.has_value() && name == Kind{}.typeName)
            {
                result.emplace(std::in_place_type<Kind>);
            }
        }

        template <class... Kinds>
        [[nodiscard]] std::optional<HitReactionItem> MakeByName(std::string_view name,
                                                                std::type_identity<std::variant<Kinds...>>)
        {
            std::optional<HitReactionItem> result;
            (TryMakeItem<Kinds>(name, result), ...);
            return result;
        }

        // 持ち主へ渡す項目の値を書く欄の鍵。リフレクションの欄の表に無いので、欄を当てる前に外す
        constexpr const char* k_OwnerValuesKey = "値";

        // 欄の値の名前から値。数でない値があれば読まずに false
        [[nodiscard]] bool ReadOwnerValues(const nlohmann::json& values, std::map<std::string, float, std::less<>>& out)
        {
            if (!values.is_object())
            {
                return false;
            }
            for (nlohmann::json::const_iterator entry = values.begin(); entry != values.end(); ++entry)
            {
                if (!entry.value().is_number())
                {
                    return false;
                }
                out[entry.key()] = entry.value().get<float>();
            }
            return true;
        }

        // 欄 fields を item へ当てる。壊れていれば error に理由を書いて false
        [[nodiscard]] bool ApplyItemFields(HitReactionItem& item, const nlohmann::json& fields, std::string& error)
        {
            if (!fields.is_object())
            {
                error = "欄 fields が object でない";
                return false;
            }
            nlohmann::json reflected = fields;
            if (HitOwnerItem* owner = std::get_if<HitOwnerItem>(&item))
            {
                const nlohmann::json::const_iterator values = fields.find(k_OwnerValuesKey);
                if (values != fields.end())
                {
                    if (!ReadOwnerValues(*values, owner->values))
                    {
                        error = std::format("欄 {} が名前から数への object でない", k_OwnerValuesKey);
                        return false;
                    }
                    reflected.erase(k_OwnerValuesKey);
                }
            }
            const ReflectionInfo* info = HitReactionItemReflection(item);
            if (info != nullptr)
            {
                (void)ApplyReflectedFields(HitReactionItemFields(item), *info, reflected);
            }
            else if (!reflected.empty())
            {
                NS_LOG_WARN(Scene, "種類 {} は欄を持たないので、欄を読まない", HitReactionItemTypeName(item));
            }
            return true;
        }

        // doc の鍵のうち known に無い物を WARN で知らせる。読まずに飛ばす
        void WarnUnknownKeys(const nlohmann::json& doc,
                             std::initializer_list<std::string_view> known,
                             std::string_view where)
        {
            for (nlohmann::json::const_iterator entry = doc.begin(); entry != doc.end(); ++entry)
            {
                if (std::find(known.begin(), known.end(), entry.key()) == known.end())
                {
                    NS_LOG_WARN(Scene, "{} に読み手のいない欄 {} があり、読まない", where, entry.key());
                }
            }
        }

        // 項目の行 1 つを読む。束の項目は長さ length を持つ
        [[nodiscard]] std::optional<HitReactionEntry> ParseEntry(const nlohmann::json& row,
                                                                 std::size_t index,
                                                                 std::string& error)
        {
            std::string reason;
            std::optional<HitReactionItem> item = ParseHitReactionItem(row, {"length"}, reason);
            if (!item.has_value())
            {
                error = std::format("{} 番目の項目: {}", index, reason);
                return std::nullopt;
            }
            HitReactionEntry entry{.item = std::move(*item)};
            if (!ReadHitReactionFrames(row, "length", entry.length))
            {
                error = std::format("{} 番目の項目の長さ length が 0 以上の整数でない", index);
                return std::nullopt;
            }
            return entry;
        }

        // 揺れのフレーム数の上限。揺れの並びは始める時に全フレームぶんの領域を取るので、欄の打ち間違いの大きな値を止める
        // 1 秒を超える揺れは当たりの返りではなく、画面が揺れ続けている状態
        constexpr int k_MaxShakeFrames = 60;

        // 上限を超えた長さは警告して断る。カメラの無い場面は黙って断る
        bool CanStartCameraMotion(const SceneActor* owner, int frames)
        {
            if (frames > k_MaxShakeFrames)
            {
                NS_LOG_WARN(
                    Scene, "揺れのフレーム数 {} が上限 {} を超えていて、揺らさなかった", frames, k_MaxShakeFrames);
                return false;
            }
            return owner->GetCameraManager() != nullptr;
        }

        // 丸めた整数を 32 ビットの符号なしへ写す。負の値も同じ値なら同じビットになる
        [[nodiscard]] uint32 RoundedBits(float value) noexcept
        {
            return static_cast<uint32>(static_cast<int32>(std::lround(value)));
        }

        // 束を始めた回数と渡した位置から揺れの種を作る。毎回少し違い、同じ入力の再生では同じになる
        // 位置は丸めて、浮動小数の末尾の差で揺れが変わらないようにする。seed_seq の混ぜ方は規格で決まっている
        [[nodiscard]] uint32 StartSeed(uint32 startCount, const Vector3& position)
        {
            std::seed_seq values{startCount,
                                 RoundedBits(position.x * 100.0f),
                                 RoundedBits(position.y * 100.0f),
                                 RoundedBits(position.z * 100.0f)};
            std::array<uint32, 1> seed{};
            values.generate(seed.begin(), seed.end());
            return seed[0];
        }

        // 渡した位置から体の一番遠い所までの長さ。震えが裏まで届く長さに使う
        [[nodiscard]] float BodyLengthOf(const SensorVolume& volume) noexcept
        {
            if (volume.isBox)
            {
                const AABB bounds = volume.Bounds();
                return 2.0f * std::max({bounds.Extents.x, bounds.Extents.y, bounds.Extents.z});
            }
            return 2.0f * volume.radius;
        }
    } // namespace

    // 束か項目を始める時に、項目が共通に使う向きと位置。始めるたびに MakeStartContext が 1 回だけ出す
    struct HitStartContext
    {
        Vector3 position{};    // 渡した位置
        Vector3 toOther{};     // 持ち主のセンサーの中心から相手のセンサーの中心まで
        Vector3 towardOther{}; // toOther の水平の長さ 1 の向き。決まらなければ hasToward が偽
        bool hasToward = false;
        Vector3 cameraRight{1.0f, 0.0f, 0.0f}; // 効果を掛ける前の視点の水平の右
        float side = 0.0f;                     // toOther の cameraRight の成分
        float up = 0.0f;                       // toOther の上の成分
        float bodyLength = 0.0f;               // 持ち主の体の差し渡し (m)
        uint32 seed = 0;                       // 揺れの種
    };

    namespace
    {
        // 束か項目を始める時の向きと位置を、2 つのセンサーと渡した位置から出す
        [[nodiscard]] HitStartContext MakeStartContext(const SceneActor* owner,
                                                       uint32 startCount,
                                                       const Vector3& position,
                                                       const HitSensor& self,
                                                       const HitSensor& other)
        {
            const SensorVolume selfVolume = HitSensorDirector::VolumeOf(&self);
            HitStartContext context;
            context.position = position;
            context.toOther = HitSensorDirector::VolumeOf(&other).Center() - selfVolume.Center();
            context.hasToward = TryNormalizeHorizontal(context.toOther, context.towardOther);
            const Vector3 forward = CalcCameraForwardHorizontal(owner);
            context.cameraRight = Vector3{forward.z, 0.0f, -forward.x};
            context.side = Dot(context.toOther, context.cameraRight);
            context.up = context.toOther.y;
            context.bodyLength = BodyLengthOf(selfVolume);
            context.seed = StartSeed(startCount, position);
            return context;
        }
    } // namespace

    // 項目の種類ごとの始め方。種類を足して書き忘れると、std::visit が呼べずにコンパイルが止まる
    struct HitReaction::ItemStarter
    {
        HitReaction& reaction;
        const HitReactionSelf& owner;
        const HitStartContext& context;
        int length;
        int shapeFrame = 0; // 形を描き始めるフレーム

        void operator()(const HitShapeItem& shape) const
        {
            Model* model = owner.model;
            if (model == nullptr || length <= 0)
            {
                return;
            }
            // 向きが決まらない時は水平の倍率を掛けず、高さだけを変える
            Vector3 alongDirection{};
            if (context.hasToward)
            {
                alongDirection = context.towardOther;
            }
            model->StartDrawShape(DrawShapeDesc{.along = shape.along,
                                                .height = shape.height,
                                                .side = shape.side,
                                                .alongDirection = alongDirection,
                                                .length = length,
                                                .firstFrame = shapeFrame});
        }

        void operator()(const HitBodyShakeItem& shake) const
        {
            const SceneActor* actor = owner.actor;
            Model* model = owner.model;
            if (model == nullptr || length <= 0 || !std::isfinite(shake.amplitudePixels))
            {
                return;
            }
            const std::optional<CameraPose> pose = GetCameraViewPose(actor);
            if (!pose.has_value())
            {
                return;
            }
            // 画面の横を床に沿わせた向きで揺らし、最初は相手と逆の側へ振る。正面の相手は右から
            float firstSign = 1.0f;
            if (Dot(context.toOther, context.cameraRight) > k_Epsilon)
            {
                firstSign = -1.0f;
            }
            model->StartDrawShake(DrawShakeDesc{
                .shape = DrawShakeShape::Side,
                .axis = context.cameraRight,
                .amplitude = ScreenPixelsToMeters(shake.amplitudePixels, *pose, actor->Root().Position()),
                .length = length,
                .seed = context.seed,
                .firstSign = firstSign,
                .flipFrames = shake.flipFrames});
        }

        void operator()(const HitBodyTremorItem& tremor) const
        {
            Model* model = owner.model;
            if (model == nullptr || length <= 0)
            {
                return;
            }
            // 裏まで届くのに長さを使い切ると震えが残らないので、届くのは長さの半分まで
            model->StartDrawTremor(DrawTremorDesc{.contactOffset = context.position - owner.actor->Root().Position(),
                                                  .amplitudePixels = tremor.amplitudePixels,
                                                  .amplitudeRatio = tremor.radiusRatio,
                                                  .reachFrames = std::min(tremor.reachFrames, length / 2),
                                                  .length = length,
                                                  .referenceHeight = tremor.referenceHeight,
                                                  .bodyLength = context.bodyLength});
        }

        void operator()(const HitCameraSinkItem& sink) const
        {
            // 跳ね返りは同じ持ち主の「沈みを戻す」で始まる。来なければ底のまま長さで終わる
            (void)reaction.StartSink(owner.actor,
                                     CameraSinkDesc{.bottomPixels = sink.bottomPixels,
                                                    .sinkFrames = sink.sinkFrames,
                                                    .tremblePixels = sink.tremblePixels,
                                                    .trembleFrames = sink.trembleFrames,
                                                    .tremblePeriodFrames = sink.tremblePeriodFrames,
                                                    .bounceStartFrame = length,
                                                    .overshootRatio = sink.overshootRatio,
                                                    .bouncePeriodFrames = sink.bouncePeriodFrames,
                                                    .frames = length});
        }

        void operator()(const HitCameraSinkReleaseItem&) const
        {
            if (CameraManager* cameras = owner.actor->GetCameraManager())
            {
                cameras->ReleaseSink(owner.actor);
            }
        }

        void operator()(const HitZoomRollItem& zoomRoll) const
        {
            const int frames = std::max(length, 0);
            CameraZoomRollDesc desc;
            desc.zoom = zoomRoll.zoom;
            desc.rollDegrees = zoomRoll.rollDegrees;
            if (context.hasToward)
            {
                desc.rollDirection = context.towardOther;
            }
            desc.returnFrames = std::clamp(zoomRoll.returnFrames, 0, frames);
            desc.holdFrames = frames - desc.returnFrames;
            desc.delayFrames = zoomRoll.delayFrames;
            (void)reaction.StartZoomRoll(owner.actor, desc);
        }

        void operator()(const HitCameraTraumaItem& trauma) const
        {
            CameraTraumaDesc desc;
            desc.trauma = trauma.trauma;
            desc.shape.yawDegrees = trauma.yawDegrees;
            desc.shape.pitchDegrees = trauma.pitchDegrees;
            desc.shape.rollDegrees = trauma.rollDegrees;
            desc.shape.frequency = trauma.frequency;
            desc.shape.decayPerSecond = trauma.decayPerSecond;
            desc.shape.exponent = trauma.exponent;
            desc.seed = context.seed;
            desc.kick.degrees = trauma.kickDegrees;
            desc.kick.peakFrames = trauma.kickPeakFrames;
            // 一撃は相手から自分へ向かう画面の向きへ振る。相手が真正面で向きが決まらなければ振らない
            desc.kick.direction = Vector2{-context.side, -context.up};
            (void)reaction.AddTrauma(owner.actor, desc);
        }

        void operator()(const HitCameraLurchItem& lurch) const
        {
            if (!context.hasToward)
            {
                return;
            }
            (void)reaction.StartNudge(owner.actor,
                                      CameraNudgeDesc{.direction = context.towardOther,
                                                      .distance = lurch.distance,
                                                      .frames = length,
                                                      .onScreen = false});
        }

        void operator()(const HitCameraReboundSwayItem& sway) const
        {
            if (!context.hasToward)
            {
                return;
            }
            (void)reaction.StartNudge(owner.actor,
                                      CameraNudgeDesc{.direction = -context.towardOther,
                                                      .distance = sway.distance,
                                                      .frames = length,
                                                      .onScreen = true});
        }

        void operator()(const HitFlashItem& flash) const { reaction.StartFlash(owner.actor, length, flash.alpha); }

        void operator()(const HitDistortionRingItem& ring) const
        {
            reaction.StartDistortionRing(owner.actor,
                                         HitDistortionRingDesc{.center = context.position,
                                                               .radius = ring.radius,
                                                               .push = ring.push,
                                                               .halfWidth = ring.halfWidth,
                                                               .frames = length});
        }

        void operator()(const HitGroundWaveItem& wave) const
        {
            if (HitScreenDirector* screen = FindSceneSystem<HitScreenDirector>(owner.actor))
            {
                screen->StartGroundWave(owner.actor,
                                        HitGroundWaveDesc{.center = context.position,
                                                          .radius = wave.radius,
                                                          .strength = wave.strength,
                                                          .frames = length});
            }
        }

        void operator()(const HitPadVibrationItem& pad) const
        {
            if (!pad.byDirection)
            {
                reaction.StartPadVibration(owner.actor,
                                           HitPadVibration{.left = pad.left, .right = pad.right, .frames = length});
                return;
            }
            struct Directed
            {
                float weight;
                const Curve* left;
                const Curve* right;
            };
            const HitDirectionWeights weights = BlendHitDirectionWeights(context.side, context.up);
            const std::array<Directed, 4> directed{{{weights.right, &pad.rightLeft, &pad.rightRight},
                                                    {weights.left, &pad.leftLeft, &pad.leftRight},
                                                    {weights.up, &pad.upLeft, &pad.upRight},
                                                    {weights.down, &pad.downLeft, &pad.downRight}}};
            // 重みのある 2 本を同じフレームに重ねて鳴らす
            bool started = false;
            for (const Directed& direction : directed)
            {
                if (!(direction.weight > 0.0f))
                {
                    continue;
                }
                const HitPadVibration vibration{.left = direction.left->Scaled(direction.weight),
                                                .right = direction.right->Scaled(direction.weight),
                                                .frames = length};
                if (started)
                {
                    reaction.BlendPadVibration(owner.actor, vibration);
                    continue;
                }
                reaction.StartPadVibration(owner.actor, vibration);
                started = true;
            }
        }

        void operator()(const HitWorldSpeedItem& speed) const
        {
            if (Scene* scene = owner.actor->OwningScene())
            {
                scene->StartWorldSpeedRamp(speed.startSpeed, speed.returnSeconds, speed.shape);
            }
        }

        void operator()(const HitOthersStopItem&) const
        {
            if (Scene* scene = owner.actor->OwningScene())
            {
                scene->HoldOthers(length);
            }
        }

        void operator()(const HitOwnerItem& ownerItem) const
        {
            reaction.m_ownerRuns.push_back(
                HitOwnerRun{.item = ownerItem, .length = length, .frame = 0, .position = context.position});
        }
    };

    HitDirectionWeights BlendHitDirectionWeights(float side, float up) noexcept
    {
        // 真ん中は角度が決まらない。右へ全部を渡す
        if (side == 0.0f && up == 0.0f)
        {
            return HitDirectionWeights{.right = 1.0f};
        }
        const float angle = ToDegrees(Radians{std::atan2(up, side)}).value;
        const auto weightAt = [angle](float center) {
            // 向きの角度からのずれを -180〜180 へ畳む。90 度離れると 0
            float difference = std::fmod(angle - center, 360.0f);
            if (difference > 180.0f)
            {
                difference -= 360.0f;
            }
            if (difference < -180.0f)
            {
                difference += 360.0f;
            }
            return std::max(1.0f - std::abs(difference) / 90.0f, 0.0f);
        };
        return HitDirectionWeights{
            .right = weightAt(0.0f), .left = weightAt(180.0f), .up = weightAt(90.0f), .down = weightAt(270.0f)};
    }

    void HitReaction::ReloadSets(const SceneActor* owner)
    {
        NS_ASSERT(Scene, owner != nullptr, "HitReaction::ReloadSets: 持ち主が null");
        m_sets.clear();
        const nlohmann::json* actorFile = ActorFileLibrary::Get().Values(owner->ActorFileName());
        if (actorFile != nullptr)
        {
            m_sets = ReadHitReactionSets(owner->ActorFileName(), *actorFile);
        }
    }

    const HitReactionSet* HitReaction::FindSet(std::string_view name) const noexcept
    {
        for (const HitReactionSet& set : m_sets)
        {
            if (set.name == name)
            {
                return &set;
            }
        }
        return nullptr;
    }

    std::optional<int> HitReaction::Start(const HitReactionSelf& owner,
                                          std::string_view name,
                                          const Vector3& position,
                                          const HitSensor* self,
                                          const HitSensor* other)
    {
        NS_ASSERT(Scene, owner.actor != nullptr, "HitReaction::Start: 持ち主が null");
        NS_ASSERT(Scene, self != nullptr && other != nullptr, "HitReaction::Start: センサーが null");
        const HitReactionSet* set = FindSet(name);
        if (set == nullptr)
        {
            NS_LOG_WARN(Scene, ".actor {} に束 {} が無いので、始めなかった", owner.actor->ActorFileName(), name);
            return std::nullopt;
        }
        ++m_startCount;
        const HitStartContext context = MakeStartContext(owner.actor, m_startCount, position, *self, *other);
        for (const HitReactionEntry& entry : set->entries)
        {
            std::visit(ItemStarter{.reaction = *this, .owner = owner, .context = context, .length = entry.length},
                       entry.item);
        }
        return set->stopFrames;
    }

    void HitReaction::StartItem(const HitReactionSelf& owner,
                                const HitReactionItem& item,
                                int length,
                                const Vector3& position,
                                const HitSensor* self,
                                const HitSensor* other,
                                int shapeFrame)
    {
        NS_ASSERT(Scene, owner.actor != nullptr, "HitReaction::StartItem: 持ち主が null");
        NS_ASSERT(Scene, shapeFrame >= 0, "HitReaction::StartItem: 形を描き始めるフレーム {} が負", shapeFrame);
        NS_ASSERT(Scene, self != nullptr && other != nullptr, "HitReaction::StartItem: センサーが null");
        ++m_startCount;
        const HitStartContext context = MakeStartContext(owner.actor, m_startCount, position, *self, *other);
        std::visit(
            ItemStarter{
                .reaction = *this, .owner = owner, .context = context, .length = length, .shapeFrame = shapeFrame},
            item);
    }

    void HitReaction::AdvanceOwnerRuns() noexcept
    {
        for (HitOwnerRun& run : m_ownerRuns)
        {
            ++run.frame;
        }
        std::erase_if(m_ownerRuns, [](const HitOwnerRun& run) { return run.frame >= run.length; });
    }

    void HitReaction::CreateDirectors(const SceneActor* owner)
    {
        NS_ASSERT(Scene, owner != nullptr, "HitReaction::CreateDirectors: 持ち主が null");
        (void)GetOrCreateSceneSystem<HitScreenDirector>(owner);
        (void)GetOrCreateSceneSystem<PadRumbleDirector>(owner);
    }

    void HitReaction::StartFlash(const SceneActor* owner, int frames, float alpha) noexcept
    {
        NS_ASSERT(Scene, owner != nullptr, "HitReaction::StartFlash: 持ち主が null");
        if (HitScreenDirector* screen = FindSceneSystem<HitScreenDirector>(owner))
        {
            screen->StartFlash(owner, frames, alpha);
        }
    }

    bool HitReaction::StartShake(const SceneActor* owner, const CameraShakeDesc& desc)
    {
        NS_ASSERT(Scene, owner != nullptr, "HitReaction::StartShake: 持ち主が null");
        if (desc.frames <= 0)
        {
            return true;
        }
        if (!CanStartCameraMotion(owner, desc.frames))
        {
            return false;
        }
        if (!StartCameraShake(owner, desc))
        {
            NS_LOG_WARN(Scene,
                        "揺れの設定が壊れていて、揺らさなかった: 横 {} 縦 {} フレーム数 {} 最長 {}",
                        desc.sideAmplitude,
                        desc.upAmplitude,
                        desc.frames,
                        desc.longestFlipFrames);
            return false;
        }
        return true;
    }

    bool HitReaction::StartSink(const SceneActor* owner, const CameraSinkDesc& desc)
    {
        NS_ASSERT(Scene, owner != nullptr, "HitReaction::StartSink: 持ち主が null");
        if (desc.frames <= 0)
        {
            return true;
        }
        if (!CanStartCameraMotion(owner, desc.frames))
        {
            return false;
        }
        if (!AddCameraModifier(owner, CameraSinkModifier::Create(desc)))
        {
            NS_LOG_WARN(Scene,
                        "沈む揺れの設定が壊れていて、揺らさなかった: 深さ {} 震え {} 行き過ぎ {} フレーム数 {}",
                        desc.bottomPixels,
                        desc.tremblePixels,
                        desc.overshootRatio,
                        desc.frames);
            return false;
        }
        return true;
    }

    bool HitReaction::StartNudge(const SceneActor* owner, const CameraNudgeDesc& desc)
    {
        NS_ASSERT(Scene, owner != nullptr, "HitReaction::StartNudge: 持ち主が null");
        if (desc.frames <= 0)
        {
            return true;
        }
        if (!CanStartCameraMotion(owner, desc.frames))
        {
            return false;
        }
        if (!AddCameraModifier(owner, CameraNudgeModifier::Create(desc)))
        {
            NS_LOG_WARN(Scene,
                        "ずれの設定が壊れていて、ずらさなかった: 向き ({}, {}, {}) フレーム数 {}",
                        desc.direction.x,
                        desc.direction.y,
                        desc.direction.z,
                        desc.frames);
            return false;
        }
        return true;
    }

    bool HitReaction::StartZoomRoll(const SceneActor* owner, const CameraZoomRollDesc& desc)
    {
        NS_ASSERT(Scene, owner != nullptr, "HitReaction::StartZoomRoll: 持ち主が null");
        if (owner->GetCameraManager() == nullptr)
        {
            return false;
        }
        if (!StartCameraZoomRoll(owner, desc))
        {
            NS_LOG_WARN(Scene,
                        "寄りと傾きの設定が壊れていて、寄せなかった: 倍率 {} 傾き {} 保つ {} 戻す {}",
                        desc.zoom,
                        desc.rollDegrees,
                        desc.holdFrames,
                        desc.returnFrames);
            return false;
        }
        return true;
    }

    void HitReaction::StartShakeLines(const SceneActor* owner, const HitShakeLinesDesc& desc) noexcept
    {
        NS_ASSERT(Scene, owner != nullptr, "HitReaction::StartShakeLines: 持ち主が null");
        if (HitScreenDirector* screen = FindSceneSystem<HitScreenDirector>(owner))
        {
            screen->StartShakeLines(owner, desc);
        }
    }

    void HitReaction::StartDistortionRing(const SceneActor* owner, const HitDistortionRingDesc& desc) noexcept
    {
        NS_ASSERT(Scene, owner != nullptr, "HitReaction::StartDistortionRing: 持ち主が null");
        if (HitScreenDirector* screen = FindSceneSystem<HitScreenDirector>(owner))
        {
            screen->StartDistortionRing(owner, desc);
        }
    }

    void HitReaction::StopDistortionRing(const SceneActor* owner) noexcept
    {
        NS_ASSERT(Scene, owner != nullptr, "HitReaction::StopDistortionRing: 持ち主が null");
        if (HitScreenDirector* screen = FindSceneSystem<HitScreenDirector>(owner))
        {
            screen->StopDistortionRing(owner);
        }
    }

    void HitReaction::StopGroundWave(const SceneActor* owner) noexcept
    {
        NS_ASSERT(Scene, owner != nullptr, "HitReaction::StopGroundWave: 持ち主が null");
        if (HitScreenDirector* screen = FindSceneSystem<HitScreenDirector>(owner))
        {
            screen->StopGroundWave(owner);
        }
    }

    void HitReaction::StartPadVibration(const SceneActor* owner, const HitPadVibration& pad)
    {
        NS_ASSERT(Scene, owner != nullptr, "HitReaction::StartPadVibration: 持ち主が null");
        if (PadRumbleDirector* rumble = FindSceneSystem<PadRumbleDirector>(owner))
        {
            rumble->Start(owner, pad);
        }
    }

    void HitReaction::BlendPadVibration(const SceneActor* owner, const HitPadVibration& pad)
    {
        NS_ASSERT(Scene, owner != nullptr, "HitReaction::BlendPadVibration: 持ち主が null");
        if (PadRumbleDirector* rumble = FindSceneSystem<PadRumbleDirector>(owner))
        {
            rumble->Blend(owner, pad);
        }
    }

    void HitReaction::Stop(const SceneActor* owner)
    {
        NS_ASSERT(Scene, owner != nullptr, "HitReaction::Stop: 持ち主が null");
        if (HitScreenDirector* screen = FindSceneSystem<HitScreenDirector>(owner))
        {
            screen->Stop(owner);
        }
        if (PadRumbleDirector* rumble = FindSceneSystem<PadRumbleDirector>(owner))
        {
            rumble->Stop(owner);
        }
        if (CameraManager* cameras = owner->GetCameraManager())
        {
            for (const void* kind : CameraHitEffectKinds())
            {
                cameras->RemoveModifiersOf(owner, kind);
            }
        }
        m_ownerRuns.clear();
    }

    bool HitReaction::AddTrauma(const SceneActor* owner, const CameraTraumaDesc& desc)
    {
        NS_ASSERT(Scene, owner != nullptr, "HitReaction::AddTrauma: 持ち主が null");
        // カメラの無い場面 (試しの台) では揺らす先が無い。設定の誤りではないので黙って返す
        if (owner->GetCameraManager() == nullptr)
        {
            return false;
        }
        if (!AddCameraTrauma(owner, desc))
        {
            NS_LOG_WARN(Scene,
                        "トラウマの量か形が壊れていて、揺らさなかった: 量 {} 減る速さ {}",
                        desc.trauma,
                        desc.shape.decayPerSecond);
            return false;
        }
        return true;
    }

    void HitReaction::StopAll(const SceneActor* owner)
    {
        Stop(owner);
        // プレイを終えた後の視点にトラウマを残さない。ほかの物が頼んだ効果は、その物が外す
        if (CameraManager* cameras = owner->GetCameraManager())
        {
            cameras->RemoveModifiersOf(owner);
        }
    }

    std::string_view HitReactionItemTypeName(const HitReactionItem& item) noexcept
    {
        return std::visit([]<class Kind>(const Kind& value) -> std::string_view { return value.typeName; }, item);
    }

    std::string_view HitReactionItemLabel(const HitReactionItem& item) noexcept
    {
        return std::visit([]<class Kind>(const Kind& value) -> std::string_view { return value.label; }, item);
    }

    std::optional<HitReactionItem> MakeHitReactionItem(std::string_view typeName)
    {
        return MakeByName(typeName, std::type_identity<HitReactionItem>{});
    }

    const ReflectionInfo* HitReactionItemReflection(const HitReactionItem& item) noexcept
    {
        return std::visit(
            []<class Kind>(const Kind&) -> const ReflectionInfo* {
                if constexpr (HasReflectedFields<Kind>)
                {
                    return Kind::StaticReflection();
                }
                else
                {
                    return nullptr;
                }
            },
            item);
    }

    void* HitReactionItemFields(HitReactionItem& item) noexcept
    {
        return std::visit([]<class Kind>(Kind& value) -> void* { return &value; }, item);
    }

    const void* HitReactionItemFields(const HitReactionItem& item) noexcept
    {
        return std::visit([]<class Kind>(const Kind& value) -> const void* { return &value; }, item);
    }

    bool ReadHitReactionFrames(const nlohmann::json& row, std::string_view key, int& out)
    {
        const nlohmann::json::const_iterator value = row.find(key);
        // 1.5 のような整数でない数を断る
        if (value == row.end() || !value->is_number_integer())
        {
            return false;
        }
        // int へ写す前に範囲を見る。写してから比べると、int の外の数が切り詰められて別の長さで通る
        if (value->is_number_unsigned())
        {
            const std::uint64_t frames = value->get<std::uint64_t>();
            if (frames > static_cast<std::uint64_t>(std::numeric_limits<int>::max()))
            {
                return false;
            }
            out = static_cast<int>(frames);
            return true;
        }
        const std::int64_t frames = value->get<std::int64_t>();
        if (frames < 0 || frames > std::numeric_limits<int>::max())
        {
            return false;
        }
        out = static_cast<int>(frames);
        return true;
    }

    std::optional<HitReactionItem> ParseHitReactionItem(const nlohmann::json& row,
                                                        std::initializer_list<std::string_view> rowKeys,
                                                        std::string& error)
    {
        if (!row.is_object())
        {
            error = "行が object でない";
            return std::nullopt;
        }
        const nlohmann::json::const_iterator type = row.find("type");
        if (type == row.end() || !type->is_string())
        {
            error = "種類 type が無い";
            return std::nullopt;
        }
        std::optional<HitReactionItem> item = MakeHitReactionItem(type->get<std::string>());
        if (!item.has_value())
        {
            error = std::format("種類 {} を知らない", type->get<std::string>());
            return std::nullopt;
        }
        for (nlohmann::json::const_iterator entry = row.begin(); entry != row.end(); ++entry)
        {
            const bool known = entry.key() == "type" || entry.key() == "fields" ||
                               std::find(rowKeys.begin(), rowKeys.end(), entry.key()) != rowKeys.end();
            if (!known)
            {
                NS_LOG_WARN(
                    Scene, "種類 {} の行に読み手のいない欄 {} があり、読まない", type->get<std::string>(), entry.key());
            }
        }
        const nlohmann::json::const_iterator fields = row.find("fields");
        if (fields != row.end() && !ApplyItemFields(*item, *fields, error))
        {
            return std::nullopt;
        }
        return item;
    }

    void WriteHitReactionItem(const HitReactionItem& item, nlohmann::json& row)
    {
        row["type"] = HitReactionItemTypeName(item);
        nlohmann::json fields = nlohmann::json::object();
        const ReflectionInfo* info = HitReactionItemReflection(item);
        if (info != nullptr)
        {
            fields = SerializeReflectedFields(HitReactionItemFields(item), *info);
        }
        if (const HitOwnerItem* owner = std::get_if<HitOwnerItem>(&item))
        {
            nlohmann::json values = nlohmann::json::object();
            for (const std::pair<const std::string, float>& value : owner->values)
            {
                values[value.first] = value.second;
            }
            fields[k_OwnerValuesKey] = std::move(values);
        }
        row["fields"] = std::move(fields);
    }

    std::optional<HitReactionSet> ParseHitReactionSet(const nlohmann::json& doc, std::string& error)
    {
        if (!doc.is_object())
        {
            error = "束が object でない";
            return std::nullopt;
        }
        HitReactionSet set;
        const nlohmann::json::const_iterator name = doc.find("name");
        if (name == doc.end() || !name->is_string() || name->get_ref<const std::string&>().empty())
        {
            error = "名前 name が空でない文字列でない";
            return std::nullopt;
        }
        set.name = name->get<std::string>();
        if (doc.contains("stopFrames") && !ReadHitReactionFrames(doc, "stopFrames", set.stopFrames))
        {
            error = std::format("束 {} の止めの長さ stopFrames が 0 以上の整数でない", set.name);
            return std::nullopt;
        }
        const nlohmann::json::const_iterator items = doc.find("items");
        if (items == doc.end() || !items->is_array())
        {
            error = std::format("束 {} の項目の並び items が配列でない", set.name);
            return std::nullopt;
        }
        WarnUnknownKeys(doc, {"name", "stopFrames", "items"}, std::format("束 {}", set.name));
        set.entries.reserve(items->size());
        for (std::size_t i = 0; i < items->size(); ++i)
        {
            std::string reason;
            std::optional<HitReactionEntry> entry = ParseEntry((*items)[i], i, reason);
            // 1 項目でも読めなければ束ごと読まない。一部だけの束は、書いた物と違う演出になる
            if (!entry.has_value())
            {
                error = std::format("束 {} の {}", set.name, reason);
                return std::nullopt;
            }
            set.entries.push_back(std::move(*entry));
        }
        return set;
    }

    nlohmann::json HitReactionSetToJson(const HitReactionSet& set)
    {
        nlohmann::json items = nlohmann::json::array();
        for (const HitReactionEntry& entry : set.entries)
        {
            nlohmann::json row;
            WriteHitReactionItem(entry.item, row);
            row["length"] = entry.length;
            items.push_back(std::move(row));
        }
        nlohmann::json doc;
        doc["name"] = set.name;
        doc["stopFrames"] = set.stopFrames;
        doc["items"] = std::move(items);
        return doc;
    }

    namespace
    {
        // 束の並びの要素 1 つを読んだ結果。読まない要素は set が空で、reason に理由を持つ
        struct ListedHitReactionSet
        {
            std::optional<HitReactionSet> set;
            std::string reason;
        };

        // 束の並びを要素ごとに読む。読むか読まないかの決まりは、読む時と書き戻す時でこの 1 か所を通す
        std::vector<ListedHitReactionSet> ListHitReactionSets(const nlohmann::json& list)
        {
            std::vector<ListedHitReactionSet> listed(list.size());
            std::set<std::string, std::less<>> names;
            for (std::size_t i = 0; i < list.size(); ++i)
            {
                std::optional<HitReactionSet> set = ParseHitReactionSet(list[i], listed[i].reason);
                if (!set.has_value())
                {
                    continue;
                }
                // 同じ名前を 2 つ持つと、始める束が並びの順で黙って決まる
                if (!names.insert(set->name).second)
                {
                    listed[i].reason = std::format("名前 {} が前の束と重なる", set->name);
                    continue;
                }
                listed[i].set = std::move(set);
            }
            return listed;
        }
    } // namespace

    std::vector<HitReactionSet> ReadHitReactionSets(std::string_view actorFileName, const nlohmann::json& actorFile)
    {
        std::vector<HitReactionSet> sets;
        const nlohmann::json* list = ActorFileHitReactions(actorFile);
        if (list == nullptr)
        {
            return sets;
        }
        if (!list->is_array())
        {
            NS_LOG_ERROR(Scene, ".actor {} の束の並び hitReactions が配列でないので、束を読まない", actorFileName);
            return sets;
        }
        std::vector<ListedHitReactionSet> listed = ListHitReactionSets(*list);
        for (std::size_t i = 0; i < listed.size(); ++i)
        {
            if (!listed[i].set.has_value())
            {
                NS_LOG_ERROR(Scene, ".actor {} の {} 番目の束を読まない。{}", actorFileName, i, listed[i].reason);
                continue;
            }
            sets.push_back(std::move(*listed[i].set));
        }
        return sets;
    }

    void WriteHitReactionSets(nlohmann::json& actorFile, const std::vector<HitReactionSet>& sets)
    {
        nlohmann::json list = nlohmann::json::array();
        std::size_t next = 0;
        const nlohmann::json* current = ActorFileHitReactions(actorFile);
        if (current != nullptr && current->is_array())
        {
            // 読まなかった束は字面のまま同じ位置に残す。捨てると、直していない本人の値が .actor から消える
            const std::vector<ListedHitReactionSet> listed = ListHitReactionSets(*current);
            for (std::size_t i = 0; i < listed.size(); ++i)
            {
                if (!listed[i].set.has_value())
                {
                    list.push_back((*current)[i]);
                    continue;
                }
                if (next < sets.size())
                {
                    list.push_back(HitReactionSetToJson(sets[next]));
                    ++next;
                }
            }
        }
        for (; next < sets.size(); ++next)
        {
            list.push_back(HitReactionSetToJson(sets[next]));
        }
        SetActorFileHitReactions(actorFile, std::move(list));
    }
} // namespace NS
