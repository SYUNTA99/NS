#pragma once

#include "Runtime/Core/Math.h"
#include "Runtime/Core/NonCopyable.h"

#pragma warning(push, 0)
#include <Effekseer.h>
#include <EffekseerRendererCommon/EffekseerRenderer.Renderer.h>
#pragma warning(pop)

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace NS::Graphics
{
    class Camera;

    //! @brief EffectScene の構築パラメータ
    struct EffectSceneDesc
    {
        std::string effectRoot; //!< Preload が .efkefc を探すディレクトリ
    };

    //! @brief EffectScene::Play が返す、再生したエフェクトのハンドル
    struct EffectHandle
    {
        std::int32_t value = -1; //!< Effekseer の Manager が返したハンドル値。既定の -1 は無効

        //! value が 0 以上の場合 true、それ以外の場合は false
        //! @details エフェクトが今も残っているかは EffectScene::Exists で見る
        [[nodiscard]] bool IsValid() const noexcept { return value >= 0; }
    };

    //! @brief EffectScene::Play の再生パラメータ
    struct EffectPlayDesc
    {
        std::string name;                             //!< Preload に渡した名前
        NS::Core::Vector3 position{0.0f, 0.0f, 0.0f}; //!< 再生位置のワールド座標
    };

    //! @brief Effekseer のエフェクトを読み込み、再生して描くクラス
    //! @details 構築時に Gpu() の device と context を使う。NS の Renderer が未構築なら無効な状態になる
    //! 構築に失敗した場合は例外を送出せず、無効な状態として扱う。無効な状態では各操作は何もしない
    //! エフェクトは Preload で読み込んでから Play で再生する。Play はファイルを読まない
    class EffectScene : public NS::Core::NonCopyable
    {
    public:
        //! @brief Effekseer の Renderer と Manager を作る
        //! @param[in] desc 構築パラメータ
        explicit EffectScene(const EffectSceneDesc& desc) noexcept;
        ~EffectScene();

        //! 構築に成功した場合 true、それ以外の場合は false
        [[nodiscard]] bool IsValid() const noexcept;

        //! @brief effectRoot から name.efkefc を読み込み、Play で再生できるようにする
        //! @param[in] name 拡張子を除いた effectRoot 相対のパス。UTF-8 で渡す
        //! @return 読み込めたか読み込み済みの場合 true、それ以外の場合は false
        //! @details 参照するテクスチャ・モデル・マテリアル・カーブを 1 つでも読めなければ失敗にする
        //! 読み込めた name は 2 回目以降ファイルを読まない
        [[nodiscard]] bool Preload(std::string_view name) noexcept;

        //! @brief Preload 済みのエフェクトを 1 つ再生する
        //! @param[in] desc 再生パラメータ
        //! @return 再生したエフェクトのハンドル。Preload していない名前なら IsValid が false のハンドル
        //! @details Preload していない名前の警告は名前ごとに 1 回だけ出す
        [[nodiscard]] EffectHandle Play(const EffectPlayDesc& desc) noexcept;

        //! @brief handle のエフェクトを止める
        //! @param[in] handle Play が返したハンドル。IsValid が false なら何もしない
        void Stop(EffectHandle handle) noexcept;

        //! 再生中の全エフェクトを止める
        void StopAll() noexcept;

        //! @brief handle のエフェクトが Effekseer の Manager に残っているかを返す
        //! @param[in] handle Play が返したハンドル
        //! @return 残っている場合 true、それ以外の場合は false
        [[nodiscard]] bool Exists(EffectHandle handle) const noexcept;

        //! @brief 全エフェクトを deltaSeconds だけ進める
        //! @param[in] deltaSeconds 経過秒数。0 なら進めずに Play と Stop だけを描画へ反映する。負なら何もしない
        void Update(float deltaSeconds) noexcept;

        //! @brief camera から見た全エフェクトを現在の描画先へ描く
        //! @param[in] camera ビュー行列・投影行列・位置を使う視点
        void Draw(const Camera& camera) noexcept;

    private:
        Effekseer::ManagerRef m_manager;
        EffekseerRenderer::RendererRef m_renderer;
        std::unordered_map<std::string, Effekseer::EffectRef> m_effects;
        std::unordered_set<std::string> m_warnedMissing;
        std::string m_effectRoot;
        float m_elapsedSeconds = 0.0f;
    };
} // namespace NS::Graphics
