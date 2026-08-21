#pragma once

#include "Runtime/Core/Math.h"

#include <cstddef>
#include <cstdint>

namespace NS::Object
{
    //! @brief 点列を折れ線で結ぶカーブの値型。点は最大 8 個で x 昇順に持つ
    //! @details 固定長なので値の写しにメモリ確保が要らず、FieldDesc の get / set が写しだけで完結する。
    //! 既定との比較も既定の比較演算子で書ける。手で形を決める用途なので 8 点で足りる
    struct Curve
    {
        static constexpr std::size_t k_MaxKeys = 8; // 保持できる点の上限

        //! カーブ上の 1 点。x が入力、y が返る値
        struct Key
        {
            float x = 0.0f;
            float y = 0.0f;

            [[nodiscard]] constexpr bool operator==(const Key&) const noexcept = default;
        };

        Key keys[k_MaxKeys]{};   // 点の並び。有効なのは先頭 count 個
        std::uint32_t count = 0; // 有効な点の数

        //! x に対応する y を返す。点が無ければ 0、範囲の外は端の点の y、点の間は線形補間
        //! 事前条件: keys の先頭 count 個が x 昇順。同じ x が並ぶ区間は後の点の y になる
        [[nodiscard]] float Evaluate(float x) const noexcept;

        //! keys の先頭 count 個を x 昇順に並べ直す。同じ x は元の並びを保つ
        void SortKeys() noexcept;

        [[nodiscard]] constexpr bool operator==(const Curve&) const noexcept = default;
    };

    inline float Curve::Evaluate(float x) const noexcept
    {
        // 既定の形は使う側が持つ。点が無い時は 0 を返すだけにする
        if (count == 0)
        {
            return 0.0f;
        }
        // 外挿すると手で置いた形の外で値が暴れるため、範囲の外は端の値で止める
        if (x <= keys[0].x)
        {
            return keys[0].y;
        }
        const std::uint32_t last = count - 1;
        if (x >= keys[last].x)
        {
            return keys[last].y;
        }
        for (std::uint32_t i = 0; i < last; ++i)
        {
            if (x <= keys[i + 1].x)
            {
                const float width = keys[i + 1].x - keys[i].x;
                // 同じ x が並ぶと 0 除算になるため、後の点の値を返す
                if (width <= 0.0f)
                {
                    return keys[i + 1].y;
                }
                const float t = (x - keys[i].x) / width;
                return NS::Core::Lerp(keys[i].y, keys[i + 1].y, t);
            }
        }
        return keys[last].y;
    }

    inline void Curve::SortKeys() noexcept
    {
        // 最大 8 個なので挿入で足りる。同じ x は元の並びを保つ
        for (std::uint32_t i = 1; i < count; ++i)
        {
            const Key inserted = keys[i];
            std::uint32_t j = i;
            while (j > 0 && inserted.x < keys[j - 1].x)
            {
                keys[j] = keys[j - 1];
                --j;
            }
            keys[j] = inserted;
        }
    }
} // namespace NS::Object
