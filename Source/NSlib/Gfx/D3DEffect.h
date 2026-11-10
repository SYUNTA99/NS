#pragma once

#include "NSlib/Core/NonCopyable.h"
#include "NSlib/Core/Types.h"

#pragma warning(push, 0)
#include <Effekseer.h>
#pragma warning(pop)

#include <memory>
#include <optional>
#include <string_view>

namespace NS
{
    namespace Gfx
    {
        //! Effekseer のエフェクトデータ (.efkefc と依存するテクスチャ・モデル・マテリアル・カーブ)
        //! @warning D3DDevice より先に破棄すること
        class D3DEffect : public NonCopyable
        {
        public:
            //! ファイルからエフェクトを読み込む。D3DDevice の無い場では ERROR を出さずに nullptr を返す
            [[nodiscard]] static std::unique_ptr<D3DEffect> LoadFromFile(
                std::string_view path,
                std::optional<uint32> seed = std::nullopt) noexcept;

            //! ファイル名の幹 (拡張子を除いた名前) から決まる乱数の種を返す
            [[nodiscard]] static uint32 DefaultSeed(std::string_view path) noexcept;

            ~D3DEffect();

            [[nodiscard]] bool IsValid() const noexcept { return m_effect != nullptr; }
            [[nodiscard]] const Effekseer::EffectRef& Native() const noexcept { return m_effect; }
            [[nodiscard]] uint32 Seed() const noexcept { return m_seed; }

        private:
            D3DEffect(Effekseer::EffectRef effect, uint32 seed) noexcept;

            Effekseer::EffectRef m_effect;
            uint32 m_seed = 1u;
        };
    } // namespace Gfx
} // namespace NS
