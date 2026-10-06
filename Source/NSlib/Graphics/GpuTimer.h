#pragma once

#include "NSlib/Core/NonCopyable.h"
#include "NSlib/Graphics/D3dCommon.h"

#include <optional>

namespace NS::Gfx
{

    //! @brief Begin と End の間に積んだ描画に、描画装置が掛けた時間を測る
    //! @details D3D11 の時刻の問い合わせで、Begin と End の位置で描画装置が読んだ時刻の差を返す。
    //! 読む時は描画装置が End まで進むのを待つので、読んだ後のフレームは遅れる。測る道具の中だけで使う
    //! 構築時に Gpu() の device を使う。Renderer が未構築なら無効な状態になり、何もしない
    //! @warning Renderer より先に破棄すること
    class GpuTimer : public NS::NonCopyable
    {
    public:
        //! 時刻と、時刻の刻みが揃っているかの問い合わせを作る
        GpuTimer() noexcept;
        ~GpuTimer();

        //! 構築に成功した場合 true、それ以外の場合は false
        [[nodiscard]] bool IsValid() const noexcept;

        //! 測り始める。前の Begin から End まで測った物は読まずに捨てる
        void Begin() noexcept;

        //! Begin からの測りを終える。Begin の後でなければ何もしない
        void End() noexcept;

        //! @brief 直近の Begin から End までに描画装置が掛けた時間を、描画装置を待って読む
        //! @return 読めた場合はミリ秒。End まで測っていない・刻みが途中で変わった・待ちきれなかった場合は空
        //! @details 1 回読むと測った物は無くなる。続けて呼ぶと 2 回目は空
        [[nodiscard]] std::optional<float> ReadMilliseconds() noexcept;

    private:
        ComPtr<ID3D11Query> m_disjoint; // 間で時刻の刻みが変わらなかったかと、1 秒あたりの刻み
        ComPtr<ID3D11Query> m_start;    // Begin の時刻
        ComPtr<ID3D11Query> m_end;      // End の時刻
        bool m_begun = false;           // Begin の後で End の前か
        bool m_ended = false;           // End まで測って、まだ読んでいないか
    };
} // namespace NS::Gfx
