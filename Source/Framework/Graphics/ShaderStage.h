#pragma once

/// @file ShaderStage.h
/// @brief NS::Graphics::ShaderStage — リソースの bind 対象ステージを表す bitflag
///
/// @details `CommandList` / `Renderer` の Texture / TextureArray / ConstantBuffer bind 先と
/// `Material` の CB bind 先指定に使う。 描画パイプラインの対象は VS / PS のみ

namespace NS::Graphics
{

    /// 定数バッファ / SRV の bind 対象ステージ bitflag (描画は VS / PS のみ)
    enum class ShaderStage : unsigned
    {
        None = 0,
        Vertex = 1u << 0,
        Pixel = 1u << 1,
        All = Vertex | Pixel,
    };

    [[nodiscard]] constexpr ShaderStage operator|(ShaderStage a, ShaderStage b) noexcept
    {
        return static_cast<ShaderStage>(static_cast<unsigned>(a) | static_cast<unsigned>(b));
    }
    [[nodiscard]] constexpr ShaderStage operator&(ShaderStage a, ShaderStage b) noexcept
    {
        return static_cast<ShaderStage>(static_cast<unsigned>(a) & static_cast<unsigned>(b));
    }
    [[nodiscard]] constexpr bool HasStage(ShaderStage set, ShaderStage flag) noexcept
    {
        return (static_cast<unsigned>(set) & static_cast<unsigned>(flag)) != 0u;
    }

} // namespace NS::Graphics
