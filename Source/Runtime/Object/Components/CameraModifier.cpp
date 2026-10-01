#include "Runtime/Object/Components/CameraModifier.h"

#include <cmath>
#include <random>

namespace NS::Obj
{
    namespace
    {
        // 滑らかに始まって終わる補間の重み。t が 0 で 0、1 で 1 ちょうど
        [[nodiscard]] float SmoothStep(float t) noexcept
        {
            return t * t * (3.0f - 2.0f * t);
        }

        [[nodiscard]] bool IsFiniteVector(const NS::Core::Vector3& v) noexcept
        {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        }

        // 向きが入れ替わるまでのフレーム数を 1〜longest から選ぶ。longest が 2 以上なら previous と同じ数を選ばない
        // 分布は実装ごとに結果が違うので通さず、生成器の出力の余りから選ぶ
        [[nodiscard]] int PickFlipFrames(std::mt19937& generator, int longest, int previous) noexcept
        {
            if (longest <= 1)
            {
                return 1;
            }
            if (previous < 1)
            {
                return 1 + static_cast<int>(generator() % static_cast<std::uint32_t>(longest));
            }
            // previous を除いた longest - 1 通りから選び、previous 以上は 1 つ後ろへずらす
            int picked = 1 + static_cast<int>(generator() % static_cast<std::uint32_t>(longest - 1));
            if (picked >= previous)
            {
                ++picked;
            }
            return picked;
        }

        // offsets の各フレームの axis の成分へ向きの符号を書く。最初は firstSign
        void FillFlipSigns(std::mt19937& generator,
                           int longest,
                           float firstSign,
                           float NS::Core::Vector2::* axis,
                           std::vector<NS::Core::Vector2>& offsets) noexcept
        {
            float sign = firstSign;
            int previous = 0;
            std::size_t frame = 0;
            while (frame < offsets.size())
            {
                const int run = PickFlipFrames(generator, longest, previous);
                for (int step = 0; step < run && frame < offsets.size(); ++step)
                {
                    offsets[frame].*axis = sign;
                    ++frame;
                }
                sign = -sign;
                previous = run;
            }
        }
    } // namespace

    CameraModifier::~CameraModifier() noexcept = default;

    void CameraModifier::Tick() noexcept
    {
        // 積んだ後の最初の Tick では進めない。積んだフレームに最初の振れと寄りを描く
        if (m_justAdded)
        {
            m_justAdded = false;
            return;
        }
        if (!IsFinished())
        {
            Advance();
        }
    }

    std::unique_ptr<CameraShakeModifier> CameraShakeModifier::Create(const CameraShakeDesc& desc, float firstSideSign)
    {
        // 壊れた値が pose へ流れると視点が消える。入口で捨てる
        const bool finite = std::isfinite(desc.sideAmplitude) && std::isfinite(desc.upAmplitude) &&
                            IsFiniteVector(desc.firstSideDirection);
        if (!finite || desc.sideAmplitude < 0.0f || desc.upAmplitude < 0.0f || desc.frames <= 0 ||
            desc.longestFlipFrames < 1)
        {
            return nullptr;
        }

        std::unique_ptr<CameraShakeModifier> shake{new CameraShakeModifier()};
        // seed_seq と mt19937 は手順が規格で決まっているので、同じ種なら実装によらず同じ並びになる
        std::seed_seq seeds{desc.seed};
        std::mt19937 generator(seeds);
        shake->m_offsets.assign(static_cast<std::size_t>(desc.frames), NS::Core::Vector2{0.0f, 0.0f});
        float sideSign = 1.0f;
        if (firstSideSign < 0.0f)
        {
            sideSign = -1.0f;
        }
        FillFlipSigns(generator, desc.longestFlipFrames, sideSign, &NS::Core::Vector2::x, shake->m_offsets);
        FillFlipSigns(generator, desc.longestFlipFrames, -1.0f, &NS::Core::Vector2::y, shake->m_offsets);

        // 始めたフレームが最大で、残りのフレーム数に比例して減る
        for (int frame = 0; frame < desc.frames; ++frame)
        {
            const float decay = static_cast<float>(desc.frames - frame) / static_cast<float>(desc.frames);
            NS::Core::Vector2& offset = shake->m_offsets[static_cast<std::size_t>(frame)];
            offset.x *= desc.sideAmplitude * decay;
            offset.y *= desc.upAmplitude * decay;
        }
        return shake;
    }

    const void* CameraShakeModifier::StaticKind() noexcept
    {
        static const char kind = 0;
        return &kind;
    }

    NS::Core::Vector2 CameraShakeModifier::Offset() const noexcept
    {
        if (m_frame < static_cast<int>(m_offsets.size()))
        {
            return m_offsets[static_cast<std::size_t>(m_frame)];
        }
        return NS::Core::Vector2{0.0f, 0.0f};
    }

    void CameraShakeModifier::Modify(CameraPose& pose, const CameraAxes& axes) const noexcept
    {
        const NS::Core::Vector2 shake = Offset();
        if (shake.x == 0.0f && shake.y == 0.0f)
        {
            return;
        }
        const NS::Core::Vector3 offset = axes.right * shake.x + axes.up * shake.y;
        pose.position += offset;
        pose.target += offset;
    }

    bool CameraShakeModifier::IsFinished() const noexcept
    {
        return m_frame >= static_cast<int>(m_offsets.size());
    }

    void CameraShakeModifier::Advance() noexcept
    {
        ++m_frame;
    }

    std::unique_ptr<CameraZoomRollModifier> CameraZoomRollModifier::Create(const CameraZoomRollDesc& desc,
                                                                           float rollSign)
    {
        const bool finite =
            std::isfinite(desc.zoom) && std::isfinite(desc.rollDegrees) && IsFiniteVector(desc.rollDirection);
        if (!finite || desc.zoom < 1.0f || desc.holdFrames < 0 || desc.returnFrames < 0)
        {
            return nullptr;
        }

        float rollDirection = 1.0f;
        if (rollSign < 0.0f)
        {
            rollDirection = -1.0f;
        }
        std::unique_ptr<CameraZoomRollModifier> zoomRoll{new CameraZoomRollModifier()};
        zoomRoll->m_full = CameraZoomRoll{
            .zoom = desc.zoom,
            .rollDegrees = desc.rollDegrees * rollDirection,
        };
        zoomRoll->m_holdFrames = desc.holdFrames;
        zoomRoll->m_returnFrames = desc.returnFrames;
        return zoomRoll;
    }

    const void* CameraZoomRollModifier::StaticKind() noexcept
    {
        static const char kind = 0;
        return &kind;
    }

    CameraZoomRoll CameraZoomRollModifier::Current() const noexcept
    {
        if (m_frame >= m_holdFrames + m_returnFrames)
        {
            return CameraZoomRoll{};
        }
        if (m_frame < m_holdFrames)
        {
            return m_full;
        }

        // 重みを 1 - weight と weight に分けて掛けるので、戻しの最後のフレームで倍率 1・傾き 0 ちょうどになる
        const float t = static_cast<float>(m_frame - m_holdFrames + 1) / static_cast<float>(m_returnFrames);
        const float weight = SmoothStep(t);
        return CameraZoomRoll{
            .zoom = m_full.zoom * (1.0f - weight) + weight,
            .rollDegrees = m_full.rollDegrees * (1.0f - weight),
        };
    }

    void CameraZoomRollModifier::Modify(CameraPose& pose, const CameraAxes& axes) const noexcept
    {
        const CameraZoomRoll zoomRoll = Current();
        if (zoomRoll.zoom != 1.0f)
        {
            pose.fovY = NS::Core::Radians{2.0f * std::atan(std::tan(pose.fovY.value * 0.5f) / zoomRoll.zoom)};
        }
        if (zoomRoll.rollDegrees != 0.0f)
        {
            const float roll = NS::Core::DegreesToRadians(zoomRoll.rollDegrees);
            pose.up = axes.up * std::cos(roll) + axes.right * std::sin(roll);
        }
    }

    bool CameraZoomRollModifier::IsFinished() const noexcept
    {
        return m_frame >= m_holdFrames + m_returnFrames;
    }

    void CameraZoomRollModifier::Advance() noexcept
    {
        ++m_frame;
    }
} // namespace NS::Obj
