#pragma once

#include "NSlib/Core/NonCopyable.h"
#include "NSlib/Gfx/D3dCommon.h"

#include <optional>

namespace NS
{
    namespace Gfx
    {
        //! GPU の Begin〜End の時間を測る。読み取りは GPU を待つため計測用に限る
        //! D3DDevice が未構築なら無効。D3DDevice より先に破棄する
        class D3DGpuTimer : public NonCopyable
        {
        public:
            //! 時刻と、時刻の刻みが揃っているかの問い合わせを作る
            D3DGpuTimer() noexcept;
            ~D3DGpuTimer();

            //! 構築に成功した場合 true、それ以外の場合は false
            [[nodiscard]] bool IsValid() const noexcept;

            //! 測り始める。前の Begin から End まで測った物は読まずに捨てる
            void Begin() noexcept;

            //! Begin からの測りを終える。Begin の後でなければ何もしない
            void End() noexcept;

            //! 直近の Begin から End までに描画装置が掛けた時間を、描画装置を待って読む
            //! @return 読めた場合はミリ秒。End まで測っていない・刻みが途中で変わった・待ちきれなかった場合は空
            //! 1 回読むと測った物は無くなる。続けて呼ぶと 2 回目は空
            [[nodiscard]] std::optional<float> ReadMilliseconds() noexcept;

        private:
            ComPtr<ID3D11Query> m_disjoint; // 間で時刻の刻みが変わらなかったかと、1 秒あたりの刻み
            ComPtr<ID3D11Query> m_start;    // Begin の時刻
            ComPtr<ID3D11Query> m_end;      // End の時刻
            bool m_begun = false;           // Begin の後で End の前か
            bool m_ended = false;           // End まで測って、まだ読んでいないか
        };
    } // namespace Gfx
} // namespace NS
