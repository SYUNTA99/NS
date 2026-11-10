#pragma once

#include "NSlib/Core/Math.h"

namespace NS
{
    //! 前と今の 2 つの値を、描く時の割合で混ぜる
    [[nodiscard]] inline Vector3 InterpolateValue(const Vector3& previous, const Vector3& current, float alpha) noexcept
    {
        return Vector3::Lerp(previous, current, alpha);
    }

    [[nodiscard]] inline Quaternion InterpolateValue(const Quaternion& previous,
                                                     const Quaternion& current,
                                                     float alpha) noexcept
    {
        return Quaternion::Slerp(previous, current, alpha);
    }

    //! 固定ステップの前の値と今の値の組。描く時に、固定ステップの間の割合で混ぜて読む
    //! 使う側は固定ステップの頭に Snapshot を呼び、その歩の中では Set で今の値だけを書く
    template <typename T>
    class Interpolated
    {
    public:
        Interpolated() noexcept = default;
        explicit Interpolated(const T& value) noexcept : m_previous(value), m_current(value) {}

        //! 今の値だけを書く。前の値から今の値へ補間して描かれる
        void Set(const T& value) noexcept { m_current = value; }
        //! 前の値も今の値も書く。補間せずに新しい値で描かれる (瞬間移動)
        void Teleport(const T& value) noexcept
        {
            m_previous = value;
            m_current = value;
        }
        //! 前の値を今の値にそろえる
        void Snapshot() noexcept { m_previous = m_current; }
        //! 前の値と今の値を同じだけずらす。ずらした分は補間に動きとして映らない
        void Shift(const T& delta) noexcept
        {
            m_previous += delta;
            m_current += delta;
        }

        [[nodiscard]] const T& Previous() const noexcept { return m_previous; }
        [[nodiscard]] const T& Current() const noexcept { return m_current; }

        //! 割合 alpha (0 で前、1 で今) で混ぜた値。1 以上なら今の値をそのまま返す
        //! 前 + (今 − 前) は丸めで今からずれるので、止めた世界を描く割合 1 の絵が前の値で変わらないようにする
        [[nodiscard]] T Lerp(float alpha) const noexcept
        {
            if (alpha >= 1.0f)
            {
                return m_current;
            }
            return InterpolateValue(m_previous, m_current, alpha);
        }

    private:
        T m_previous{};
        T m_current{};
    };
} // namespace NS
