#pragma once

/// @file Audio.h
/// @brief Audio 層の一括 include ヘッダ (仮実装)
///
/// @details 将来 DirectXTK::Audio + XAudio2 で BGM / SE 機能を実装予定

namespace NS::Audio
{
    /// @brief Audio 層が初期化されたことを示す仮のマーカ
    inline constexpr bool kPlaceholder = true;
} // namespace NS::Audio
