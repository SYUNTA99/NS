#include "Game/Level/SlamArrow.h"

#include "Game/Level/CollisionInput.h"
#include "Game/Level/ImpactResolver.h"
#include "Game/Player/PlayerComponent.h"
#include "Runtime/Graphics/FrameConstants.h"
#include "Runtime/Graphics/Material.h"
#include "Runtime/Graphics/Pipeline.h"
#include "Runtime/Graphics/RenderContext.h"
#include "Runtime/Graphics/StaticMesh.h"
#include "Runtime/Object/AssetManager.h"
#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"
#include "Runtime/Object/Scene/Scene.h"
#include "Runtime/Object/Transform.h"
#include "Runtime/Physics/PhysicsScene.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>

namespace NS::Game::Level
{
    namespace
    {
        // 帯を区切る間隔 (m)。床の端で帯が宙へはみ出す長さを、この半分の 5 cm 以下に抑える
        constexpr float k_GroundProbeSpacing = 0.1f;
        // 玉の下の床を探す深さ (m)。自機の影 (Shadow の最大投影距離の既定 12 m) が出る高さまでは矢印も出す
        constexpr float k_BallGroundSearchDepth = 12.0f;
        // 同じ高さとみなす差 (m)。光線の距離の丸めの差で、平らな床の帯が板に割れないようにする
        constexpr float k_SameHeightTolerance = 1e-3f;
        // 帯の区切りの数の上限。極端な距離で光線と板の数が膨らむのを止める
        constexpr float k_MaxBandPieces = 4096.0f;
        // 帯の絵の横幅のうち、明るい縁の外側どうしの間 (玉の通る幅) が占める割合。絵の作り (512 画素のうち 448)
        // に合わせた値
        constexpr float k_BandTextureSpan = 448.0f / 512.0f;
        // 矢じりの絵の幅と高さのうち、不透明な範囲が占める割合。絵の作り (1024 × 512 画素のうち 960 × 480) に合わせた値
        constexpr float k_HeadTextureSpan = 960.0f / 1024.0f;

        constexpr const char* k_QuadMesh = "shadowQuad";
        constexpr const char* k_BandMaterialPath = "Assets/Materials/ground_arrow_band.mat";
        constexpr const char* k_HeadMaterialPath = "Assets/Materials/ground_arrow_head.mat";

        // Shaders/ground_arrow.ps.hlsl の cbuffer と同じ並び。頂点シェーダは standard.vs.hlsl をそのまま使うので、
        // FrameCB と同じ大きさにし、world と viewProj を同じ位置に置く。残りは照明の欄の場所に地面の矢印の値を置く
        struct alignas(16) GroundArrowConstants
        {
            NS::Core::Matrix world{};
            NS::Core::Matrix viewProj{};
            NS::Core::Vector4 chargedColor{}; // rgb は色の付いた部分の色、a は明るい縁の不透明度
            NS::Core::Vector4 plainColor{};   // rgb は色の付いていない部分の色、a は明るい縁の不透明度
            NS::Core::Vector4 darkColor{};    // rgb は暗い縁の色、a はその不透明度
            // xy は始まりのぼかし、zw は色の付いた部分の先の境目。どちらも板の v の 1 次式 (v = 0 の値と v
            // あたりの変化)
            NS::Core::Vector4 fadeAndFront{};
            // xy は帯の切れ目を測る、矢じりの先からの距離 ÷ 矢じりの奥行き (板の v の 1 次式)
            // z と w は色の付いた部分と付いていない部分の塗りの平均の不透明度
            NS::Core::Vector4 rearAndFill{};
        };
        static_assert(sizeof(GroundArrowConstants) == sizeof(NS::Gfx::FrameCB), "FrameCB と同じ大きさで送る");
        static_assert(offsetof(GroundArrowConstants, world) == offsetof(NS::Gfx::FrameCB, world),
                      "頂点シェーダが読む world の位置");
        static_assert(offsetof(GroundArrowConstants, viewProj) == offsetof(NS::Gfx::FrameCB, viewProj),
                      "頂点シェーダが読む viewProj の位置");
        static_assert(std::is_trivially_copyable_v<GroundArrowConstants>, "FrameCB へバイトで写す");

        // 板の v (0 が遠い端、1 が近い端) の 1 次式。v での値は value + slope × v
        struct PlateLinear
        {
            float value = 0.0f;
            float slope = 0.0f;
        };

        // 線に沿った距離 along の 1 次式 (a + b × along) を、線に沿った範囲 [alongNear, alongFar] の板の v の 1
        // 次式へ直す
        [[nodiscard]] PlateLinear ToPlateV(float a, float b, float alongNear, float alongFar) noexcept
        {
            return PlateLinear{.value = a + b * alongFar, .slope = b * (alongNear - alongFar)};
        }

        [[nodiscard]] bool IsPositiveFinite(float value) noexcept
        {
            return std::isfinite(value) && value > 0.0f;
        }

        [[nodiscard]] bool IsNonNegativeFinite(float value) noexcept
        {
            return std::isfinite(value) && value >= 0.0f;
        }

        [[nodiscard]] bool IsFiniteVector(const NS::Core::Vector3& value) noexcept
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        [[nodiscard]] bool IsValidDesc(const SlamArrowDesc& desc) noexcept
        {
            if (desc.growFrames <= 0)
            {
                return false;
            }
            if (!IsPositiveFinite(desc.headWidth) || !IsPositiveFinite(desc.headDepthMin) ||
                !IsPositiveFinite(desc.headDepthMax) || !IsPositiveFinite(desc.startFade) ||
                !IsPositiveFinite(desc.frontSoftness))
            {
                return false;
            }
            // 下限が上限を超えると Clamp の結果が決まらない
            if (desc.headDepthMin > desc.headDepthMax)
            {
                return false;
            }
            const float nonNegative[] = {desc.groundLift,
                                         desc.headDepthRatio,
                                         desc.lateStageFrom,
                                         desc.darkAlpha,
                                         desc.bandEdgeAlpha,
                                         desc.bandFillAlpha,
                                         desc.headEdgeAlpha,
                                         desc.headFillAlpha,
                                         desc.plainBandEdgeAlpha,
                                         desc.plainBandFillAlpha,
                                         desc.plainHeadEdgeAlpha,
                                         desc.plainHeadFillAlpha};
            for (const float value : nonNegative)
            {
                if (!IsNonNegativeFinite(value))
                {
                    return false;
                }
            }
            return IsFiniteVector(desc.earlyColor) && IsFiniteVector(desc.lateColor) &&
                   IsFiniteVector(desc.fullColor) && IsFiniteVector(desc.plainColor) && IsFiniteVector(desc.darkColor);
        }

        // 上向きの板 (shadowQuad。1 × 1 m で、+z の端が v = 0) を、線に沿った範囲と横の幅へ伸ばし、線の向きへ回して高さ
        // height に置く
        [[nodiscard]] NS::Core::Matrix PlateWorld(
            const SlamArrowShape& shape, float alongNear, float alongFar, float width, float height) noexcept
        {
            const NS::Core::Vector3& forward = shape.direction;
            // 左手系で y が上。前が +z の時に右が +x になる向き
            const NS::Core::Vector3 right{forward.z, 0.0f, -forward.x};
            const float length = alongFar - alongNear;
            const NS::Core::Vector3 center = shape.origin + forward * ((alongNear + alongFar) * 0.5f);
            return NS::Core::Matrix{right.x * width,
                                    0.0f,
                                    right.z * width,
                                    0.0f,
                                    0.0f,
                                    1.0f,
                                    0.0f,
                                    0.0f,
                                    forward.x * length,
                                    0.0f,
                                    forward.z * length,
                                    0.0f,
                                    center.x,
                                    height,
                                    center.z,
                                    1.0f};
        }

        // 帯と矢じりの板 1 枚ぶんの描く値
        struct PlateLook
        {
            float edgeAlpha = 0.0f;      // 色の付いた部分の明るい縁の不透明度
            float fillAlpha = 0.0f;      // 色の付いた部分の塗りの平均の不透明度
            float plainEdgeAlpha = 0.0f; // 色の付いていない部分の明るい縁の不透明度
            float plainFillAlpha = 0.0f; // 色の付いていない部分の塗りの平均の不透明度
            bool cutUnderHead = false;   // 帯を矢じりの後ろの端で切る場合 true
        };

        [[nodiscard]] GroundArrowConstants MakeConstants(const NS::Gfx::RenderContext& context,
                                                         const SlamArrowShape& shape,
                                                         const SlamArrowDesc& desc,
                                                         const SlamArrowPiece& plate,
                                                         float width,
                                                         const PlateLook& look) noexcept
        {
            GroundArrowConstants constants{};
            constants.world = PlateWorld(shape, plate.alongNear, plate.alongFar, width, plate.height);
            constants.viewProj = context.viewProjection;
            constants.chargedColor =
                NS::Core::Vector4{shape.stageColor.x, shape.stageColor.y, shape.stageColor.z, look.edgeAlpha};
            constants.plainColor =
                NS::Core::Vector4{desc.plainColor.x, desc.plainColor.y, desc.plainColor.z, look.plainEdgeAlpha};
            constants.darkColor =
                NS::Core::Vector4{desc.darkColor.x, desc.darkColor.y, desc.darkColor.z, desc.darkAlpha};

            // 始まりのぼかし: (along − 玉の縁) ÷ ぼかす長さ。0 以下で消え、1 以上で全部出る
            const PlateLinear fade =
                ToPlateV(-shape.start / desc.startFade, 1.0f / desc.startFade, plate.alongNear, plate.alongFar);
            // 色の付いた部分の重み: (色の先 − along) ÷ 境目の幅 + 0.5。境目の真ん中で半分になる
            PlateLinear front{.value = 1.0f, .slope = 0.0f};
            if (!shape.fullyColored)
            {
                front = ToPlateV(shape.colorFront / desc.frontSoftness + 0.5f,
                                 -1.0f / desc.frontSoftness,
                                 plate.alongNear,
                                 plate.alongFar);
            }
            // 帯の切れ目: (矢じりの先 − along) ÷ 矢じりの奥行きが、帯の絵の a (その横の位置で切れる割合)
            // 以上の所だけ帯を出す
            PlateLinear rear{.value = 1.0f, .slope = 0.0f};
            if (look.cutUnderHead)
            {
                rear = ToPlateV(shape.tip / shape.headDepth, -1.0f / shape.headDepth, plate.alongNear, plate.alongFar);
            }
            constants.fadeAndFront = NS::Core::Vector4{fade.value, fade.slope, front.value, front.slope};
            constants.rearAndFill = NS::Core::Vector4{rear.value, rear.slope, look.fillAlpha, look.plainFillAlpha};
            return constants;
        }

        [[nodiscard]] NS::Gfx::DrawItem MakeDrawItem(NS::Gfx::StaticMesh* mesh,
                                                     NS::Gfx::Material* material,
                                                     const GroundArrowConstants& constants) noexcept
        {
            NS::Gfx::DrawItem item{};
            item.mesh = mesh;
            item.material = material;
            item.blend = NS::Gfx::BlendMode::Alpha; // 深度は読むだけ。壁と手前の物に隠れる
            std::memcpy(&item.constants, &constants, sizeof(constants));
            return item;
        }

        // .mat を読んだマテリアル。読めなければ nullptr
        [[nodiscard]] NS::Gfx::Material* LoadArrowMaterial(NS::Obj::AssetManager& assets, const char* path)
        {
            const std::optional<std::string> resolved = NS::Obj::ResolveContentPath(path);
            if (!resolved)
            {
                return nullptr;
            }
            return assets.LoadMaterial(*resolved).material;
        }
    } // namespace

    bool BuildSlamArrow(const SlamArrowState& state, const SlamArrowDesc& desc, SlamArrowShape& outShape)
    {
        if (!IsValidDesc(desc) || state.framesSinceShown < 0)
        {
            return false;
        }
        if (!IsPositiveFinite(state.ballRadius) || !IsFiniteVector(state.line.origin) ||
            !std::isfinite(state.targetContact) || !std::isfinite(state.charge01))
        {
            return false;
        }
        NS::Core::Vector3 direction{};
        // 非数と無限の向きは正規化を通り抜ける
        if (!NS::Core::TryNormalizeHorizontal(state.line.direction, direction) || !IsFiniteVector(direction))
        {
            return false;
        }

        SlamArrowShape shape{};
        shape.origin = state.line.origin;
        shape.direction = direction;
        shape.bandWidth = state.ballRadius * 2.0f;
        shape.start = state.ballRadius;
        // 玉の縁が相手に触れる所で玉の中心が居る位置から玉の半径だけ先 (相手の手前の面)
        shape.fullTip = std::max(state.targetContact + state.ballRadius, shape.start);

        // 矢印を出したフレームを 1 フレーム目として等速に伸びる
        const float grown =
            std::min(1.0f, (static_cast<float>(state.framesSinceShown) + 1.0f) / static_cast<float>(desc.growFrames));
        shape.tip = shape.start + (shape.fullTip - shape.start) * grown;
        shape.headDepth = NS::Core::Clamp(desc.headDepthRatio * shape.tip, desc.headDepthMin, desc.headDepthMax);

        const float charge = NS::Core::Clamp(state.charge01, 0.0f, 1.0f);
        shape.colorFront = shape.start + (shape.fullTip - shape.start) * charge;
        shape.fullyColored = state.chargeFull;
        if (state.chargeFull)
        {
            shape.stageColor = desc.fullColor;
        }
        else if (charge < desc.lateStageFrom)
        {
            shape.stageColor = desc.earlyColor;
        }
        else
        {
            shape.stageColor = desc.lateColor;
        }

        outShape = std::move(shape);
        return true;
    }

    void PlaceSlamArrowOnGround(const SlamArrowGroundProbe& probe, float groundLift, SlamArrowShape& shape)
    {
        shape.band.clear();
        shape.hasHead = false;
        if (!probe || !(shape.tip > shape.start) || !std::isfinite(groundLift))
        {
            return;
        }
        float ballGround = 0.0f;
        if (!probe(shape.origin, k_BallGroundSearchDepth, ballGround))
        {
            return;
        }
        // 玉の中心の高さから、玉の下の床より玉の半径だけ深い所まで探す。上りは玉の中心の高さまでの段を拾う
        // それより深い所 (段を下りた先・落下死の体積の上面) には貼らない
        const float depth = (shape.origin.y - ballGround) + shape.bandWidth * 0.5f;
        if (!IsPositiveFinite(depth))
        {
            return;
        }

        const float pieceCount = std::ceil((shape.tip - shape.start) / k_GroundProbeSpacing);
        if (!(pieceCount <= k_MaxBandPieces))
        {
            return;
        }
        const int count = static_cast<int>(pieceCount);
        int lastIndex = -2;
        for (int i = 0; i < count; ++i)
        {
            const float alongNear = shape.start + k_GroundProbeSpacing * static_cast<float>(i);
            const float alongFar = std::min(alongNear + k_GroundProbeSpacing, shape.tip);
            const NS::Core::Vector3 from = shape.origin + shape.direction * ((alongNear + alongFar) * 0.5f);
            float ground = 0.0f;
            if (!probe(from, depth, ground))
            {
                continue;
            }
            const float height = ground + groundLift;
            if (lastIndex == i - 1 && std::abs(shape.band.back().height - height) <= k_SameHeightTolerance)
            {
                shape.band.back().alongFar = alongFar;
            }
            else
            {
                shape.band.push_back(SlamArrowPiece{.alongNear = alongNear, .alongFar = alongFar, .height = height});
            }
            lastIndex = i;
        }

        // 矢じりは奥行きの真ん中の真下の 1 つの高さに置く
        const NS::Core::Vector3 headCenter = shape.origin + shape.direction * (shape.tip - shape.headDepth * 0.5f);
        float headGround = 0.0f;
        if (probe(headCenter, depth, headGround))
        {
            shape.hasHead = true;
            shape.head = SlamArrowPiece{
                .alongNear = shape.tip - shape.headDepth, .alongFar = shape.tip, .height = headGround + groundLift};
        }
    }

    // -130 は CollisionInput (-140) がこのフレームの狙いの線と狙う相手を控えた後に読むため
    SlamArrow::SlamArrow() noexcept : NS::Obj::Component(NS::Obj::TickPriority::Update - 130) {}

    void SlamArrow::OnStart()
    {
        NS::Obj::GameObject* owner = Owner();
        if (owner == nullptr)
        {
            return;
        }
        m_input = owner->FindComponent<CollisionInput>();
        m_movement = owner->FindComponent<NS::Game::Player::PlayerComponent>();
        if (NS::Obj::Scene* scene = owner->OwningScene())
        {
            scene->RegisterRenderable(this);
        }
    }

    void SlamArrow::OnEndPlay()
    {
        NS::Obj::GameObject* owner = Owner();
        if (owner == nullptr)
        {
            return;
        }
        if (NS::Obj::Scene* scene = owner->OwningScene())
        {
            scene->UnregisterRenderable(this);
        }
    }

    void SlamArrow::ResolveAssets(NS::Obj::AssetManager& assets)
    {
        m_mesh = assets.Builtin(k_QuadMesh);
        m_bandMaterial = LoadArrowMaterial(assets, k_BandMaterialPath);
        m_headMaterial = LoadArrowMaterial(assets, k_HeadMaterialPath);
    }

    void SlamArrow::OnUpdate()
    {
        m_hasShown = false;
        // 溜め量は放した後も残るので、溜めているかで組むフレームを決める。カメラの正面に相手がいなければ出さない
        SlamArrowState state{};
        SlamLineTarget target{};
        if (m_input == nullptr || m_movement == nullptr || !m_input->IsCharging() ||
            !m_input->TryGetAimLine(state.line) || !m_input->TryGetAimTarget(target))
        {
            m_framesSinceShown = -1;
            return;
        }
        // 前のフレームに出ていなければ 0 から数える。相手が替わっても数え直さない
        if (m_framesSinceShown < 0)
        {
            m_framesSinceShown = 0;
        }
        else if (m_framesSinceShown < std::numeric_limits<int>::max())
        {
            ++m_framesSinceShown;
        }

        state.ballRadius = m_movement->CapsuleRadius();
        state.targetContact = target.contact;
        state.framesSinceShown = m_framesSinceShown;
        state.charge01 = m_input->Judge().Charge01();
        state.chargeFull = m_input->IsChargeFull();

        SlamArrowShape shape{};
        if (!BuildSlamArrow(state, m_desc, shape))
        {
            return;
        }
        if (const NS::Obj::Scene* scene = Owner()->OwningScene())
        {
            const NS::Phys::PhysicsScene& physics = scene->Physics();
            const SlamArrowGroundProbe probe =
                [&physics](const NS::Core::Vector3& from, float maxDepth, float& outGroundY) {
                    float distance = 0.0f;
                    if (!physics.Raycast(from, NS::Core::Vector3{0.0f, -1.0f, 0.0f}, maxDepth, distance))
                    {
                        return false;
                    }
                    outGroundY = from.y - distance;
                    return true;
                };
            PlaceSlamArrowOnGround(probe, m_desc.groundLift, shape);
        }
        m_shown = std::move(shape);
        m_hasShown = true;
    }

    void SlamArrow::Collect(const NS::Gfx::RenderContext& context, std::vector<NS::Gfx::DrawItem>& out)
    {
        if (!IsActive() || !m_hasShown || m_mesh == nullptr || m_bandMaterial == nullptr || m_headMaterial == nullptr)
        {
            return;
        }
        // 帯の板は絵の横幅ぶん広く置き、明るい縁の外側を玉の通る幅の端に合わせる
        const float bandPlateWidth = m_shown.bandWidth / k_BandTextureSpan;
        const PlateLook bandLook{.edgeAlpha = m_desc.bandEdgeAlpha,
                                 .fillAlpha = m_desc.bandFillAlpha,
                                 .plainEdgeAlpha = m_desc.plainBandEdgeAlpha,
                                 .plainFillAlpha = m_desc.plainBandFillAlpha,
                                 .cutUnderHead = true};
        for (const SlamArrowPiece& piece : m_shown.band)
        {
            out.push_back(MakeDrawItem(
                m_mesh, m_bandMaterial, MakeConstants(context, m_shown, m_desc, piece, bandPlateWidth, bandLook)));
        }
        if (!m_shown.hasHead)
        {
            return;
        }
        // 矢じりの板は絵の余白ぶん広く長く置き、不透明な範囲を幅と奥行きに合わせる
        const float lengthMargin = m_shown.headDepth * (1.0f / k_HeadTextureSpan - 1.0f) * 0.5f;
        const SlamArrowPiece headPlate{.alongNear = m_shown.head.alongNear - lengthMargin,
                                       .alongFar = m_shown.head.alongFar + lengthMargin,
                                       .height = m_shown.head.height};
        const PlateLook headLook{.edgeAlpha = m_desc.headEdgeAlpha,
                                 .fillAlpha = m_desc.headFillAlpha,
                                 .plainEdgeAlpha = m_desc.plainHeadEdgeAlpha,
                                 .plainFillAlpha = m_desc.plainHeadFillAlpha,
                                 .cutUnderHead = false};
        out.push_back(MakeDrawItem(
            m_mesh,
            m_headMaterial,
            MakeConstants(context, m_shown, m_desc, headPlate, m_desc.headWidth / k_HeadTextureSpan, headLook)));
    }

    NS::Core::Vector3 SlamArrow::SortCenter() const noexcept
    {
        const NS::Obj::GameObject* owner = Owner();
        if (owner == nullptr)
        {
            return {};
        }
        return owner->Root().Position();
    }

    NS::Core::AABB SlamArrow::WorldBounds() const noexcept
    {
        if (!m_hasShown)
        {
            return NS::Core::AABB{SortCenter(), NS::Core::Vector3{0.0f, 0.0f, 0.0f}};
        }
        // 線の始まりから矢じりの先 (余白込み) までを、板の幅の半分だけ横へ広げて覆う。高さは一番低い板から線の高さまで
        const float reach = m_shown.tip + m_shown.headDepth;
        const float halfWidth =
            std::max(m_shown.bandWidth / k_BandTextureSpan, m_desc.headWidth / k_HeadTextureSpan) * 0.5f;
        const NS::Core::Vector3 reachEnd = m_shown.origin + m_shown.direction * reach;
        float lowest = m_shown.origin.y;
        for (const SlamArrowPiece& piece : m_shown.band)
        {
            lowest = std::min(lowest, piece.height);
        }
        if (m_shown.hasHead)
        {
            lowest = std::min(lowest, m_shown.head.height);
        }
        const NS::Core::Vector3 low{std::min(m_shown.origin.x, reachEnd.x) - halfWidth,
                                    lowest,
                                    std::min(m_shown.origin.z, reachEnd.z) - halfWidth};
        const NS::Core::Vector3 high{std::max(m_shown.origin.x, reachEnd.x) + halfWidth,
                                     m_shown.origin.y,
                                     std::max(m_shown.origin.z, reachEnd.z) + halfWidth};
        return NS::Core::AABB{(low + high) * 0.5f, (high - low) * 0.5f};
    }

    bool SlamArrow::TryGetShownArrow(SlamArrowShape& outShape) const
    {
        if (!m_hasShown)
        {
            return false;
        }
        outShape = m_shown;
        return true;
    }

    NS_CLASS(SlamArrow)
} // namespace NS::Game::Level
