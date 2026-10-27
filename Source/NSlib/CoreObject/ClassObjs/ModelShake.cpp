#include "NSlib/CoreObject/ClassObjs/ModelShake.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace NS
{
    Vector3 DrawShakeDesc::OffsetAt(int frame) const noexcept
    {
        if (shape == DrawShakeShape::Depth)
        {
            return axis * DepthShakeOffset(frame, length, amplitude, pushFrames, returnRatio, seed);
        }
        return axis * BodyShakeOffset(frame, length, amplitude, seed, firstSign, flipFrames);
    }

    namespace
    {
        // 点の無い曲線は元の形の 1。0 を返すと描く形が消える
        [[nodiscard]] float ShapeFactor(const Curve& curve, float frame) noexcept
        {
            if (curve.count == 0)
            {
                return 1.0f;
            }
            return curve.Evaluate(frame);
        }
    } // namespace

    Vector3 DrawShapeDesc::ScaleAt(int frame) const noexcept
    {
        const float x = static_cast<float>(frame);
        const float alongFactor = ShapeFactor(along, x);
        const float sideFactor = ShapeFactor(side, x);
        const float dx2 = alongDirection.x * alongDirection.x;
        const float dz2 = alongDirection.z * alongDirection.z;
        return Vector3{1.0f + (alongFactor - 1.0f) * dx2 + (sideFactor - 1.0f) * dz2,
                       ShapeFactor(height, x),
                       1.0f + (alongFactor - 1.0f) * dz2 + (sideFactor - 1.0f) * dx2};
    }

    float BodyShakeReach(int frame, int length, float amplitude) noexcept
    {
        if (length <= 0 || frame < 1 || frame >= length)
        {
            return 0.0f;
        }
        return amplitude * (1.0f - static_cast<float>(frame) / static_cast<float>(length));
    }

    float BodyShakeOffset(int frame, int length, float amplitude, uint32 seed, float firstSign, int flipFrames) noexcept
    {
        if (length <= 0 || frame < 1 || frame >= length)
        {
            return 0.0f;
        }
        const float left = 1.0f - static_cast<float>(frame) / static_cast<float>(length);
        // 振れ幅だけをばらつかせ、左右の入れ替わりは乱さない。乱すと止まって見えるフレームができる
        const float spread = 0.7f + 0.3f * static_cast<float>(NoiseHash(frame, seed)) *
                                        (1.0f / static_cast<float>(std::numeric_limits<uint32>::max()));
        float sign = 1.0f;
        if (firstSign < 0.0f)
        {
            sign = -1.0f;
        }
        // 1 フレームごとの入れ替えは 1 秒に 30 往復で、ちらつきに見える。数フレーム同じ側に留めると揺れと読める
        const int flip = std::max(flipFrames, 1);
        if (((frame - 1) / flip) % 2 == 1)
        {
            sign = -sign;
        }
        // 直線で弱める。2 乗だと止めの半分で 4 分の 1 まで落ち、大きく振れるのが白と火花に隠れる最初だけになる
        return amplitude * left * spread * sign;
    }

    float DepthShakeOffset(int frame, int length, float depth, int pushFrames, float returnRatio, uint32 seed) noexcept
    {
        if (length <= 0 || frame < 1 || frame > length)
        {
            return 0.0f;
        }
        const int push = std::max(pushFrames, 1);
        const int index = (frame - 1) / push;
        const int since = (frame - 1) % push;
        const float left = 1.0f - static_cast<float>(index * push) / static_cast<float>(length);
        // 1 つの押しの間はばらつきを変えない。変えると戻る途中で押し直したように跳ねる
        const float spread = 0.75f + 0.25f * static_cast<float>(NoiseHash(index, seed)) *
                                         (1.0f / static_cast<float>(std::numeric_limits<uint32>::max()));
        return depth * left * spread * std::pow(returnRatio, static_cast<float>(since));
    }

    float ScreenPixelsToMeters(float pixels, const CameraPose& pose, const Vector3& at, float referenceHeight) noexcept
    {
        if (!std::isfinite(referenceHeight) || referenceHeight <= 0.0f)
        {
            return 0.0f;
        }
        Vector3 forward = pose.target - pose.position;
        if (!HasPositiveMagnitude(forward))
        {
            return 0.0f;
        }
        forward.Normalize();
        float depth = Dot(at - pose.position, forward);
        if (!IsPositive(depth))
        {
            depth = (at - pose.position).Length();
        }
        return pixels * 2.0f * depth * std::tan(pose.fovY.value * 0.5f) / referenceHeight;
    }

    Gfx::TremorCB
        MakeTremor(const DrawTremorDesc& desc, int elapsedFrames, const Vector3& root, const CameraPose& pose) noexcept
    {
        Gfx::TremorCB tremor{};
        const int ringFrames = desc.length - std::max(desc.reachFrames, 0);
        if (elapsedFrames < 0 || elapsedFrames >= desc.length || ringFrames <= 0 ||
            !std::isfinite(desc.amplitudePixels))
        {
            return tremor;
        }
        Vector3 forward = pose.target - pose.position;
        if (!HasPositiveMagnitude(forward))
        {
            return tremor;
        }
        forward.Normalize();
        // 画面の右と上。真上か真下を見ている時は世界の X を右にする
        Vector3 right = Cross(Vector3{0.0f, 1.0f, 0.0f}, forward);
        if (HasPositiveMagnitude(right))
        {
            right.Normalize();
        }
        else
        {
            right = Vector3{1.0f, 0.0f, 0.0f};
        }
        Vector3 up = Cross(forward, right);
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
                std::max(ScreenPixelsToMeters(desc.amplitudePixels, pose, root, desc.referenceHeight), 0.0f);
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

    bool IsFiniteTremor(const Gfx::TremorCB& tremor) noexcept
    {
        return IsFinite(tremor.contactOffset) && IsFinite(tremor.right) && IsFinite(tremor.up) &&
               std::isfinite(tremor.amplitude) && std::isfinite(tremor.elapsedFrames) &&
               std::isfinite(tremor.framesPerMeter) && std::isfinite(tremor.ringFrames);
    }
} // namespace NS
