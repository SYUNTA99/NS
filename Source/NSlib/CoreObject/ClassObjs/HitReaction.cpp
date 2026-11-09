#include "NSlib/CoreObject/ClassObjs/HitReaction.h"

#include "NSlib/Core/Assert.h"
#include "NSlib/Core/Logger.h"
#include "NSlib/CoreObject/ClassObjs/EffectObj.h"
#include "NSlib/CoreObject/ClassObjs/HitSensor.h"
#include "NSlib/CoreObject/ClassObjs/Model.h"
#include "NSlib/CoreObject/ClassObjs/ModelShake.h"
#include "NSlib/CoreObject/IGet/IGetCamera.h"
#include "NSlib/CoreObject/Reflection/ActorFile.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"
#include "NSlib/CoreObject/Reflection/detail/ActorFileItems.h"
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
#include <type_traits>
#include <utility>

namespace NS
{
    namespace
    {
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

        // 種類の欄の表の外の値を fields から読む
        [[nodiscard]] bool ReadExtraFields(HitReactionItem& item, nlohmann::json& fields, std::string& error)
        {
            return item.ReadExtraFields(fields, error);
        }

        // item の種類 type と欄 fields を row へ書く。欄の表の外の値を持つ種類は、その値も欄に書く
        void WriteItem(const HitReactionItem& item, nlohmann::json& row)
        {
            detail::WriteItemRow(item, row);
            item.WriteExtraFields(row["fields"]);
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
            std::optional<HitReactionItem> item =
                detail::ParseItemRow<HitReactionItem>(row, {"length"}, reason, &ReadExtraFields);
            if (!item.has_value())
            {
                error = std::format("{} 番目の項目: {}", index, reason);
                return std::nullopt;
            }
            HitReactionEntry entry{.item = std::move(*item)};
            if (!ReadActorFileFrames(row, "length", entry.length))
            {
                error = std::format("{} 番目の項目の長さ length が 0 以上の整数でない", index);
                return std::nullopt;
            }
            return entry;
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

    namespace
    {
        // 持ち主のシーンの画面の演出。シーンに入っていなければ nullptr
        [[nodiscard]] HitScreenDirector* HitScreenOf(const SceneActor* owner) noexcept
        {
            Scene* scene = owner->OwningScene();
            if (scene == nullptr)
            {
                return nullptr;
            }
            return &scene->HitScreen();
        }

        // 持ち主のシーンの振動。シーンに入っていなければ nullptr
        [[nodiscard]] PadRumbleDirector* PadRumbleOf(const SceneActor* owner) noexcept
        {
            Scene* scene = owner->OwningScene();
            if (scene == nullptr)
            {
                return nullptr;
            }
            return &scene->PadRumble();
        }

        // 束を始める時の向きと位置を、2 つのセンサーと渡した位置から出す
        [[nodiscard]] HitReactionStart MakeStart(HitReaction& reaction,
                                                 const HitReactionSelf& self,
                                                 uint32 startCount,
                                                 const Vector3& position,
                                                 const HitSensor& selfSensor,
                                                 const HitSensor& other)
        {
            const SceneActor* owner = self.actor;
            const SensorVolume selfVolume = HitSensorDirector::VolumeOf(&selfSensor);
            HitReactionStart context;
            context.reaction = &reaction;
            context.owner = self;
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

    void HitShapeInfo::Start(const HitReactionStart& start) const
    {
        Model* model = start.owner.model;
        if (model == nullptr || start.length <= 0)
        {
            return;
        }
        // 向きが決まらない時は水平の倍率を掛けず、高さだけを変える
        Vector3 alongDirection{};
        if (start.hasToward)
        {
            alongDirection = start.towardOther;
        }
        model->StartDrawShape(DrawShapeDesc{.along = along,
                                            .height = height,
                                            .side = side,
                                            .alongDirection = alongDirection,
                                            .length = start.length});
    }

    void HitBodyShakeInfo::Start(const HitReactionStart& start) const
    {
        const SceneActor* actor = start.owner.actor;
        Model* model = start.owner.model;
        if (model == nullptr || start.length <= 0 || !std::isfinite(amplitudePixels))
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
        if (HasPositiveMagnitude(Dot(start.toOther, start.cameraRight)))
        {
            firstSign = -1.0f;
        }
        model->StartDrawShake(
            DrawShakeDesc{.shape = DrawShakeShape::Side,
                          .axis = start.cameraRight,
                          .amplitude = ScreenPixelsToMeters(amplitudePixels, *pose, actor->Root().Position()),
                          .length = start.length,
                          .seed = start.seed,
                          .firstSign = firstSign,
                          .flipFrames = flipFrames});
    }

    void HitBodyTremorInfo::Start(const HitReactionStart& start) const
    {
        Model* model = start.owner.model;
        if (model == nullptr || start.length <= 0)
        {
            return;
        }
        // 裏まで届くのに長さを使い切ると震えが残らないので、届くのは長さの半分まで
        model->StartDrawTremor(DrawTremorDesc{.contactOffset = start.position - start.owner.actor->Root().Position(),
                                              .amplitudePixels = amplitudePixels,
                                              .amplitudeRatio = radiusRatio,
                                              .reachFrames = std::min(reachFrames, start.length / 2),
                                              .length = start.length,
                                              .referenceHeight = referenceHeight,
                                              .bodyLength = start.bodyLength});
    }

    void HitCameraTraumaInfo::Start(const HitReactionStart& start) const
    {
        CameraTraumaDesc desc;
        desc.trauma = trauma;
        desc.shape.yawDegrees = yawDegrees;
        desc.shape.pitchDegrees = pitchDegrees;
        desc.shape.rollDegrees = rollDegrees;
        desc.shape.frequency = frequency;
        desc.shape.decayPerSecond = decayPerSecond;
        desc.shape.exponent = exponent;
        desc.seed = start.seed;
        desc.kick.degrees = kickDegrees;
        desc.kick.peakFrames = kickPeakFrames;
        // 一撃は相手から自分へ向かう画面の向きへ振る。相手が真正面で向きが決まらなければ振らない
        desc.kick.direction = Vector2{-start.side, -start.up};
        (void)start.reaction->AddTrauma(start.owner.actor, desc);
    }

    void HitFlashInfo::Start(const HitReactionStart& start) const
    {
        if (HitScreenDirector* screen = HitScreenOf(start.owner.actor))
        {
            screen->StartFlash(start.owner.actor, start.length, alpha);
        }
    }

    void HitDistortionRingInfo::Start(const HitReactionStart& start) const
    {
        if (HitScreenDirector* screen = HitScreenOf(start.owner.actor))
        {
            screen->StartDistortionRing(start.owner.actor,
                                        HitDistortionRingDesc{.center = start.position,
                                                              .radius = radius,
                                                              .push = push,
                                                              .halfWidth = halfWidth,
                                                              .frames = start.length});
        }
    }

    void HitGroundWaveInfo::Start(const HitReactionStart& start) const
    {
        if (HitScreenDirector* screen = HitScreenOf(start.owner.actor))
        {
            screen->StartGroundWave(start.owner.actor,
                                    HitGroundWaveDesc{.center = start.position,
                                                      .radius = radius,
                                                      .strength = strength,
                                                      .frames = start.length});
        }
    }

    void HitPadInfo::Start(const HitReactionStart& start) const
    {
        PadRumbleDirector* rumble = PadRumbleOf(start.owner.actor);
        if (rumble == nullptr)
        {
            return;
        }
        if (!byDirection)
        {
            const PadRumbleDesc vibration{.left = left, .right = right, .frames = start.length};
            if (layered)
            {
                rumble->Blend(start.owner.actor, vibration);
                return;
            }
            rumble->Start(start.owner.actor, vibration);
            return;
        }
        struct Directed
        {
            float weight;
            const Curve* left;
            const Curve* right;
        };
        const HitDirectionWeights weights = BlendHitDirectionWeights(start.side, start.up);
        const std::array<Directed, 4> directed{{{weights.right, &rightLeft, &rightRight},
                                                {weights.left, &leftLeft, &leftRight},
                                                {weights.up, &upLeft, &upRight},
                                                {weights.down, &downLeft, &downRight}}};
        // 重みのある 2 本を同じフレームに重ねて鳴らす。重ねる項目は 1 本目から走っている振動に重ねる
        bool started = layered;
        for (const Directed& direction : directed)
        {
            if (!IsPositive(direction.weight))
            {
                continue;
            }
            const PadRumbleDesc vibration{.left = direction.left->Scaled(direction.weight),
                                            .right = direction.right->Scaled(direction.weight),
                                            .frames = start.length};
            if (started)
            {
                rumble->Blend(start.owner.actor, vibration);
                continue;
            }
            rumble->Start(start.owner.actor, vibration);
            started = true;
        }
    }

    void HitOwnerInfo::Start(const HitReactionStart& start) const
    {
        start.reaction->m_ownerRuns.push_back(
            HitOwnerRun{.item = *this, .length = start.length, .frame = 0, .position = start.position});
    }

    void HitSoundInfo::Start(const HitReactionStart& start) const { PlaySound(start.owner.actor, sound, volume); }

    void HitEffectInfo::Start(const HitReactionStart& start) const
    {
        EffectObj* effects = start.owner.actor->GetEffect();
        if (effects == nullptr || effect == nullptr)
        {
            return;
        }
        Gfx::EffectPlayDesc desc;
        desc.position = start.position;
        (void)effects->Play(start.owner.actor->GetEffectScene(), effect, desc);
    }
    HitReactionItem::HitReactionItem(const HitReactionItem& other)
    {
        if (other.m_value != nullptr)
        {
            m_value = other.m_value->Clone();
        }
    }

    HitReactionItem& HitReactionItem::operator=(const HitReactionItem& other)
    {
        if (this != &other)
        {
            HitReactionItem copy{other};
            m_value = std::move(copy.m_value);
        }
        return *this;
    }

    std::string_view HitReactionItem::TypeName() const noexcept
    {
        if (m_value == nullptr)
        {
            return {};
        }
        return m_value->TypeName();
    }

    std::string_view HitReactionItem::Label() const noexcept
    {
        if (m_value == nullptr)
        {
            return {};
        }
        return m_value->Label();
    }

    const ReflectionInfo* HitReactionItem::Reflection() const noexcept
    {
        if (m_value == nullptr)
        {
            return nullptr;
        }
        return m_value->Reflection();
    }

    void* HitReactionItem::Fields() noexcept
    {
        if (m_value == nullptr)
        {
            return nullptr;
        }
        return m_value->Fields();
    }

    const void* HitReactionItem::Fields() const noexcept
    {
        if (m_value == nullptr)
        {
            return nullptr;
        }
        return std::as_const(*m_value).Fields();
    }

    void HitReactionItem::Start(const HitReactionStart& start) const
    {
        if (m_value != nullptr)
        {
            m_value->Start(start);
        }
    }

    bool HitReactionItem::ReadExtraFields(nlohmann::json& fields, std::string& error)
    {
        if (m_value == nullptr)
        {
            return true;
        }
        return m_value->ReadExtraFields(fields, error);
    }

    void HitReactionItem::WriteExtraFields(nlohmann::json& fields) const
    {
        if (m_value != nullptr)
        {
            m_value->WriteExtraFields(fields);
        }
    }

    std::optional<HitReactionItem> MakeItemOf(std::string_view typeName, std::type_identity<HitReactionItem>)
    {
        const HitReactionItemKind* kind = HitReactionItemRegistry::Get().Find(typeName);
        if (kind == nullptr)
        {
            return std::nullopt;
        }
        return kind->make();
    }

    HitReactionItemRegistry& HitReactionItemRegistry::Get() noexcept
    {
        static HitReactionItemRegistry registry;
        return registry;
    }

    void HitReactionItemRegistry::Register(const HitReactionItemKind& kind)
    {
        if (Find(kind.typeName) != nullptr)
        {
            NS_ASSERT(Scene, false, "HitReactionItemRegistry::Register: 同じ名前の種類を 2 度登録した");
            NS_LOG_ERROR(Scene, "当たりの項目の種類 {} を 2 度登録しようとしたので、後の登録を断った", kind.typeName);
            return;
        }
        m_kinds.push_back(kind);
    }

    const HitReactionItemKind* HitReactionItemRegistry::Find(std::string_view typeName) const noexcept
    {
        for (const HitReactionItemKind& kind : m_kinds)
        {
            if (kind.typeName == typeName)
            {
                return &kind;
            }
        }
        return nullptr;
    }

    bool HitOwnerInfo::ReadExtraFields(HitOwnerInfo& item, nlohmann::json& fields, std::string& error)
    {
        const nlohmann::json::const_iterator values = fields.find(k_OwnerValuesKey);
        if (values == fields.end())
        {
            return true;
        }
        if (!ReadOwnerValues(*values, item.values))
        {
            error = std::format("欄 {} が名前から数への object でない", k_OwnerValuesKey);
            return false;
        }
        fields.erase(k_OwnerValuesKey);
        return true;
    }

    void HitOwnerInfo::WriteExtraFields(const HitOwnerInfo& item, nlohmann::json& fields)
    {
        nlohmann::json values = nlohmann::json::object();
        for (const std::pair<const std::string, float>& value : item.values)
        {
            values[value.first] = value.second;
        }
        fields[k_OwnerValuesKey] = std::move(values);
    }

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
        HitReactionStart start = MakeStart(*this, owner, m_startCount, position, *self, *other);
        for (const HitReactionEntry& entry : set->entries)
        {
            start.length = entry.length;
            entry.item.Start(start);
        }
        return set->stopFrames;
    }

    void HitReaction::AdvanceOwnerRuns() noexcept
    {
        for (HitOwnerRun& run : m_ownerRuns)
        {
            ++run.frame;
        }
        std::erase_if(m_ownerRuns, [](const HitOwnerRun& run) { return run.frame >= run.length; });
    }

    std::vector<HitOwnerRun> HitReaction::OwnerRunsSince(std::size_t index) const
    {
        const std::size_t first = std::min(index, m_ownerRuns.size());
        return {m_ownerRuns.begin() + static_cast<std::ptrdiff_t>(first), m_ownerRuns.end()};
    }

    void HitReaction::StopDistortionRing(const SceneActor* owner) noexcept
    {
        NS_ASSERT(Scene, owner != nullptr, "HitReaction::StopDistortionRing: 持ち主が null");
        if (HitScreenDirector* screen = HitScreenOf(owner))
        {
            screen->StopDistortionRing(owner);
        }
    }

    void HitReaction::StopGroundWave(const SceneActor* owner) noexcept
    {
        NS_ASSERT(Scene, owner != nullptr, "HitReaction::StopGroundWave: 持ち主が null");
        if (HitScreenDirector* screen = HitScreenOf(owner))
        {
            screen->StopGroundWave(owner);
        }
    }

    void HitReaction::ScaleRunning(const HitReactionSelf& owner, float scale)
    {
        NS_ASSERT(Scene, owner.actor != nullptr, "HitReaction::ScaleRunning: 持ち主が null");
        NS_ASSERT(Scene, std::isfinite(scale) && scale >= 0.0f, "HitReaction::ScaleRunning: 倍率が負か非数");
        if (HitScreenDirector* screen = HitScreenOf(owner.actor))
        {
            screen->Scale(owner.actor, scale);
        }
        if (PadRumbleDirector* rumble = PadRumbleOf(owner.actor))
        {
            rumble->Scale(owner.actor, scale);
        }
        if (owner.model != nullptr)
        {
            owner.model->ScaleDrawShaking(scale);
        }
    }

    void HitReaction::Stop(const SceneActor* owner)
    {
        NS_ASSERT(Scene, owner != nullptr, "HitReaction::Stop: 持ち主が null");
        if (HitScreenDirector* screen = HitScreenOf(owner))
        {
            screen->Stop(owner);
        }
        if (PadRumbleDirector* rumble = PadRumbleOf(owner))
        {
            rumble->Stop(owner);
        }
        if (CameraManager* cameras = owner->GetCameraManager())
        {
            // 持ち主が頼む効果は当たりの項目とトラウマだけ。トラウマは続けて当てると足されるので残す
            cameras->RemoveModifiersOfExcept(owner, CameraTraumaModifier::StaticKind());
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
        if (doc.contains("stopFrames") && !ReadActorFileFrames(doc, "stopFrames", set.stopFrames))
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
            WriteItem(entry.item, row);
            row["length"] = entry.length;
            items.push_back(std::move(row));
        }
        nlohmann::json doc;
        doc["name"] = set.name;
        doc["stopFrames"] = set.stopFrames;
        doc["items"] = std::move(items);
        return doc;
    }

    std::vector<HitReactionSet> ReadHitReactionSets(std::string_view actorFileName, const nlohmann::json& actorFile)
    {
        return detail::ReadNamedActorFileItems(
            actorFileName, ActorFileHitReactions(actorFile), &ParseHitReactionSet, "hitReactions", "束");
    }

    void WriteHitReactionSets(nlohmann::json& actorFile, const std::vector<HitReactionSet>& sets)
    {
        // 読まなかった束は字面のまま同じ位置に残す。捨てると、直していない本人の値が .actor から消える
        SetActorFileHitReactions(
            actorFile,
            detail::BuildNamedActorFileItemsJson(
                ActorFileHitReactions(actorFile), sets, &ParseHitReactionSet, &HitReactionSetToJson, "束"));
    }

    NS_HIT_REACTION_ITEM(HitShapeInfo)
    NS_HIT_REACTION_ITEM(HitBodyShakeInfo)
    NS_HIT_REACTION_ITEM(HitBodyTremorInfo)
    NS_HIT_REACTION_ITEM(HitCameraTraumaInfo)
    NS_HIT_REACTION_ITEM(HitFlashInfo)
    NS_HIT_REACTION_ITEM(HitDistortionRingInfo)
    NS_HIT_REACTION_ITEM(HitGroundWaveInfo)
    NS_HIT_REACTION_ITEM(HitPadInfo)
    NS_HIT_REACTION_ITEM(HitOwnerInfo)
    NS_HIT_REACTION_ITEM(HitSoundInfo)
    NS_HIT_REACTION_ITEM(HitEffectInfo)
} // namespace NS
