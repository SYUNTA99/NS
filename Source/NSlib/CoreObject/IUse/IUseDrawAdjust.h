#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/Gfx/FrameConstants.h"

namespace NS
{
    // 震えの定数の 16 バイト整列を値で抱えるため、想定どおりのパディングが入る。C4324 を黙らせる
#pragma warning(push)
#pragma warning(disable : 4324)
    //! @brief 本体を描く時だけの調整。根の Transform と当たりには効かず、保存しない
    struct DrawAdjust
    {
        Vector3 offset{0.0f, 0.0f, 0.0f}; //!< 描く姿勢へ足す世界のずれ (m)。補間しない
        //! 形の下端中央を中心に掛ける世界の軸の倍率。全成分が有限の正。Model が固定フレームの間を補間する
        Vector3 scale{1.0f, 1.0f, 1.0f};
        Gfx::TremorCB tremor{}; //!< 頂点シェーダーへ渡す震え。補間しない
    };
#pragma warning(pop)

    //! @brief 持ち主の Model が本体を描く時の調整を返すインターフェース
    //! @details 継いだアクターが自分の値を返す。継いでいないアクターの Model は調整なしで描く
    class IUseDrawAdjust
    {
    public:
        //! 今の固定フレームの調整。描くたびに呼ばれるので、計算を重ねず控えた値を返す
        [[nodiscard]] virtual DrawAdjust GetDrawAdjust() const noexcept = 0;

    protected:
        ~IUseDrawAdjust() = default;
    };
} // namespace NS
