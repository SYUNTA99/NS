#pragma once

#include "Runtime/Core/NonCopyable.h"
#include "Runtime/Graphics/RenderProxyList.h"
#include "Runtime/Graphics/RenderSettings.h"
#include "Runtime/Object/Components/VirtualCamera.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace NS::Gfx
{
    class EffectScene;
    class Renderer;
    class RenderTarget;
    struct RenderContext;
} // namespace NS::Gfx

namespace NS::Obj
{
    class CameraBrain;
    class CameraComponent;
    class DirectionalLight;
    class IRenderable;
    class OverlayRenderer;

    //! @brief 指定の描画先へ指定の視点でシーンを描く単位
    //! @details target が null なら backbuffer、viewPose が空なら Brain の選ぶカメラで描く
    struct SceneView
    {
        NS::Gfx::RenderTarget* target = nullptr; // 描画先、非所有。null は backbuffer
        std::optional<CameraPose> viewPose;      // 描画視点。空なら Brain の選ぶカメラ
    };

    //! @brief 描画物・重ね描き・平行光の登録簿を持ち、1 フレーム分のシーンを描く
    //! @details 登録は Component が OnStart / OnEndPlay で自分で行い、Scene の同名メソッドがここへ転送する
    //! 描画は Scene::OnRenderScene が Render を 1 回呼んで駆動する
    //! 依存: NS::Gfx::Renderer, NS::Gfx::RenderProxyList, NS::Gfx::EffectScene, CameraBrain
    class SceneRenderer : public NS::Core::NonCopyable
    {
    public:
        //! EffectScene を前方宣言のまま持つ。unique_ptr が完全型を要る境目は .cpp 側
        SceneRenderer();
        ~SceneRenderer();

        //! @brief レンダラーを非所有で差す。EffectScene を作り直し、null なら畳む
        //! @details EffectScene は構築時に Gpu() を読む。Renderer が立つまで作れない
        void SetRenderer(NS::Gfx::Renderer* renderer) noexcept;

        //! @brief Preload が .efkefc を探すディレクトリを差す。空のままなら ContentRoot の Assets/Effects
        //! @details effectRoot は EffectScene の構築時に固まる。SetRenderer より前に差す
        void SetEffectRoot(std::string root) noexcept;

        //! 所有している EffectScene。レンダラー未設定の間は nullptr
        [[nodiscard]] NS::Gfx::EffectScene* Effects() noexcept { return m_effects.get(); }

        //! 経過秒ぶんエフェクトを進める。EffectScene が無ければ何もしない
        void UpdateEffects(float deltaSeconds) noexcept;

        //! @brief 1 フレームで描くビュー列を差す。空なら現描画先へ Brain の視点で 1 回だけ描く
        //! @details 空でない間は各ビューを順に bind して描き分ける。差すのは Editor だけで、出荷では常に空
        void SetSceneViews(std::vector<SceneView> views) noexcept { m_sceneViews = std::move(views); }

        //! IRenderable Component の自己登録。二重登録は無視する
        void RegisterRenderable(IRenderable* renderable);
        //! IRenderable Component の自己解除
        void UnregisterRenderable(IRenderable* renderable);

        //! OverlayRenderer の自己登録。二重登録は無視する
        //! 並びは priority 昇順に保たれ、同値なら後から登録した方が後ろになる
        void RegisterOverlay(OverlayRenderer* overlay);
        //! OverlayRenderer の自己解除
        void UnregisterOverlay(OverlayRenderer* overlay);

        //! 平行光の自己登録。二重登録は無視する
        //! 並びは登録順。ResolveSceneSettings はこの順に読む
        void RegisterLight(DirectionalLight* light);
        //! 平行光の自己解除
        void UnregisterLight(DirectionalLight* light);

        //! 登録中の全 renderable の bounds とソート情報を RenderProxyList へ同期する。描画の入口で呼ぶ
        void SyncRenderBounds();

        //! @brief ビュー列を順に描く。列が空なら現描画先へ 1 回だけ描く
        //! @details レンダラー未設定なら何も描かない。ビューごとに Renderer::BeginSceneView で描画先を差し替える
        //! @param[in,out] brain 描画の直前に Evaluate する CameraBrain
        //! @param[in,out] camera brain が駆動する実カメラ。アスペクト比をレンダラーの現在サイズへ揃える
        //! @param[in] skyboxPath 描く skybox の ContentRoot 配下相対パス。空なら skybox を描かない
        void Render(CameraBrain& brain, CameraComponent& camera, std::string_view skyboxPath);

        //! Opaque バケットを視錐台で絞り、並べ替えずに描画する
        void DrawOpaque(const NS::Gfx::RenderContext& context);
        //! Transparent バケットを視錐台で絞り、context.cameraPosition から遠い順に描画する
        //! 距離が同じなら SortPriority 昇順
        void DrawTransparent(const NS::Gfx::RenderContext& context);
        //! 登録中の OverlayRenderer を priority 昇順で描画する。IsActive が偽なら飛ばす
        void DrawOverlays(const NS::Gfx::RenderContext& context);

        //! @brief プロジェクト既定値からシーンの描画設定を作る。有効な平行光があれば照明を上書きする
        //! @details 登録が無ければプロジェクト既定値がそのまま残る
        [[nodiscard]] NS::Gfx::RenderSettings ResolveSceneSettings(const NS::Gfx::RenderSettings& projectDefaults);

    private:
        //! 1 ビュー分のシーンを描き、その上へデバッグ描画と OverlayRenderer の重ね描きを出す
        void RenderViewWithOverlays(CameraBrain& brain,
                                    CameraComponent& camera,
                                    std::string_view skyboxPath,
                                    const std::optional<CameraPose>& viewOverride);

        //! 不透明→空→半透明→エフェクトの順に 1 ビュー分を描き、組んだ RenderContext を返す
        //! viewOverride が空なら Brain の選ぶカメラで描く
        [[nodiscard]] NS::Gfx::RenderContext RenderWorld(CameraBrain& brain,
                                                         CameraComponent& camera,
                                                         std::string_view skyboxPath,
                                                         const std::optional<CameraPose>& viewOverride);

        //! IRenderable と RenderProxyList 登録ハンドルの対。renderable は非所有
        struct RenderEntry
        {
            IRenderable* renderable = nullptr;
            NS::Gfx::RenderHandle handle{};
        };
        std::vector<RenderEntry> m_renderables;

        std::vector<OverlayRenderer*> m_overlays; // 重ね描きの登録簿。priority 昇順、非所有

        std::vector<DirectionalLight*> m_lights; // 平行光の登録簿。登録順、非所有

        NS::Gfx::RenderProxyList m_renderScene;

        bool m_warnedZeroLightDirection = false; // 平行光の向きが 0 の警告を出したか
        NS::Gfx::Renderer* m_renderer = nullptr; // レンダラー、非所有。未設定なら描かない

        std::vector<SceneView> m_sceneViews; // 描くビュー列。空なら現描画先へ 1 回だけ描く

        std::unique_ptr<NS::Gfx::EffectScene> m_effects; // エフェクトの再生と描画。レンダラー未設定の間は空

        std::string m_effectRoot; // .efkefc を探すディレクトリ。空なら既定の Assets/Effects
    };
} // namespace NS::Obj
