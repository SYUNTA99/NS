#pragma once

#include "Runtime/Core/Math.h"

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace NS::Object
{
    //! @brief 点列を結ぶカーブの値型。点は最大 8 個で x 昇順に持ち、区間の形は両端の点の補間モードで決まる
    //! @details 固定長なので値の写しにメモリ確保が要らず、FieldDesc の get / set が写しだけで完結する。
    //! 既定との比較も既定の比較演算子で書ける。手で形を決める用途なので 8 点で足りる
    struct Curve
    {
        static constexpr std::size_t k_MaxKeys = 8; // 保持できる点の上限

        //! 点ごとの補間モード。番号は保存する点のモード番号にそのまま書くので並びを変えない
        enum class InterpMode : std::uint8_t
        {
            Linear = 0,
            AutoSmooth = 1,
            Manual = 2,
        };

        //! カーブ上の 1 点。x が入力、y が返る値
        struct Key
        {
            float x = 0.0f;
            float y = 0.0f;
            // 既定は Linear。既存データの評価結果を 1 ビットも変えないため
            InterpMode mode = InterpMode::Linear;
            // 接線は dy/dx の傾きで持つ。t 基準の値にすると区間の幅が変わるたびに意味が変わるため
            float inTangent = 0.0f;
            float outTangent = 0.0f;

            [[nodiscard]] constexpr bool operator==(const Key&) const noexcept = default;
        };

        Key keys[k_MaxKeys]{};   // 点の並び。有効なのは先頭 count 個
        std::uint32_t count = 0; // 有効な点の数

        //! x に対応する y を返す。点が無ければ 0、範囲の外は端の点の
        //! y、点の間は両端のモードに応じて線形かエルミートで補間
        //! 事前条件: keys の先頭 count 個が x 昇順。同じ x が並ぶ区間は後の点の y になる
        [[nodiscard]] float Evaluate(float x) const noexcept;

        //! keys の先頭 count 個を x 昇順に並べ直す。同じ x は元の並びを保つ
        void SortKeys() noexcept;

        //! i 番目と i + 1 番目の点の間を x でエルミート補間した y を返す。width は区間の x の幅
        //! 事前条件: i + 1 < count と width > 0。守らないと配列の外を読み 0 除算する
        [[nodiscard]] float EvaluateHermite(std::uint32_t i, float x, float width) const noexcept;

        //! AutoSmooth の点の接線 dy/dx を返す。隣り合う点の y の間から飛び出さない傾きへ抑える
        [[nodiscard]] float AutoTangentAt(std::uint32_t i) const noexcept;

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
                // 両端が直線の区間は従来の線形補間の経路を通す
                // エルミートへ寄せると丸めが 1 ビット変わり、既存データの評価結果と基準比較が崩れる
                if (keys[i].mode == InterpMode::Linear && keys[i + 1].mode == InterpMode::Linear)
                {
                    const float t = (x - keys[i].x) / width;
                    return NS::Core::Lerp(keys[i].y, keys[i + 1].y, t);
                }
                return EvaluateHermite(i, x, width);
            }
        }
        return keys[last].y;
    }

    inline float Curve::EvaluateHermite(std::uint32_t i, float x, float width) const noexcept
    {
        const float slope = (keys[i + 1].y - keys[i].y) / width;
        float tangentOut = slope;
        switch (keys[i].mode)
        {
        case InterpMode::Linear:
            tangentOut = slope;
            break;
        case InterpMode::AutoSmooth:
            tangentOut = AutoTangentAt(i);
            break;
        case InterpMode::Manual:
            tangentOut = keys[i].outTangent;
            break;
        }
        float tangentIn = slope;
        switch (keys[i + 1].mode)
        {
        case InterpMode::Linear:
            tangentIn = slope;
            break;
        case InterpMode::AutoSmooth:
            tangentIn = AutoTangentAt(i + 1);
            break;
        case InterpMode::Manual:
            tangentIn = keys[i + 1].inTangent;
            break;
        }
        const float t = (x - keys[i].x) / width;
        const float t2 = t * t;
        const float t3 = t2 * t;
        const float weightY0 = 2.0f * t3 - 3.0f * t2 + 1.0f;
        const float weightOut = t3 - 2.0f * t2 + t;
        const float weightY1 = -2.0f * t3 + 3.0f * t2;
        const float weightIn = t3 - t2;
        // 接線は dy/dx の傾きなので、t の空間へ持ち込むために区間の幅を掛ける
        return weightY0 * keys[i].y + weightOut * width * tangentOut + weightY1 * keys[i + 1].y +
               weightIn * width * tangentIn;
    }

    inline float Curve::AutoTangentAt(std::uint32_t i) const noexcept
    {
        // 幅 0 の隣は傾きを作れないので隣が無い扱いにする
        bool hasPrev = false;
        float slopePrev = 0.0f;
        if (i > 0)
        {
            const float widthPrev = keys[i].x - keys[i - 1].x;
            if (widthPrev > 0.0f)
            {
                hasPrev = true;
                slopePrev = (keys[i].y - keys[i - 1].y) / widthPrev;
            }
        }
        bool hasNext = false;
        float slopeNext = 0.0f;
        if (i + 1 < count)
        {
            const float widthNext = keys[i + 1].x - keys[i].x;
            if (widthNext > 0.0f)
            {
                hasNext = true;
                slopeNext = (keys[i + 1].y - keys[i].y) / widthNext;
            }
        }
        if (!hasPrev && !hasNext)
        {
            return 0.0f;
        }
        if (!hasPrev)
        {
            return slopeNext;
        }
        if (!hasNext)
        {
            return slopePrev;
        }
        // 山と谷の点は接線 0 で平らに通す。傾きを残すと点の y を越えて膨らむため
        if (slopePrev * slopeNext <= 0.0f)
        {
            return 0.0f;
        }
        // 割線の平均のままだと、急な区間の隣の平らな区間で両端の y の間から飛び出す
        // 両端の接線が割線の 0〜3 倍なら補間は両端の y の間に留まるので、その範囲へ抑える
        const float magnitudePrev = std::fabs(slopePrev);
        const float magnitudeNext = std::fabs(slopeNext);
        float limit = 3.0f * magnitudePrev;
        if (magnitudeNext < magnitudePrev)
        {
            limit = 3.0f * magnitudeNext;
        }
        return NS::Core::Clamp((slopePrev + slopeNext) * 0.5f, -limit, limit);
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
