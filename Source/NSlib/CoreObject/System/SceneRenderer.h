#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/CoreObject/SubObjects/VirtualCamera.h"
#include "NSlib/CoreObject/System/SceneSystem.h"
#include "NSlib/Gfx/DebugDraw.h"
#include "NSlib/Gfx/RenderContext.h"
#include "NSlib/Gfx/RenderProxyList.h"
#include "NSlib/Gfx/RenderSettings.h"
#include "NSlib/Gfx/ScreenFade.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace NS
{
    namespace Gfx
    {
        class Bloom;
    }
    namespace Gfx
    {
        class EffectScene;
    }
    namespace Gfx
    {
        class Renderer;
    }
    namespace Gfx
    {
        class D3DRenderTarget;
    }
    namespace Gfx
    {
        class ScreenPasses;
    }
    namespace Gfx
    {
        class D3DTexture;
    }
} // namespace NS

namespace NS
{
    class CameraManager;
    class SceneCamera;
    class DirectionalLight;
    class IRenderable;
    class IOverlay;
    class UIDirector;

    //! @brief 指定の描画先へ指定の視点でシーンを描く単位
    //! @details target が未指定ならウィンドウへ、viewPose が空なら CameraManager のカメラで描く
    //! drawShapes を持つビューだけ、描く直前に使い捨ての DebugShapes へ図形を積ませて描く
    struct SceneView
    {
        Gfx::D3DRenderTarget* target = nullptr; // 非所有の描画先。未指定ならウィンドウへ描く
        std::optional<CameraPose> viewPose;      // 描画視点。空なら CameraManager の選ぶカメラ

        //! そのビューを描く間だけ要る開発用の図形を積む口。空なら積まない
        //! 渡される DebugShapes は空で、描いた後に捨てられる。行列はそのビューのビュー射影
        std::function<void(Gfx::DebugShapes&, const Matrix&)> drawShapes;
    };

    //! @brief シーンの描画物を登録し、各ビューへ描画する
    //! @details 世界と後処理の後に重ね描きと UI、最後にシーン全体の暗転を描く
    class SceneRenderer : public SceneSystem
    {
    public:
        SceneRenderer();
        ~SceneRenderer();

        //! @brief レンダラーを非所有で差す。EffectScene を作り直し、未設定なら畳む
        //! @details EffectScene は構築時に Gpu() を読む。Renderer が立つまで作れない
        void SetRenderer(Gfx::Renderer* renderer) noexcept;

        //! @brief Preload が .efkefc を探すディレクトリを差す。空のままなら ContentRoot の Assets/Effects
        //! @details effectRoot は EffectScene の構築時に固まる。SetRenderer より前に差す
        void SetEffectRoot(std::string root) noexcept;

        //! 所有している EffectScene。レンダラー未設定の間は nullptr
        [[nodiscard]] Gfx::EffectScene* Effects() const noexcept { return m_effects.get(); }

        //! エフェクトの世界を固定ステップの刻み幅ぶん進める。EffectScene が無ければ何もしない
        void OnTick() override;

        //! @brief 1 フレームで描くビュー列を差す。空なら現描画先へ CameraManager の視点で 1 回だけ描く
        //! @details 空でない間は各ビューを順に bind して描き分ける。差すのは Editor だけで、出荷では常に空
        void SetSceneViews(std::vector<SceneView> views) noexcept { m_sceneViews = std::move(views); }

        //! @brief 次に描くフレームから使う床の波を書く。強さ 0 で消える
        void SetGroundWave(const Gfx::GroundWave& wave) noexcept { m_groundWave = wave; }
        //! 描く時に使う床の波
        [[nodiscard]] const Gfx::GroundWave& GroundWaveShown() const noexcept { return m_groundWave; }

        //! @brief 次に描くフレームから使う歪みの輪を書く。押し 0 で消える
        void SetDistortionRing(const Gfx::DistortionRing& ring) noexcept { m_distortionRing = ring; }
        //! 描く時に使う歪みの輪
        [[nodiscard]] const Gfx::DistortionRing& DistortionRingShown() const noexcept { return m_distortionRing; }

        //! @brief 体の型に写す描く物を差し替える。空なら型を描かない
        //! @details 登録を外した描く物は、ここからも外す
        //! @param[in] renderables 型に写す描く物、非所有
        void SetBodyMask(std::vector<IRenderable*> renderables) noexcept { m_bodyMask = std::move(renderables); }
        //! @brief ビュー列の index 番目に、今のフレームで描いた体の型を返す
        //! @return 1 色 8 ビットの型。未描画なら空
        [[nodiscard]] const Gfx::D3DTexture* BodyMaskShown(std::size_t viewIndex) const noexcept;

        //! IRenderable の部品の自己登録。二重登録は無視する
        void RegisterRenderable(IRenderable* renderable);
        //! IRenderable の部品の自己解除
        void UnregisterRenderable(IRenderable* renderable);

        //! 重ね描きの登録。二重登録は無視する
        //! 並びは OverlayOrder 昇順に保たれ、同値なら後から登録した方が後ろになる
        void RegisterOverlay(IOverlay* overlay);
        //! 重ね描きの解除
        void UnregisterOverlay(IOverlay* overlay);

        //! 平行光の自己登録。二重登録は無視する
        //! 並びは登録順。ResolveSceneSettings はこの順に読む
        void RegisterLight(DirectionalLight* light);
        //! 平行光の自己解除
        void UnregisterLight(DirectionalLight* light);

        [[nodiscard]] std::size_t RenderableCount() const noexcept { return m_renderables.size(); }
        [[nodiscard]] std::size_t OverlayCount() const noexcept { return m_overlays.size(); }
        [[nodiscard]] std::size_t LightCount() const noexcept { return m_lights.size(); }

        //! 登録中の全 renderable の bounds とソート情報を RenderProxyList へ同期する。描画の入口で呼ぶ
        void SyncRenderBounds();

        //! ビューを順に描く。未指定なら現在の描画先、レンダラーなしなら描画しない
        //! カメラの姿勢は呼ぶ前に確定する。alpha は固定更新間の割合 0〜1
        //! skyboxPath は ContentRoot 内の相対パス。空なら空を描かない
        void Render(CameraManager& cameras,
                    SceneCamera& camera,
                    UIDirector& ui,
                    const Gfx::ScreenFade& fade,
                    std::string_view skyboxPath,
                    float alpha);

        //! Opaque バケットを視錐台で絞り、並べ替えずに描画する
        void DrawOpaque(const Gfx::RenderContext& context);
        //! Transparent バケットを視錐台で絞り、context.cameraPosition から遠い順に描画する
        //! 距離が同じなら SortPriority 昇順
        void DrawTransparent(const Gfx::RenderContext& context);
        //! 登録中の重ね描きを OverlayOrder 昇順で描画し、その上へ ui の画面を DrawOrder 昇順で描く
        //! IsOverlayVisible が false の重ね描きは飛ばす
        //! 最後にシーン全体の暗転 fade を重ねる
        void DrawOverlays(const Gfx::RenderContext& context, UIDirector& ui, const Gfx::ScreenFade& fade);

        //! @brief プロジェクト既定値からシーンの描画設定を作る。有効な平行光があれば照明を上書きする
        //! @details 登録が無ければプロジェクト既定値がそのまま残る
        [[nodiscard]] Gfx::RenderSettings ResolveSceneSettings(const Gfx::RenderSettings& projectDefaults);

    private:
        //! ビューごとの画面の段。ビューごとに描画先の大きさが違うので分けて持つ
        struct ViewPasses
        {
            std::unique_ptr<Gfx::Bloom> bloom;
            std::unique_ptr<Gfx::ScreenPasses> screen;
        };

        //! @brief 1 ビュー分のシーンを描き、その上へデバッグ描画と OverlayRenderer の重ね描きを出す
        //! @details デバッグ描画は、この固定ステップの図形、view.drawShapes が積んだ図形の順に描く
        //! view.viewPose が空なら実カメラで描く
        void RenderViewWithOverlays(CameraManager& cameras,
                                    SceneCamera& camera,
                                    UIDirector& ui,
                                    const Gfx::ScreenFade& fade,
                                    std::string_view skyboxPath,
                                    const SceneView& view,
                                    float alpha,
                                    ViewPasses& passes);

        //! 不透明→空→半透明→エフェクトの順に 1 ビュー分を bloom の描画先へ描く
        //! にじみを足して今の描画先へ書き戻し、画面の段を掛ける
        //! 組んだ RenderContext を返す。viewOverride が空なら実カメラで描く
        [[nodiscard]] Gfx::RenderContext RenderWorld(CameraManager& cameras,
                                                         SceneCamera& camera,
                                                         std::string_view skyboxPath,
                                                         const std::optional<CameraPose>& viewOverride,
                                                         float alpha,
                                                         ViewPasses& passes);

        //! ビュー列の index 番目に使う画面の段。無ければ作る
        [[nodiscard]] ViewPasses& PassesForView(std::size_t index);

        //! 体の型に写す描く物を、世界の深度で隠して型へ描く。写す物が無ければ型を描かない
        void DrawBodyMask(const Gfx::RenderContext& context, Gfx::ScreenPasses& screen);

        //! IRenderable と RenderProxyList 登録ハンドルの対。renderable は非所有
        struct RenderEntry
        {
            IRenderable* renderable = nullptr;
            Gfx::RenderHandle handle{};
        };
        std::vector<RenderEntry> m_renderables;

        std::vector<IOverlay*> m_overlays; // 重ね描きの登録簿。OverlayOrder 昇順、非所有

        std::vector<DirectionalLight*> m_lights; // 平行光の登録簿。登録順、非所有

        Gfx::RenderProxyList m_renderScene;

        bool m_warnedZeroLightDirection = false; // 平行光の向きが 0 の警告を出したか
        Gfx::Renderer* m_renderer = nullptr; // レンダラー、非所有。未設定なら描かない

        std::vector<SceneView> m_sceneViews; // 描くビュー列。空なら現描画先へ 1 回だけ描く

        Gfx::DebugShapes m_viewShapes; // ビューの図形の使い捨ての溜め場。1 ビューを描く間だけ中身がある

        std::unique_ptr<Gfx::EffectScene> m_effects; // エフェクトの再生と描画。レンダラー未設定の間は空

        // 光のにじみと画面の段。ビュー列と同じ並び。レンダラーを差し替えると畳み、描く時に作る
        std::vector<ViewPasses> m_viewPasses;

        std::string m_effectRoot;                       // .efkefc を探すディレクトリ。空なら既定の Assets/Effects
        Gfx::GroundWave m_groundWave{};             // 描く床の波。書くのは当たりの裁定役だけ
        Gfx::DistortionRing m_distortionRing{};     // 描く歪みの輪。書くのは当たりの裁定役だけ
        std::vector<IRenderable*> m_bodyMask;           // 体の型に写す描く物、非所有
        std::vector<Gfx::DrawItem> m_bodyMaskItems; // 体の型に描く物。描くたびに組み直す
    };
} // namespace NS
