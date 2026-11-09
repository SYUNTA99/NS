#include "Game/Level/BodyShake.h"

#include "NSlib/CoreObject/ClassObjs/Model.h"
#include "NSlib/CoreObject/IGet/IGetCamera.h"
#include "NSlib/CoreObject/SceneActor.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace GL
{
    namespace Level
    {
        namespace
        {
            // 点の無い曲線は元の形の 1。0 を返すと描く形が消える
            [[nodiscard]] float ShapeFactor(const NS::Curve& curve, float frame) noexcept
            {
                if (curve.count == 0)
                {
                    return 1.0f;
                }
                return curve.Evaluate(frame);
            }
        } // namespace

        NS::Vector3 BodyShakeDesc::OffsetAt(int frame) const noexcept
        {
            return axis * BodyShakeOffset(*this, frame);
        }

        float BodyShakeReach(int frame, int length, float amplitude) noexcept
        {
            if (length <= 0 || frame < 1 || frame >= length)
            {
                return 0.0f;
            }
            return amplitude * (1.0f - static_cast<float>(frame) / static_cast<float>(length));
        }

        float BodyShakeOffset(const BodyShakeDesc& desc, int frame) noexcept
        {
            const float reach = BodyShakeReach(frame, desc.length, desc.amplitude);
            if (desc.length <= 0 || frame < 1 || frame >= desc.length)
            {
                return 0.0f;
            }
            // 振れ幅だけをばらつかせ、左右の入れ替わりは乱さない。乱すと止まって見えるフレームができる
            // 0〜1 の外は、包みを超えるか最初の向きが逆になるので収める
            const float amount = std::clamp(desc.amplitudeSpread, 0.0f, 1.0f);
            const float spread = (1.0f - amount) + amount * static_cast<float>(NS::NoiseHash(frame, desc.seed)) *
                                                       (1.0f / static_cast<float>(std::numeric_limits<NS::uint32>::max()));
            float sign = 1.0f;
            if (desc.firstSign < 0.0f)
            {
                sign = -1.0f;
            }
            // 1 フレームごとの入れ替えは 1 秒に 30 往復で、ちらつきに見える。数フレーム同じ側に留めると揺れと読める
            const int flip = std::max(desc.flipFrames, 1);
            if (((frame - 1) / flip) % 2 == 1)
            {
                sign = -sign;
            }
            // 直線で弱める。2 乗だと止めの半分で 4 分の 1 まで落ち、大きく振れるのが白と火花に隠れる最初だけになる
            return reach * spread * sign;
        }

        NS::Vector3 BodyShapeDesc::ScaleAt(int frame) const noexcept
        {
            const float x = static_cast<float>(frame);
            const float alongFactor = ShapeFactor(along, x);
            const float sideFactor = ShapeFactor(side, x);
            const float dx2 = alongDirection.x * alongDirection.x;
            const float dz2 = alongDirection.z * alongDirection.z;
            return NS::Vector3{1.0f + (alongFactor - 1.0f) * dx2 + (sideFactor - 1.0f) * dz2,
                               ShapeFactor(height, x),
                               1.0f + (alongFactor - 1.0f) * dz2 + (sideFactor - 1.0f) * dx2};
        }

        std::array<NS::Vector4, 4> BodyTremor::DrawValues() const noexcept
        {
            return {NS::Vector4{contactOffset.x, contactOffset.y, contactOffset.z, amplitude},
                    NS::Vector4{right.x, right.y, right.z, elapsedFrames},
                    NS::Vector4{up.x, up.y, up.z, framesPerMeter},
                    NS::Vector4{ringFrames, 0.0f, 0.0f, 0.0f}};
        }

        BodyTremor BodyTremor::FromDrawValues(const std::array<NS::Vector4, 4>& values) noexcept
        {
            return BodyTremor{.contactOffset = NS::Vector3{values[0].x, values[0].y, values[0].z},
                              .amplitude = values[0].w,
                              .right = NS::Vector3{values[1].x, values[1].y, values[1].z},
                              .elapsedFrames = values[1].w,
                              .up = NS::Vector3{values[2].x, values[2].y, values[2].z},
                              .framesPerMeter = values[2].w,
                              .ringFrames = values[3].x};
        }

        bool BodyTremor::IsFinite() const noexcept
        {
            return NS::IsFinite(contactOffset) && NS::IsFinite(right) && NS::IsFinite(up) && std::isfinite(amplitude) &&
                   std::isfinite(elapsedFrames) && std::isfinite(framesPerMeter) && std::isfinite(ringFrames);
        }

        BodyTremor MakeBodyTremor(const BodyTremorDesc& desc,
                                  int elapsedFrames,
                                  const NS::Vector3& root,
                                  const NS::CameraPose& pose) noexcept
        {
            BodyTremor tremor{};
            const int ringFrames = desc.length - std::max(desc.reachFrames, 0);
            if (elapsedFrames < 0 || elapsedFrames >= desc.length || ringFrames <= 0 ||
                !std::isfinite(desc.amplitudePixels))
            {
                return tremor;
            }
            NS::Vector3 forward = pose.target - pose.position;
            if (!NS::HasPositiveMagnitude(forward))
            {
                return tremor;
            }
            forward.Normalize();
            // 画面の右と上。真上か真下を見ている時は世界の X を右にする
            NS::Vector3 right = NS::Cross(NS::Vector3{0.0f, 1.0f, 0.0f}, forward);
            if (NS::HasPositiveMagnitude(right))
            {
                right.Normalize();
            }
            else
            {
                right = NS::Vector3{1.0f, 0.0f, 0.0f};
            }
            NS::Vector3 up = NS::Cross(forward, right);
            up.Normalize();

            const float bodyLength = desc.bodyLength;
            tremor.contactOffset = desc.contactOffset;
            // 割合は体に対する比を保つ。ピクセルから直すと遠ざかるほど体に対して大きくなる
            if (std::isfinite(desc.amplitudeRatio) && desc.amplitudeRatio > 0.0f)
            {
                tremor.amplitude = desc.amplitudeRatio * std::max(bodyLength, 0.0f) * 0.5f;
            }
            else
            {
                tremor.amplitude =
                    std::max(NS::ScreenPixelsToMeters(desc.amplitudePixels, pose, root, desc.referenceHeight), 0.0f);
            }
            tremor.right = right;
            tremor.up = up;
            tremor.elapsedFrames = static_cast<float>(elapsedFrames);
            if (bodyLength > 0.0f && desc.reachFrames > 0)
            {
                tremor.framesPerMeter = static_cast<float>(desc.reachFrames) / bodyLength;
            }
            tremor.ringFrames = static_cast<float>(ringFrames);
            return tremor;
        }

        bool BodyShakeRun::Advance(const NS::HitReactionSelf& owner)
        {
            if (owner.model == nullptr)
            {
                return false;
            }
            ++m_frame;
            // 有限でないずれは描く形を消すので書かず、前のずれのまま描く
            (void)owner.model->SetDrawOffset(m_desc.OffsetAt(m_frame));
            // 長さのフレームで 0 になる。長さの次のフレームで 0 を書いてから終わる
            return m_frame <= m_desc.length;
        }

        void BodyShakeRun::Stop(const NS::HitReactionSelf& owner) noexcept
        {
            if (owner.model != nullptr)
            {
                (void)owner.model->SetDrawOffset(NS::Vector3{0.0f, 0.0f, 0.0f});
            }
        }

        const void* BodyShakeRun::StaticKind() noexcept
        {
            static const char kind = 0;
            return &kind;
        }

        bool BodyShapeRun::Advance(const NS::HitReactionSelf& owner)
        {
            if (owner.model == nullptr)
            {
                return false;
            }
            ++m_frame;
            if (m_frame >= m_desc.length)
            {
                return false;
            }
            // 有限の正でない倍率は描く形を消すので書かず、前の倍率のまま描く
            (void)owner.model->SetDrawShapeScale(m_desc.ScaleAt(m_frame));
            return true;
        }

        void BodyShapeRun::Stop(const NS::HitReactionSelf& owner) noexcept
        {
            if (owner.model != nullptr)
            {
                (void)owner.model->SetDrawShapeScale(NS::Vector3{1.0f, 1.0f, 1.0f});
            }
        }

        const void* BodyShapeRun::StaticKind() noexcept
        {
            static const char kind = 0;
            return &kind;
        }

        bool BodyTremorRun::Advance(const NS::HitReactionSelf& owner)
        {
            if (owner.model == nullptr)
            {
                return false;
            }
            ++m_elapsed;
            BodyTremor tremor{};
            // 振れ幅はカメラとの距離で決まる
            const std::optional<NS::CameraPose> pose = NS::GetCameraViewPose(owner.actor);
            if (pose.has_value())
            {
                tremor = MakeBodyTremor(m_desc, m_elapsed, owner.actor->Root().Position(), *pose);
            }
            // 有限でない震えは描く形を消すので書かず、前の震えのまま描く
            if (tremor.IsFinite())
            {
                owner.model->SetDrawValues(tremor.DrawValues());
            }
            return m_elapsed < m_desc.length;
        }

        void BodyTremorRun::Scale(float scale) noexcept
        {
            m_desc.amplitudePixels *= scale;
            m_desc.amplitudeRatio *= scale;
        }

        void BodyTremorRun::Stop(const NS::HitReactionSelf& owner) noexcept
        {
            if (owner.model != nullptr)
            {
                owner.model->SetDrawValues({});
            }
        }

        const void* BodyTremorRun::StaticKind() noexcept
        {
            static const char kind = 0;
            return &kind;
        }
    } // namespace Level
} // namespace GL
