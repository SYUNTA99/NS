#pragma once

// 仮実装の Audio 層一括 include ヘッダ
// 将来 DirectXTK::Audio + XAudio2 で BGM / SE 機能を実装予定

namespace NS::Audio
{
    /// @brief Audio 層が初期化されたことを示す仮のマーカ
    inline constexpr bool k_Placeholder = true;
} // namespace NS::Audio
