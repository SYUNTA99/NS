#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/CoreObject/ClassObjs/VirtualCamera.h"
#include "NSlib/Core/NonCopyable.h"
#include "NSlib/Core/Types.h"
#include "NSlib/Gfx/Render/DebugDraw.h"
#include "NSlib/Gfx/Render/BuiltinShaders.h"
#include "NSlib/Gfx/Render/DrawItem.h"
#include "NSlib/Gfx/Render/RenderContext.h"
#include "NSlib/Gfx/Render/RenderSettings.h"
#include "NSlib/Gfx/Render/ScreenFade.h"

#include <array>
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
        class SceneDepthCopy;
    }
    namespace Gfx
    {
        class D3DDevice;
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
    namespace Gfx
    {
        class ScreenOverlay;
    }
    namespace Gfx
    {
        class Skybox;
    }
} // namespace NS

namespace NS
{
    class SceneCamera;
    class DirectionalLight;
    class IOverlay;
    class Model;
    class Shadow;
    class Transform;
    class UIDirector;

    //! 指定の描画先へ指定の視点でシーンを描く単位
    //! target が未指定ならウィンドウへ、viewPose が空なら実カメラで描く
    //! drawShapes を持つビューだけ、描く直前に使い捨ての DebugShapes へ図形を積ませて描く
    struct SceneView
    {
        Gfx::D3DRenderTarget* target = nullptr; // 非所有の描画先。未指定ならウィンドウへ描く
        std::optional<CameraPose> viewPose;      // 描画視点。空なら実カメラ

        //! そのビューを描く間だけ要る開発用の図形を積む口。空なら積まない
        //! 渡される DebugShapes は空で、描いた後に捨てられる。行列はそのビューのビュー射影
        std::function<void(Gfx::DebugShapes&, const Matrix&)> drawShapes;
    };

    //! シーンの描画物を登録し、各ビューへ描画する
    //! 世界と後処理の後に重ね描きと UI、最後にシーン全体の暗転を描く
    class SceneRenderer : public NonCopyable
    {
    public:
        SceneRenderer();
        ~SceneRenderer();

        //! 装置を非所有で差す。EffectScene と描く段を作り直し、未設定なら畳む
        //! EffectScene は構築時に Gpu() を読む。D3DDevice が立つまで作れない
        //! 描く段のシェーダーはここで AssetManager::Get() から引くので、AssetManager を差した後に呼ぶ
        void SetDevice(Gfx::D3DDevice* device) noexcept;

        //! 所有している EffectScene。装置未設定の間は nullptr
        [[nodiscard]] Gfx::EffectScene* Effects() const noexcept { return m_effects.get(); }

        //! 重ね描きと UI が全画面の色と画面の矩形を描く先。装置未設定の間は nullptr
        [[nodiscard]] Gfx::ScreenOverlay* Overlay() const noexcept { return m_overlay.get(); }

        //! 描画の時間とエフェクトの世界を固定ステップの刻み幅ぶん進める。EffectScene が無ければ時間だけ進める
        void OnTick();

        //! 1 フレームで描くビュー列を差す。空なら現描画先へ実カメラの視点で 1 回だけ描く
        //! 空でない間は各ビューを順に bind して描き分ける。差すのは Editor だけで、出荷では常に空
        void SetSceneViews(std::vector<SceneView> views) noexcept { m_sceneViews = std::move(views); }

        //! 次に描くフレームから使う名前の無いシーンの値の index 番目を書く。ほかの番号の値は変えない。index は欄の数より小さい事
        //! 意味は書く物と、シェーダの口のファイル SurfaceHooks.hlsli が決める
        void SetSceneValue(std::size_t index, const Vector4& value) noexcept;
        //! 描く時に使う名前の無いシーンの値。書かれていなければ全部 0
        [[nodiscard]] const std::array<Vector4, 2>& SceneValuesShown() const noexcept { return m_sceneValues; }

        //! 次に描くフレームから使う歪みの輪を書く。押し 0 で消える
        void SetDistortionRing(const Gfx::DistortionRing& ring) noexcept { m_distortionRing = ring; }
        //! 描く時に使う歪みの輪
        [[nodiscard]] const Gfx::DistortionRing& DistortionRingShown() const noexcept { return m_distortionRing; }

        //! 体の型に写す Model を差し替える。空なら型を描かない
        //! 型に写すのは登録の組が有効な Model の体だけ。登録を外した Model は、ここからも外す
        //! @param[in] models 型に写す Model、非所有
        void SetBodyMask(std::vector<const Model*> models) noexcept { m_bodyMask = std::move(models); }
        //! ビュー列の index 番目に、今のフレームで描いた体の型を返す
        //! @return 1 色 8 ビットの型。未描画なら空
        [[nodiscard]] const Gfx::D3DTexture* BodyMaskShown(std::size_t viewIndex) const noexcept;

        //! Model を、持ち主の根 root と組にして入れる。組は有効で入る。二重登録は無視する
        //! 呼ぶのは補助関数 InitDraw だけ。model と root は外すまで生きていること
        void RegisterModel(Model* model, const Transform* root);
        //! Model の組を外す。入れていない物は何もしない
        void UnregisterModel(const Model* model) noexcept;
        // 組の有効と無効を書く関数は、この 4 つと CameraManager::SetVirtualCameraEnabled で 5 つある。
        // 組の型と、無効の時に止める物が組ごとに違うので、1 つの関数にしない
        //! Model の組の有効と無効を書く。無効の組は描かず、箱も読まない。入れていない物は何もしない
        void SetModelEnabled(const Model* model, bool enabled) noexcept;

        //! Shadow を、持ち主の根 root と組にして入れる。組は有効で入る。二重登録は無視する
        //! 呼ぶのは補助関数 InitDraw だけ。shadow と root は外すまで生きていること
        void RegisterShadow(Shadow* shadow, const Transform* root);
        //! Shadow の組を外す。入れていない物は何もしない
        void UnregisterShadow(const Shadow* shadow) noexcept;
        //! Shadow の組の有効と無効を書く。無効の組は描かず、箱も読まない。入れていない物は何もしない
        void SetShadowEnabled(const Shadow* shadow, bool enabled) noexcept;

        //! 重ね描きを入れる。組は有効で入る。二重登録は無視する
        //! 呼ぶのは補助関数 InitOverlay だけ。並びは OverlayOrder 昇順に保たれ、同値なら後から入れた方が後ろになる
        void RegisterOverlay(IOverlay* overlay);
        //! 重ね描きの組を外す。入れていない物は何もしない
        void UnregisterOverlay(const IOverlay* overlay) noexcept;
        //! 重ね描きの組の有効と無効を書く。無効の組は描かない。入れていない物は何もしない
        void SetOverlayEnabled(const IOverlay* overlay, bool enabled) noexcept;

        //! 平行光を、向きを出す持ち主の根 root と組にして入れる。組は有効で入る。二重登録は無視する
        //! 呼ぶのは補助関数 InitLight だけ。並びは入れた順で、ResolveSceneSettings はこの順に読む
        void RegisterLight(DirectionalLight* light, const Transform* root);
        //! 平行光の組を外す。入れていない物は何もしない
        void UnregisterLight(const DirectionalLight* light) noexcept;
        //! 平行光の組の有効と無効を書く。無効の組は ResolveSceneSettings が読まない。入れていない物は何もしない
        void SetLightEnabled(const DirectionalLight* light, bool enabled) noexcept;

        //! 登録簿に入っている数。無効の組も数える
        [[nodiscard]] std::size_t ModelCount() const noexcept { return m_models.size(); }
        [[nodiscard]] std::size_t ShadowCount() const noexcept { return m_shadows.size(); }
        [[nodiscard]] std::size_t OverlayCount() const noexcept { return m_overlays.size(); }
        [[nodiscard]] std::size_t LightCount() const noexcept { return m_lights.size(); }

        //! 登録簿のうち有効な組の数
        [[nodiscard]] std::size_t EnabledModelCount() const noexcept;
        [[nodiscard]] std::size_t EnabledShadowCount() const noexcept;
        [[nodiscard]] std::size_t EnabledOverlayCount() const noexcept;
        [[nodiscard]] std::size_t EnabledLightCount() const noexcept;

        //! pass の段で描く DrawItem を、描く順に out へ積む。out は空にせず後ろへ足す
        //! 有効な組だけを、今の固定フレームの根で箱を読んで視錐台で絞る。Model の体と写しと Shadow を 1 つの列にする
        //! Transparent の段は、中心が context.cameraPosition から遠い順、同じなら優先度の小さい順、それも同じなら登録の順
        //! ほかの段は登録の順。DrawItem の行列は context.alpha で補間した根から作る
        void CollectPass(const Gfx::RenderContext& context, Gfx::RenderPass pass, std::vector<Gfx::DrawItem>& out);

        //! ビューを順に描く。未指定なら現在の描画先、装置なしなら描画しない
        //! カメラの姿勢は呼ぶ前に確定する。alpha は固定更新間の割合 0〜1
        //! skyboxPath は ContentRoot 内の相対パス。空なら空を描かない
        void Render(SceneCamera& camera,
                    UIDirector& ui,
                    const Gfx::ScreenFade& fade,
                    std::string_view skyboxPath,
                    float alpha);

        //! Opaque の段を CollectPass の順に描く
        void DrawOpaque(const Gfx::RenderContext& context);
        //! AfterSky の段を、画面の深度のコピーと空のキューブマップを結んだ間だけ描く。描く物が無ければ深度を写さない
        void DrawAfterSky(const Gfx::RenderContext& context);
        //! Transparent の段を CollectPass の順に描く
        void DrawTransparent(const Gfx::RenderContext& context);
        //! 有効な組の重ね描きを OverlayOrder 昇順で描画し、その上へ ui の画面を DrawOrder 昇順で描く
        //! 最後にシーン全体の暗転 fade を重ねる
        void DrawOverlays(const Gfx::RenderContext& context, UIDirector& ui, const Gfx::ScreenFade& fade);

        //! プロジェクト既定値からシーンの照明を作る。有効な平行光があれば照明を上書きする
        //! 登録が無ければプロジェクト既定値がそのまま残る
        [[nodiscard]] Gfx::RenderSettings ResolveSceneSettings(const Gfx::RenderSettings& projectDefaults);

    private:
        //! ビューごとの画面の段。ビューごとに描画先の大きさが違うので分けて持つ
        struct ViewPasses
        {
            std::unique_ptr<Gfx::Bloom> bloom;
            std::unique_ptr<Gfx::ScreenPasses> screen;
        };

        //! 1 ビュー分のシーンを描き、その上へデバッグ描画と OverlayRenderer の重ね描きを出す
        //! デバッグ描画は、この固定ステップの図形、view.drawShapes が積んだ図形の順に描く
        //! view.viewPose が空なら実カメラで描く
        void RenderViewWithOverlays(SceneCamera& camera,
                                    UIDirector& ui,
                                    const Gfx::ScreenFade& fade,
                                    std::string_view skyboxPath,
                                    const SceneView& view,
                                    float alpha,
                                    ViewPasses& passes);

        //! 不透明→空→半透明→エフェクトの順に 1 ビュー分を bloom の描画先へ描く
        //! にじみを足して今の描画先へ書き戻し、画面の段を掛ける
        //! 組んだ RenderContext を返す。viewOverride が空なら実カメラで描く
        //! 物の行列・カメラの位置・空・エフェクトは、このビューで決めた 1 つのカメラから引く
        [[nodiscard]] Gfx::RenderContext RenderWorld(SceneCamera& camera,
                                                         std::string_view skyboxPath,
                                                         const std::optional<CameraPose>& viewOverride,
                                                         float alpha,
                                                         ViewPasses& passes);

        //! ビュー列の index 番目に使う画面の段。無ければ作る
        [[nodiscard]] ViewPasses& PassesForView(std::size_t index);

        //! 体の型に写す描く物を、世界の深度で隠して型へ描く。写す物が無ければ型を描かない
        void DrawBodyMask(const Gfx::RenderContext& context, Gfx::ScreenPasses& screen);

        //! 描く部品と、描く時に渡す持ち主の根の組。指す物は全部非所有
        template <typename Part>
        struct DrawEntry
        {
            Part* part = nullptr;
            const Transform* root = nullptr;
            uint64 order = 0;    // 型をまたいだ登録の通し番号。半透明の段で同じ距離と優先度の物をこの順に描く
            bool enabled = true; // 持ち主が出ているか。正はこの値だけ
        };
        std::vector<DrawEntry<Model>> m_models;   // 登録順
        std::vector<DrawEntry<Shadow>> m_shadows; // 登録順
        uint64 m_nextOrder = 0;                   // 次に入れる組の通し番号

        //! 視錐台を通った描く物 1 つ。どの一覧の何番目の何を描くかと、並べる値
        struct VisibleDraw
        {
            enum class Kind : uint8
            {
                ModelBody,
                ModelCopies,
                Shadow,
            };
            Kind kind = Kind::ModelBody;
            std::size_t index = 0; // Kind の一覧の番号
            Vector3 sortCenter{};
            int sortPriority = 0;
            uint64 order = 0;
        };
        std::vector<VisibleDraw> m_visibleScratch; // CollectPass が使う作業用の並び
        std::vector<Gfx::DrawItem> m_drawScratch;  // DrawOpaque などが積んで発行する作業用の並び

        //! pass の段の有効な組のうち視錐台を通った物を m_visibleScratch に、CollectPass の順に並べる
        void GatherVisible(const Gfx::RenderContext& context, Gfx::RenderPass pass);
        //! m_drawScratch を順に context.device へ発行する。装置が無ければ何もしない
        void IssueDrawScratch(const Gfx::RenderContext& context);

        //! 重ね描きの組。指す物は非所有
        struct OverlayEntry
        {
            IOverlay* overlay = nullptr;
            bool enabled = true; // 持ち主が出ているか。正はこの値だけ
        };
        std::vector<OverlayEntry> m_overlays; // OverlayOrder 昇順

        //! 平行光と、向きを出す持ち主の根の組。指す物は非所有
        struct LightEntry
        {
            DirectionalLight* light = nullptr;
            const Transform* root = nullptr;
            bool enabled = true; // 持ち主が出ているか。正はこの値だけ
        };
        std::vector<LightEntry> m_lights; // 入れた順

        bool m_warnedZeroLightDirection = false; // 平行光の向きが 0 の警告を出したか
        Gfx::D3DDevice* m_device = nullptr; // 装置、非所有。未設定なら描かない

        std::vector<SceneView> m_sceneViews; // 描くビュー列。空なら現描画先へ 1 回だけ描く

        Gfx::DebugShapes m_viewShapes; // ビューの図形の使い捨ての溜め場。1 ビューを描く間だけ中身がある
        // 開発用の図形を描く段。装置を差す度に作り直す。装置未設定の間と出荷では空
        std::unique_ptr<Gfx::DebugShapePass> m_debugShapes;

        std::unique_ptr<Gfx::EffectScene> m_effects; // エフェクトの再生と描画。装置未設定の間は空

        // 描く段の組み込みのシェーダー。装置を差した時に AssetManager から引き、指すだけで持たない
        Gfx::BuiltinShaders m_shaders{};
        std::unique_ptr<Gfx::ScreenOverlay> m_overlay; // 全画面の色と画面の矩形。装置未設定の間は空
        std::unique_ptr<Gfx::Skybox> m_skybox;         // 空。装置未設定の間は空
        std::unique_ptr<Gfx::SceneDepthCopy> m_afterSkyDepth; // AfterSky の段が読む深度。装置未設定の間は空
        bool m_afterSkyDepthFailureLogged = false;
        float m_sceneTimeSeconds = 0.0f; // 固定更新の経過秒。ゲームを止めている間は進まない

        // 光のにじみと画面の段。ビュー列と同じ並び。装置を差し替えると畳み、描く時に作る
        std::vector<ViewPasses> m_viewPasses;

        std::array<Vector4, 2> m_sceneValues{};     // 描く名前の無いシーンの値。SetSceneValue で書いた値
        Gfx::DistortionRing m_distortionRing{};     // 描く歪みの輪。SetDistortionRing で書いた値
        std::vector<const Model*> m_bodyMask;       // 体の型に写す Model、非所有
        std::vector<Gfx::DrawItem> m_bodyMaskItems; // 体の型に描く物。描くたびに組み直す
    };
} // namespace NS
