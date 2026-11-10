#pragma once

#include "NSlib/Core/NonCopyable.h"
#include "NSlib/Core/Types.h"

#pragma warning(push, 0)
#include <Effekseer.h>
#pragma warning(pop)

#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace NS
{
    namespace Gfx
    {
        //! エフェクトが依存するテクスチャ・モデル・マテリアル・カーブを読む Effekseer の Setting
        //! 同じ物で読んだエフェクトどうしは、同じ道筋の依存を 1 回だけ読んで使い回す
        //! @warning D3DDevice より先に破棄すること
        class D3DEffectLoader : public NonCopyable
        {
        public:
            //! 今の D3DDevice で作る。D3DDevice の無い場では ERROR を出さずに nullptr を返す
            [[nodiscard]] static std::unique_ptr<D3DEffectLoader> Create() noexcept;

            ~D3DEffectLoader();

            [[nodiscard]] const Effekseer::SettingRef& Native() const noexcept { return m_setting; }

            //! 作った時の D3DDevice が今の D3DDevice か。装置を作り直した後は false で、作り直して使う
            [[nodiscard]] bool IsForCurrentDevice() const noexcept;

        private:
            D3DEffectLoader(Effekseer::SettingRef setting, const void* nativeDevice) noexcept;

            Effekseer::SettingRef m_setting;
            // 作った時の ID3D11Device。見分けるだけで、指す先は読まない。m_setting が装置を生かしているので番地は使い回されない
            const void* m_nativeDevice = nullptr;
        };

        //! Effekseer のエフェクトデータ (.efkefc と依存するテクスチャ・モデル・マテリアル・カーブ)
        //! 読めなかった物は作らないので、作られた物は必ず中身を持つ
        //! @warning D3DDevice より先に破棄すること
        class D3DEffect : public NonCopyable
        {
        public:
            //! ファイルからエフェクトを読み込む。D3DDevice の無い場では ERROR を出さずに nullptr を返す
            //! @param[in] loader 依存を読む物。nullptr ならこの呼び出しのために 1 つ作る。
            //!                   今の D3DDevice で作っていない物は ERROR を出して nullptr を返す
            [[nodiscard]] static std::unique_ptr<D3DEffect> LoadFromFile(
                std::string_view path,
                std::optional<uint32> seed = std::nullopt,
                const D3DEffectLoader* loader = nullptr) noexcept;

            //! ファイル名の幹 (拡張子を除いた名前) から決まる乱数の種を返す
            [[nodiscard]] static uint32 DefaultSeed(std::string_view path) noexcept;

            ~D3DEffect();

            [[nodiscard]] const Effekseer::EffectRef& Native() const noexcept { return m_effect; }
            [[nodiscard]] uint32 Seed() const noexcept { return m_seed; }
            //! 読んだファイルの道筋。EffectScene が再生の回数を数える鍵に使う
            [[nodiscard]] const std::string& Path() const noexcept { return m_path; }

        private:
            D3DEffect(Effekseer::EffectRef effect, uint32 seed, std::string path) noexcept;

            Effekseer::EffectRef m_effect;
            uint32 m_seed = 1u;
            std::string m_path;
        };
    } // namespace Gfx
} // namespace NS
