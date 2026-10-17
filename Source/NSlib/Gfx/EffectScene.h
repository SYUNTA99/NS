#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/Core/NonCopyable.h"
#include "NSlib/Gfx/EffectTypes.h"

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

        //! Effekseer の再生と描画。Renderer の構築後に作り、Preload 後に再生する
        //! 無効な状態では各操作を行わない
        class EffectScene : public NonCopyable
        {
        public:
            //! @brief Effekseer の Renderer と Manager を作る
            //! @param[in] effectRoot Preload が .efkefc を探すディレクトリ
            explicit EffectScene(std::string effectRoot) noexcept;
            ~EffectScene();

            //! 構築に成功した場合 true、それ以外の場合は false
            [[nodiscard]] bool IsValid() const noexcept;

            //! @brief .efkefc と参照資産を読み込む。読み込み済みの名前は再利用する
            //! @param[in] name effectRoot 相対の拡張子なしのパス UTF-8
            //! @return 全資産を読み込めた場合 true、それ以外の場合は false
            [[nodiscard]] bool Preload(std::string_view name) noexcept;

            //! 読み込み済みの名前を世界座標の position で再生する。未読み込みなら無効なハンドル
            [[nodiscard]] EffectHandle Play(std::string_view name, Vector3 position = {}) noexcept;

            //! 読み込み済みの名前を再生する。粒の生成は次の Update、未読み込みなら無効なハンドル
            //! 同じ名前の再生回数から乱数を決めるため、他のエフェクトの乱数に影響されない
            [[nodiscard]] EffectHandle Play(std::string_view name, const EffectPlayDesc& desc) noexcept;

            //! 世界座標の姿勢を設定する。反映は次の Update、無効なハンドルでは何もしない
            void SetTransform(EffectHandle handle,
                              const Vector3& position,
                              const Quaternion& rotation,
                              const Vector3& scale) noexcept;

            //! 動的入力 index 0〜3 を設定する。生成時の値は最初の Update 前に設定する
            //! 範囲外の番号や無効なハンドルでは何もしない
            void SetDynamicInput(EffectHandle handle, int index, float value) noexcept;

            //! 子も含めて次の Update で消す。経過秒 0 の更新でも反映し、無効なハンドルは無視する
            void Stop(EffectHandle handle) noexcept;

            //! 新しい子の生成を止め、出ていた子は寿命まで残す。無効なハンドルは無視する
            void StopRoot(EffectHandle handle) noexcept;

            //! 再生中の全エフェクトを止める
            void StopAll() noexcept;

            [[nodiscard]] bool Exists(EffectHandle handle) const noexcept;

            //! @brief 全エフェクトを deltaSeconds だけ進める
            //! @param[in] deltaSeconds 経過秒数。0 なら進めずに Play と Stop だけを描画へ反映する。負なら何もしない
            void Update(float deltaSeconds) noexcept;

            //! 全エフェクトを描く。D24_UNORM_S8_UINT の深度を渡せない場合は深度なしで描く
            //! 深度のコピーに失敗した場合は IsUsingDepthFallback で確認できる
            void Draw(const CameraData& camera) noexcept;

            //! 直前の Draw がエフェクトへ奥行きを渡した場合 true、それ以外の場合は false
            [[nodiscard]] bool PassesDepth() const noexcept;

            //! 直前の Draw が写せない奥行きか写し先の作りそこないで奥行き無しに倒した場合 true、それ以外の場合は false
            [[nodiscard]] bool IsUsingDepthFallback() const noexcept { return m_depthFallback; }

            //! 奥行きの写し先を作った回数
            [[nodiscard]] uint32 DepthCopiesCreated() const noexcept { return m_depthCopiesCreated; }

        private:
            // 奥行きの写し先 1 枚。描画先の大きさで引く
            struct DepthCopy
            {
                std::unique_ptr<D3DTexture> texture;
                Effekseer::Backend::TextureRef effekseerTexture;
                uint32 width = 0;
                uint32 height = 0;
                uint32 lastUsed = 0; // 最後に使った Draw の番号。3 つ目の大きさが来たら古い方を捨てる
            };

            // 描画先に結ばれた奥行きを写し先へ写してエフェクトへ渡す。渡せない時は奥行きを外す
            void PassDepth(const CameraData& camera) noexcept;
            // 大きさの合う写し先を返す。無ければ作る。作れなければ nullptr
            [[nodiscard]] DepthCopy* DepthCopyFor(uint32 width, uint32 height) noexcept;
            // 奥行き無しへ倒す。同じ理由と大きさでは 2 回目からエラーを出さない
            void FallBackWithoutDepth(std::string_view reason, uint32 width, uint32 height) noexcept;

            Effekseer::ManagerRef m_manager;
            EffekseerRenderer::RendererRef m_renderer;
            std::unordered_map<std::string, Effekseer::EffectRef> m_effects;
            std::unordered_set<std::string> m_warnedMissing;
            // 名前ごとに Play した回数。乱数の種に混ぜる
            std::unordered_map<std::string, uint32> m_playCounts;
            std::string m_effectRoot;
            float m_elapsedSeconds = 0.0f;

            std::array<DepthCopy, 2> m_depthCopies{}; // Bloom と同じくゲームの絵とエディタのビューで 1 枚ずつ
            uint32 m_depthCopiesCreated = 0;
            uint32 m_drawCount = 0;
            bool m_depthFallback = false;
            std::string m_failedDepthKey; // 最後にエラーを出した理由と大きさ。同じ失敗を毎フレーム出さない
        };
    } // namespace Gfx
} // namespace NS
