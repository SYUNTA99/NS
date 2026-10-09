#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/Core/NonCopyable.h"
#include "NSlib/Graphics/DebugDraw.h"
#include "NSlib/Graphics/RenderContext.h"
#include "NSlib/Graphics/RenderProxyList.h"
#include "NSlib/Graphics/RenderSettings.h"
#include "NSlib/Object/ITickable.h"
#include "NSlib/Object/SubObjects/VirtualCamera.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace NS::Gfx
{
    class Bloom;
    class EffectScene;
    class Renderer;
    class RenderTarget;
    class ScreenPasses;
    class Texture;
} // namespace NS::Gfx

namespace NS::Obj
{
    class CameraManager;
    class SceneCamera;
    class DirectionalLight;
    class IRenderable;
    class IOverlay;
    class UIActor;

    //! @brief 指定の描画先へ指定の視点でシーンを描く単位
    //! @details target が null なら backbuffer、viewPose が空なら CameraManager の選ぶカメラで描く
    //! drawShapes を持つビューだけ、描く直前に使い捨ての DebugShapes へ図形を積ませて描く
    struct SceneView
    {
        NS::Gfx::RenderTarget* target = nullptr; // 描画先、非所有。null は backbuffer
        std::optional<CameraPose> viewPose;      // 描画視点。空なら CameraManager の選ぶカメラ

        //! そのビューを描く間だけ要る開発用の図形を積む口。空なら積まない
        //! 渡される DebugShapes は空で、描いた後に捨てられる。行列はそのビューのビュー射影
        std::function<void(NS::Gfx::DebugShapes&, const NS::Matrix&)> drawShapes;
    };

    //! @brief 描画物・重ね描き・平行光の登録簿を持ち、1 フレーム分のシーンを描く
    //! @details 登録は SubObject が OnStart / OnEndPlay で自分で行い、Scene の同名メソッドがここへ転送する
    //! 描画は Scene::OnRender が Render を 1 回呼んで駆動する
    //! エフェクトの世界はエフェクトの段の登録物として、その段の Actor が出した演出を受けて進む
    //! 世界は NS::Gfx::Bloom の浮動小数の描画先へ描き、1 を超えた分をにじませて書き戻す
    //! 書き戻した絵に NS::Gfx::ScreenPasses の段を掛けてから、重ね描きを描く
    //! 依存: NS::Gfx::Renderer, NS::Gfx::RenderProxyList, NS::Gfx::EffectScene, NS::Gfx::Bloom, CameraManager
    class SceneRenderer : public NS::NonCopyable, public ITickable
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
        [[nodiscard]] NS::Gfx::EffectScene* Effects() const noexcept { return m_effects.get(); }

        //! エフェクトの世界を固定ステップの刻み幅ぶん進める。EffectScene が無ければ何もしない
        void OnTick() override;

        //! @brief 1 フレームで描くビュー列を差す。空なら現描画先へ CameraManager の視点で 1 回だけ描く
        //! @details 空でない間は各ビューを順に bind して描き分ける。差すのは Editor だけで、出荷では常に空
        void SetSceneViews(std::vector<SceneView> views) noexcept { m_sceneViews = std::move(views); }

        //! @brief 次に描くフレームから使う床の波を書く。強さ 0 で消える
        //! @param[in] wave 床の波
        void SetGroundWave(const NS::Gfx::GroundWave& wave) noexcept { m_groundWave = wave; }
        //! 描く時に使う床の波
        [[nodiscard]] const NS::Gfx::GroundWave& GroundWaveShown() const noexcept { return m_groundWave; }

        //! @brief 次に描くフレームから使う歪みの輪を書く。押し 0 で消える
        //! @param[in] ring 歪みの輪
        void SetDistortionRing(const NS::Gfx::DistortionRing& ring) noexcept { m_distortionRing = ring; }
        //! 描く時に使う歪みの輪
        [[nodiscard]] const NS::Gfx::DistortionRing& DistortionRingShown() const noexcept { return m_distortionRing; }

        //! @brief 体の型に写す描く物を差し替える。空なら型を描かない
        //! @details 登録を外した描く物は、ここからも外す
        //! @param[in] renderables 型に写す描く物、非所有
        void SetBodyMask(std::vector<IRenderable*> renderables) noexcept { m_bodyMask = std::move(renderables); }
        //! @brief ビュー列の index 番目に、今のフレームで描いた体の型を返す
        //! @return 1 色 8 ビットの型。描いていなければ null
        [[nodiscard]] const NS::Gfx::Texture* BodyMaskShown(std::size_t viewIndex) const noexcept;

        //! IRenderable SubObject の自己登録。二重登録は無視する
        void RegisterRenderable(IRenderable* renderable);
        //! IRenderable SubObject の自己解除
        void UnregisterRenderable(IRenderable* renderable);

        //! 重ね描きの登録。二重登録は無視する
        //! 並びは OverlayOrder 昇順に保たれ、同値なら後から登録した方が後ろになる
        void RegisterOverlay(IOverlay* overlay);
        //! 重ね描きの解除
        void UnregisterOverlay(IOverlay* overlay);

        //! 画面に出す物の登録。二重登録は無視する。並びは DrawOrder 昇順、同値なら後から入れた方が後ろ
        void RegisterUIActor(UIActor* actor);
        //! 画面に出す物の解除
        void UnregisterUIActor(UIActor* actor) noexcept;
        //! 画面に出す物の一覧。DrawOrder 昇順、非所有
        [[nodiscard]] const std::vector<UIActor*>& UIActors() const noexcept { return m_uiActors; }

        //! 平行光の自己登録。二重登録は無視する
        //! 並びは登録順。ResolveSceneSettings はこの順に読む
        void RegisterLight(DirectionalLight* light);
        //! 平行光の自己解除
        void UnregisterLight(DirectionalLight* light);

        //! 登録中の全 renderable の bounds とソート情報を RenderProxyList へ同期する。描画の入口で呼ぶ
        void SyncRenderBounds();

        //! @brief ビュー列を順に描く。列が空なら現描画先へ 1 回だけ描く
        //! @details レンダラー未設定なら何も描かない。ビューごとに Renderer::BeginSceneView で描画先を差し替える
        //! @param[in,out] cameras 実カメラの行列を引く CameraManager。姿勢は Scene::OnRender が書き終えている
        //! @param[in,out] camera cameras が駆動する実カメラ。アスペクト比をレンダラーの現在サイズへ揃える
        //! @param[in] skyboxPath 描く skybox の ContentRoot 配下相対パス。空なら skybox を描かない
        //! @param[in] alpha 前の固定フレームから今の固定フレームまでの補間の割合 0..1
        //! 1 なら今の固定フレームの姿
        void Render(CameraManager& cameras, SceneCamera& camera, std::string_view skyboxPath, float alpha);

        //! Opaque バケットを視錐台で絞り、並べ替えずに描画する
        void DrawOpaque(const NS::Gfx::RenderContext& context);
        //! Transparent バケットを視錐台で絞り、context.cameraPosition から遠い順に描画する
        //! 距離が同じなら SortPriority 昇順
        void DrawTransparent(const NS::Gfx::RenderContext& context);
        //! 登録中の OverlayRenderer を priority 昇順で描画し、その上へ画面に出す物を DrawOrder 昇順で描く
        //! OverlayRenderer は IsActive が偽なら飛ばす
        void DrawOverlays(const NS::Gfx::RenderContext& context);

        //! @brief プロジェクト既定値からシーンの描画設定を作る。有効な平行光があれば照明を上書きする
        //! @details 登録が無ければプロジェクト既定値がそのまま残る
        [[nodiscard]] NS::Gfx::RenderSettings ResolveSceneSettings(const NS::Gfx::RenderSettings& projectDefaults);

    private:
        //! ビューごとの画面の段。ビューごとに描画先の大きさが違うので分けて持つ
        struct ViewPasses
        {
            std::unique_ptr<NS::Gfx::Bloom> bloom;
            std::unique_ptr<NS::Gfx::ScreenPasses> screen;
        };

        //! @brief 1 ビュー分のシーンを描き、その上へデバッグ描画と OverlayRenderer の重ね描きを出す
        //! @details デバッグ描画は、この固定ステップの図形、view.drawShapes が積んだ図形の順に描く
        //! view.viewPose が空なら実カメラで描く
        void RenderViewWithOverlays(CameraManager& cameras,
                                    SceneCamera& camera,
                                    std::string_view skyboxPath,
                                    const SceneView& view,
                                    float alpha,
                                    ViewPasses& passes);

        //! 不透明→空→半透明→エフェクトの順に 1 ビュー分を bloom の描画先へ描く
        //! にじみを足して今の描画先へ書き戻し、画面の段を掛ける
        //! 組んだ RenderContext を返す。viewOverride が空なら実カメラで描く
        [[nodiscard]] NS::Gfx::RenderContext RenderWorld(CameraManager& cameras,
                                                         SceneCamera& camera,
                                                         std::string_view skyboxPath,
                                                         const std::optional<CameraPose>& viewOverride,
                                                         float alpha,
                                                         ViewPasses& passes);

        //! ビュー列の index 番目に使う画面の段。無ければ作る
        [[nodiscard]] ViewPasses& PassesForView(std::size_t index);

        //! 体の型に写す描く物を、世界の深度で隠して型へ描く。写す物が無ければ型を描かない
        void DrawBodyMask(const NS::Gfx::RenderContext& context, NS::Gfx::ScreenPasses& screen);

        //! IRenderable と RenderProxyList 登録ハンドルの対。renderable は非所有
        struct RenderEntry
        {
            IRenderable* renderable = nullptr;
            NS::Gfx::RenderHandle handle{};
        };
        std::vector<RenderEntry> m_renderables;

        std::vector<IOverlay*> m_overlays; // 重ね描きの登録簿。OverlayOrder 昇順、非所有
        std::vector<UIActor*> m_uiActors;  // 画面に出す物の登録簿。DrawOrder 昇順、非所有

        std::vector<DirectionalLight*> m_lights; // 平行光の登録簿。登録順、非所有

        NS::Gfx::RenderProxyList m_renderScene;

        bool m_warnedZeroLightDirection = false; // 平行光の向きが 0 の警告を出したか
        NS::Gfx::Renderer* m_renderer = nullptr; // レンダラー、非所有。未設定なら描かない

        std::vector<SceneView> m_sceneViews; // 描くビュー列。空なら現描画先へ 1 回だけ描く

        NS::Gfx::DebugShapes m_viewShapes; // ビューの図形の使い捨ての溜め場。1 ビューを描く間だけ中身がある

        std::unique_ptr<NS::Gfx::EffectScene> m_effects; // エフェクトの再生と描画。レンダラー未設定の間は空

        // 光のにじみと画面の段。ビュー列と同じ並び。レンダラーを差し替えると畳み、描く時に作る
        std::vector<ViewPasses> m_viewPasses;

        std::string m_effectRoot;                       // .efkefc を探すディレクトリ。空なら既定の Assets/Effects
        NS::Gfx::GroundWave m_groundWave{};             // 描く床の波。書くのは当たりの裁定役だけ
        NS::Gfx::DistortionRing m_distortionRing{};     // 描く歪みの輪。書くのは当たりの裁定役だけ
        std::vector<IRenderable*> m_bodyMask;           // 体の型に写す描く物、非所有
        std::vector<NS::Gfx::DrawItem> m_bodyMaskItems; // 体の型に描く物。描くたびに組み直す
    };
} // namespace NS::Obj
