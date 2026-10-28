#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/Core/NonCopyable.h"
#include "NSlib/Gfx/Render/EffectTypes.h"

#pragma warning(push, 0)
#include <Effekseer.h>
#include <EffekseerRendererCommon/EffekseerRenderer.Renderer.h>
#pragma warning(pop)

#include "NSlib/Core/Types.h"
#include <array>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace NS
{
    class CameraData;
} // namespace NS

namespace NS
{
    namespace Gfx
    {
        class D3DTexture;

        //! Effekseer の再生と描画。D3DDevice の構築後に作り、Preload 後に再生する
        class EffectScene : public NonCopyable
        {
        public:
            //! effectRoot は Preload が .efkefc を探す基準ディレクトリ
            explicit EffectScene(std::string effectRoot) noexcept;
            ~EffectScene();

            [[nodiscard]] bool IsValid() const noexcept;

            //! effectRoot 相対の拡張子なしパス (UTF-8) から .efkefc と参照資産を読み込む。読み込み済みなら再利用する
            [[nodiscard]] bool Preload(std::string_view name) noexcept;
            //! 同じ識別子でファイルの道が変わった場合は読み直す
            [[nodiscard]] bool Preload(const EffectResourceDesc& asset) noexcept;
            //! 読み込み済みのファイルと定義の道が一致しない場合は無効なハンドルを返す
            [[nodiscard]] EffectHandle Play(const EffectResourceDesc& asset, const EffectPlayDesc& desc) noexcept;

            //! 読み込み済みのエフェクトをワールド座標 position で再生する。未読み込み時は無効なハンドルを返す
            [[nodiscard]] EffectHandle Play(std::string_view name, Vector3 position = {}) noexcept;

            //! 読み込み済みのエフェクトを再生する (パーティクル生成は次の Update
            //! 時)。未読み込み時は無効なハンドルを返す
            //! 乱数シードはエフェクト名ごとの再生回数から決定するため、他エフェクトの再生順序に影響されない
            [[nodiscard]] EffectHandle Play(std::string_view name, const EffectPlayDesc& desc) noexcept;

            //! ワールド座標の姿勢を設定する (次の Update で反映)
            void SetTransform(EffectHandle handle,
                              const Vector3& position,
                              const Quaternion& rotation,
                              const Vector3& scale) noexcept;

            //! 動的入力 (index 0〜3) を設定する。生成時の初期値は最初の Update 前に設定すること
            void SetDynamicInput(EffectHandle handle, int index, float value) noexcept;

            //! 子も含めて次の Update で消去する (deltaSeconds == 0.0f の Update でも反映)
            void Stop(EffectHandle handle) noexcept;

            //! 新規生成を止め、既存のパーティクルは寿命まで残す
            void StopRoot(EffectHandle handle) noexcept;

            void StopAll() noexcept;

            [[nodiscard]] bool Exists(EffectHandle handle) const noexcept;

            //! deltaSeconds == 0.0f の場合は時間を進めず Play と Stop のみ反映し、負値は何もしない
            void Update(float deltaSeconds) noexcept;

            //! 全エフェクトを描画する。D24_UNORM_S8_UINT の深度をコピーできない場合は深度なしで描画する
            void Draw(const CameraData& camera) noexcept;

            //! 直前の Draw でエフェクトへ深度を渡せたか
            [[nodiscard]] bool PassesDepth() const noexcept;

            //! 直前の Draw で深度コピー不可により深度なし描画へフォールバックしたか
            [[nodiscard]] bool IsUsingDepthFallback() const noexcept { return m_depthFallback; }

            [[nodiscard]] uint32 DepthCopiesCreated() const noexcept { return m_depthCopiesCreated; }

        private:
            struct DepthCopy
            {
                std::unique_ptr<D3DTexture> texture;
                Effekseer::Backend::TextureRef effekseerTexture;
                uint32 width = 0;
                uint32 height = 0;
                uint32 lastUsed = 0;
            };

            bool PreloadFile(std::string_view id, const std::string& path, uint32 seed) noexcept;
            std::unordered_map<std::string, uint32> m_seedBases;
            std::unordered_map<std::string, std::string> m_resourcePaths;

            void PassDepth(const CameraData& camera) noexcept;
            [[nodiscard]] DepthCopy* DepthCopyFor(uint32 width, uint32 height) noexcept;
            void FallBackWithoutDepth(std::string_view reason, uint32 width, uint32 height) noexcept;

            Effekseer::ManagerRef m_manager;
            EffekseerRenderer::RendererRef m_renderer;
            std::unordered_map<std::string, Effekseer::EffectRef> m_effects;
            std::unordered_set<std::string> m_warnedMissing;
            std::unordered_map<std::string, uint32> m_playCounts;
            std::string m_effectRoot;
            float m_elapsedSeconds = 0.0f;

            std::array<DepthCopy, 2> m_depthCopies{};
            uint32 m_depthCopiesCreated = 0;
            uint32 m_drawCount = 0;
            bool m_depthFallback = false;
            std::string m_failedDepthKey;
        };
    } // namespace Gfx
} // namespace NS
