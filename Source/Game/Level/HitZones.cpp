#include "Game/Level/HitZones.h"

#include "Runtime/Core/OBB.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace NS::Game::Level
{
    namespace
    {
        // 非数と無限は割合として意味を持たないので 0。それ以外は lower〜upper へ丸める
        [[nodiscard]] float SanitizeShare(float value, float lower, float upper) noexcept
        {
            if (!std::isfinite(value))
            {
                return 0.0f;
            }
            return std::clamp(value, lower, upper);
        }

        // 威力の倍率。負と非数と無限は 0
        [[nodiscard]] float SanitizeScale(float value) noexcept
        {
            if (!std::isfinite(value))
            {
                return 0.0f;
            }
            return std::max(value, 0.0f);
        }

        // 部品の欄を通らずに渡された写しも、部品と同じ範囲で読む
        [[nodiscard]] HitFace SanitizeFace(const HitFace& face) noexcept
        {
            HitFace result;
            result.round = face.round;
            result.width = SanitizeShare(face.width, 0.0f, 1.0f);
            result.height = SanitizeShare(face.height, 0.0f, 1.0f);
            result.centerU = SanitizeShare(face.centerU, -1.0f, 1.0f);
            result.centerV = SanitizeShare(face.centerV, -1.0f, 1.0f);
            result.powerScale = SanitizeScale(face.powerScale);
            result.remainderPowerScale = SanitizeScale(face.remainderPowerScale);
            return result;
        }

        [[nodiscard]] bool IsFinite(const NS::Core::Vector3& value) noexcept
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        // 面の大きさと触れる点を出すための体の測り。core は表面までの距離を測る起点、surfaceRadius は core から表面まで
        struct BodyMeasure
        {
            NS::Obj::HitSensorShape shape = NS::Obj::HitSensorShape::Sphere;
            NS::Core::Vector3 center;
            float halfWidth = 0.0f;
            float halfHeight = 0.0f;
            NS::Core::Vector3 core;
            float surfaceRadius = 0.0f;
        };

        // 体の形を、線に直交する水平の軸 (rightX, 0, rightZ) と真上へ写す。球・カプセル・箱のどれでもなければ false
        // 線分と半径の形は、半径が有限の正なら球かカプセル。両端が重なれば球
        [[nodiscard]] bool MeasureBody(const NS::Obj::SensorVolume& body,
                                       float rightX,
                                       float rightZ,
                                       BodyMeasure& out) noexcept
        {
            if (body.isBox)
            {
                const NS::Core::OBB& box = body.box;
                const std::array<NS::Core::Vector3, 3> axes{box.axisX, box.axisY, box.axisZ};
                const std::array<float, 3> halves{box.halfExtentX, box.halfExtentY, box.halfExtentZ};
                if (!IsFinite(box.center))
                {
                    return false;
                }
                out.shape = NS::Obj::HitSensorShape::Box;
                out.center = box.center;
                out.halfWidth = 0.0f;
                out.halfHeight = 0.0f;
                float surfaceSq = 0.0f;
                for (std::size_t i = 0; i < axes.size(); ++i)
                {
                    if (!IsFinite(axes[i]) || !std::isfinite(halves[i]) || halves[i] < 0.0f)
                    {
                        return false;
                    }
                    out.halfWidth += std::abs(axes[i].x * rightX + axes[i].z * rightZ) * halves[i];
                    out.halfHeight += std::abs(axes[i].y) * halves[i];
                    surfaceSq += halves[i] * halves[i];
                }
                out.core = box.center;
                // TODO: 箱は外接球の上の点で、面の中ほどでは表面から浮く。箱の相手を置く時に箱の表面へ寄せる
                out.surfaceRadius = std::sqrt(surfaceSq);
                return true;
            }
            if (!IsFinite(body.a) || !IsFinite(body.b) || !std::isfinite(body.radius) || !(body.radius > 0.0f))
            {
                return false;
            }
            const NS::Core::Vector3 half = (body.b - body.a) * 0.5f;
            out.center = (body.a + body.b) * 0.5f;
            out.halfWidth = body.radius + std::abs(half.x * rightX + half.z * rightZ);
            out.halfHeight = body.radius + std::abs(half.y);
            out.core = out.center;
            out.surfaceRadius = body.radius;
            out.shape = NS::Obj::HitSensorShape::Sphere;
            if (half.LengthSquared() > NS::Core::k_Epsilon * NS::Core::k_Epsilon)
            {
                out.shape = NS::Obj::HitSensorShape::Capsule;
            }
            return true;
        }

        // 自機の玉が相手の表面に触れる向き。core から見た単位ベクトル
        // 玉の中心が core から touchRadius の球に入った所で触れる
        // 直線がその球に入る点の向きで測り、origin がもう球の中でも入った点へ戻る
        // 予測は遠くから、当たりは触れてから呼ぶので、今の位置で測ると両者が割れる
        // 直線が届かない時と、球が origin の後ろにある時は、直線に一番近い点の向き
        [[nodiscard]] NS::Core::Vector3 FindTouchDirection(const NS::Core::Vector3& core,
                                                           float touchRadius,
                                                           const NS::Core::Vector3& origin,
                                                           const NS::Core::Vector3& direction) noexcept
        {
            const NS::Core::Vector3 fromCore = origin - core;
            const float along = fromCore.Dot(direction);
            const float outside = fromCore.Dot(fromCore) - touchRadius * touchRadius;
            const float discriminant = along * along - outside;
            NS::Core::Vector3 point = origin + direction * -along;
            if (outside <= 0.0f || (discriminant >= 0.0f && along < 0.0f))
            {
                point = origin + direction * (-along - std::sqrt(std::max(discriminant, 0.0f)));
            }
            NS::Core::Vector3 touch = point - core;
            // 玉の中心が core に重なると向きが無い。来た側から触れたことにする
            if (touch.LengthSquared() < NS::Core::k_Epsilon * NS::Core::k_Epsilon)
            {
                touch = -direction;
            }
            touch.Normalize();
            return touch;
        }

        // 決まりが読む物。条件の種類を足す時は、JudgeHitFace の入力から欄を足す
        struct TierRuleInput
        {
            const HitFace& face;
            float u = 0.0f;
            float v = 0.0f;
        };

        // 赤の中か。丸は横と縦の幅を半径にした楕円、箱は矩形。縁ちょうどは外
        [[nodiscard]] bool IsInsideRed(const TierRuleInput& input) noexcept
        {
            // 幅 0 で割らないよう先に外す。幅 0 の向きがある赤はどこも覆わない
            if (!(input.face.width > 0.0f) || !(input.face.height > 0.0f))
            {
                return false;
            }
            const float x = (input.u - input.face.centerU) / input.face.width;
            const float y = (input.v - input.face.centerV) / input.face.height;
            if (input.face.round)
            {
                return x * x + y * y < 1.0f;
            }
            return std::abs(x) < 1.0f && std::abs(y) < 1.0f;
        }

        [[nodiscard]] float RedPowerScale(const HitFace& face) noexcept
        {
            return face.powerScale;
        }

        // 段の決まり 1 つ。当てはまる条件、その時の段、威力の倍率
        struct TierRule
        {
            bool (*matches)(const TierRuleInput& input) noexcept;
            HitTier tier;
            float (*powerScale)(const HitFace& face) noexcept;
        };

        // 段の決まりの並び。優先の高い順で、上から見て最初に当てはまった決まりで段と威力の倍率を決める
        // 段を足す時はここへ決まりを 1 つ足す。どれにも当てはまらない所は外れと残りの威力の倍率
        constexpr std::array<TierRule, 1> k_TierRules{{
            {&IsInsideRed, HitTier::Center, &RedPowerScale},
        }};
    } // namespace

    bool JudgeHitFace(const HitFace& face,
                      const NS::Obj::SensorVolume& body,
                      const NS::Core::Vector3& origin,
                      const NS::Core::Vector3& direction,
                      float playerRadius,
                      HitFaceJudgement& out) noexcept
    {
        if (!IsFinite(origin) || !std::isfinite(playerRadius))
        {
            return false;
        }
        const float horizontal = std::sqrt(direction.x * direction.x + direction.z * direction.z);
        if (!std::isfinite(horizontal) || !(horizontal > NS::Core::k_Epsilon))
        {
            return false;
        }
        const float dirX = direction.x / horizontal;
        const float dirZ = direction.z / horizontal;
        // 線に直交する水平の軸。左手系で上から見て、進む向きの右
        const float rightX = dirZ;
        const float rightZ = -dirX;

        BodyMeasure measure;
        if (!MeasureBody(body, rightX, rightZ, measure))
        {
            return false;
        }
        const NS::Core::Vector3& center = measure.center;
        const float toX = center.x - origin.x;
        const float toZ = center.z - origin.z;
        const float along = toX * dirX + toZ * dirZ;
        // 線が相手の中心から右へずれている長さ
        const float right = -(toX * rightX + toZ * rightZ);
        const float reach = measure.halfWidth + playerRadius;
        const float reachUp = measure.halfHeight + playerRadius;
        // 半幅と半径の和が 0 以下では割れない。真ん中扱いへ倒す
        float u = 0.0f;
        if (reach > 0.0f)
        {
            u = right / reach;
        }
        float v = 0.0f;
        if (reachUp > 0.0f)
        {
            v = (origin.y - center.y) / reachUp;
        }

        const HitFace sanitized = SanitizeFace(face);
        const TierRuleInput input{sanitized, u, v};
        out.tier = HitTier::Wide;
        out.powerScale = sanitized.remainderPowerScale;
        for (const TierRule& rule : k_TierRules)
        {
            if (rule.matches(input))
            {
                out.tier = rule.tier;
                out.powerScale = rule.powerScale(sanitized);
                break;
            }
        }
        out.u = u;
        out.v = v;
        out.ratio = std::abs(u);
        out.offset01 = NS::Core::Clamp(out.ratio, 0.0f, 1.0f);
        out.along = along;
        out.linePoint = NS::Core::Vector3{origin.x + dirX * along, center.y, origin.z + dirZ * along};
        out.bodyShape = measure.shape;

        // カプセルは、線の通った点を線の高さに置いた点に一番近い筒の軸の上の点から表面までを測る
        NS::Core::Vector3 core = measure.core;
        if (measure.shape == NS::Obj::HitSensorShape::Capsule)
        {
            const NS::Core::Vector3 axis = body.b - body.a;
            const NS::Core::Vector3 nearest{out.linePoint.x, origin.y, out.linePoint.z};
            const float t = std::clamp((nearest - body.a).Dot(axis) / axis.Dot(axis), 0.0f, 1.0f);
            core = body.a + axis * t;
        }
        const NS::Core::Vector3 lineDirection{dirX, 0.0f, dirZ};
        out.surfacePoint =
            core + FindTouchDirection(core, measure.surfaceRadius + playerRadius, origin, lineDirection) *
                       measure.surfaceRadius;
        return true;
    }

    void HitZones::SetRound(bool round) noexcept
    {
        m_face.round = round;
    }

    void HitZones::SetWidth(float width) noexcept
    {
        m_face.width = SanitizeShare(width, 0.0f, 1.0f);
    }

    void HitZones::SetHeight(float height) noexcept
    {
        m_face.height = SanitizeShare(height, 0.0f, 1.0f);
    }

    void HitZones::SetCenterU(float u) noexcept
    {
        m_face.centerU = SanitizeShare(u, -1.0f, 1.0f);
    }

    void HitZones::SetCenterV(float v) noexcept
    {
        m_face.centerV = SanitizeShare(v, -1.0f, 1.0f);
    }

    void HitZones::SetPowerScale(float scale) noexcept
    {
        m_face.powerScale = SanitizeScale(scale);
    }

    void HitZones::SetRemainderPowerScale(float scale) noexcept
    {
        m_face.remainderPowerScale = SanitizeScale(scale);
    }

    NS_CLASS(HitZones)
} // namespace NS::Game::Level
