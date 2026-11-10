#pragma once

#include "NSlib/Core/AABB.h"
#include "NSlib/Core/Interpolated.h"
#include "NSlib/Core/Math.h"
#include "NSlib/Core/Types.h"
#include "NSlib/CoreObject/Reflection/AssetField.h"
#include "NSlib/CoreObject/Object.h"
#include "NSlib/Gfx/D3DShader.h"
#include "NSlib/Gfx/Render/DrawItem.h"
#include "NSlib/Gfx/Render/RenderBounds.h"

#include <array>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace NS
{
    //! 共有モデルを半透明で描く一回分の指定。元のModelは変更しない
    struct ModelDraw
    {
        //! 描画姿勢を世界行列で固定する。指定中は根・局所変換・offsetを適用しない
        std::optional<Matrix> world;
        //! 世界座標の描画ずれを置き換える。省略時は元のModelのずれを使う
        std::optional<Vector3> offset;
        float opacity = 1.0f;
        bool sceneValues = true;  // 偽なら名前の無いシーンの値を全部 0 で渡す
        bool objectValues = true; // 偽なら名前の無い物ごとの値を全部 0 で渡す
    };

    namespace Gfx
    {
        class D3DBuffer;
        struct RenderContext;
    } // namespace Gfx
} // namespace NS

namespace NS
{
    // 震えの定数の 16 バイト整列を値で抱えるため、想定どおりのパディングが入る。C4324 を黙らせる
#pragma warning(push)
#pragma warning(disable : 4324)
    //! メッシュとマテリアルを描画する部品
    //! 描画用の姿勢変更は根の Transform と衝突形状に影響せず、保存されない。
    //! 根の行列は覚えず、描く物の管理クラスと持ち主のアクターが渡す
    //! 姿を読む元の Model を差した Model は自分の体を描かず、元の Model の写しだけを描く
    class Model : public Object
    {
    public:
        //! メッシュとマテリアルの欄は、作る時に既定の名前の素材を AssetManager::Get() から引く
        Model();

        //! マテリアルを共有したままアクターごとに変える色の上書き。空ならマテリアルの基本色で描く
        //! 光の色とは別に掛かる。透明度は使わず rgb だけを描く
        void SetColorOverride(const std::optional<Color>& color) noexcept { m_colorOverride = color; }
        [[nodiscard]] const std::optional<Color>& ColorOverride() const noexcept { return m_colorOverride; }
        //! 描く色。上書きが入っていればその色、空なら欄のマテリアルの Material::DrawColor。掛け合わせない
        [[nodiscard]] Color DrawColor() const noexcept;

        //! 欄を替えずに、描くメッシュを替える。保存する名前は欄のメッシュのまま
        //! nullptr で欄のメッシュに戻す。SceneActor::ReloadAssets も欄のメッシュに戻す
        void SetMesh(Mesh* mesh) noexcept { m_shownMesh = mesh; }
        //! メッシュの欄の Mesh。SetMesh で替えた描くメッシュは見ない。nullptr にならない
        [[nodiscard]] const Mesh* MeshField() const noexcept { return m_mesh; }
        //! 描くメッシュ。SetMesh で替えていればその物、無ければ欄のメッシュ
        [[nodiscard]] const Mesh* GetMesh() const noexcept;

        //! 描くメッシュの名前。組み込みの形の名前か、Content の下のパス。空・読めない時は何も描かない
        [[nodiscard]] const std::string& MeshRef() const noexcept { return AssetFieldName(m_mesh); }
        //! 名前のメッシュをその場で AssetManager::Get() から引き、欄に入れる
        void SetMeshRef(std::string_view ref);

        //! 欄のマテリアルの名前。.mat のパス。読めない時はピンクで描き、空の時は灰色の既定で描く
        [[nodiscard]] const std::string& MaterialRef() const noexcept { return AssetFieldName(m_material); }
        //! 名前のマテリアルをその場で AssetManager::Get() から引き、欄に入れる
        void SetMaterialRef(std::string_view ref);

        //! 個体ごとの頂点シェーダーの定数を設定する。同じ SceneActor の Animation がプレイの始まりに設定する
        //! cpuData があれば、描画側が毎回バッファーへ転送してから設定する
        void SetPerObjectVsConstant(const Gfx::D3DBuffer* cb,
                                    const void* cpuData,
                                    std::size_t cpuDataSize,
                                    uint slot) noexcept;

        //! skinned など mesh 固定の LocalBounds では現在ポーズを包めない時に、同じ object の部品が毎フレーム
        //! 現在ポーズの局所境界を差す。差された間は WorldBounds がこれを世界変換して使う
        void SetLocalBoundsOverride(const AABB& localBounds) noexcept
        {
            m_localBoundsOverride = localBounds;
            m_hasLocalBoundsOverride = true;
        }

        //! 根の行列の前に掛ける描画用の局所回転。固定更新間を補間する
        void SetLocalRotation(const Quaternion& rotation) noexcept { m_localRotation.Set(rotation); }
        //! 補間せずに局所回転を切り替える
        void SnapLocalRotation(const Quaternion& rotation) noexcept { m_localRotation.Teleport(rotation); }
        //! 今のフレームの局所の回転を返す。書かれていなければ単位回転
        [[nodiscard]] const Quaternion& LocalRotation() const noexcept { return m_localRotation.Current(); }

        //! 形の原点を中心に、局所の回転より先に掛ける倍率を設定する。補間しない
        //! 差したメッシュの大きさを合わせる倍率で、根の倍率と描く倍率とは別に掛かる
        //! 描く倍率の中心と描く形の箱は、この倍率を掛けた形から測る
        //! @return 全成分が有限の正値の場合 true、それ以外の場合は変更せず false
        [[nodiscard]] bool SetLocalScale(const Vector3& scale) noexcept;
        //! 局所の倍率を返す。書かれていなければ (1, 1, 1)
        [[nodiscard]] const Vector3& LocalScale() const noexcept { return m_localScale; }

        //! 形の下端中央を中心に世界の軸で掛ける、描く時だけの倍率を書く。固定更新の間を補間する
        //! SetDrawShapeScale で書いた倍率も掛けて描く
        //! @return 全成分が有限の正値の場合 true、それ以外の場合は変更せず false
        [[nodiscard]] bool SetDrawScale(const Vector3& scale) noexcept;
        //! 描く時だけの倍率を、補間せずに切り替える
        //! @return 全成分が有限の正値の場合 true、それ以外の場合は変更せず false
        bool SnapDrawScale(const Vector3& scale) noexcept;
        //! 描く姿勢へ足す世界のずれ (m) を書く。補間しない。当たりの見た目の走る物が毎フレーム書く
        //! @return 全成分が有限の場合 true、それ以外の場合は変更せず false
        [[nodiscard]] bool SetDrawOffset(const Vector3& offset) noexcept;
        //! SetDrawScale の倍率に掛ける、当たりの見た目の形の倍率を書く。補間する。描く倍率は 2 つの積
        //! @return 全成分が有限の正値の場合 true、それ以外の場合は変更せず false
        [[nodiscard]] bool SetDrawShapeScale(const Vector3& scale) noexcept;
        //! シェーダへ渡す、名前の無い物ごとの 4 つの値を書く。補間しない
        //! 意味は値を書く物と、シェーダの口のファイル SurfaceHooks.hlsli が決める。描く 1 回ごとに ObjectConstants::objectValues へ渡す
        void SetDrawValues(const std::array<Vector4, 4>& values) noexcept { m_drawValues = values; }
        //! SetDrawValues で書いた値。書かれていなければ全部 0
        [[nodiscard]] const std::array<Vector4, 4>& DrawValues() const noexcept { return m_drawValues; }
        //! 描く姿勢へ足す世界のずれ (m)。補間しない
        [[nodiscard]] const Vector3& GetDrawOffset() const noexcept { return m_drawOffset; }
        //! 今の固定フレームの描く時だけの倍率。SetDrawScale と SetDrawShapeScale で書いた倍率の積。
        //! どちらも無ければ (1, 1, 1)
        [[nodiscard]] const Vector3& GetDrawScale() const noexcept { return m_shownDrawScale.Current(); }

        //! 局所回転と描画倍率を補間した描画姿勢を返す。世界座標のずれは補間後に足す
        //! 描画倍率は形の下端中央を中心に世界の軸で掛ける。境界がなければ根を中心にする
        //! @param[in] root 持ち主の根の世界行列。alpha で補間した物を渡す
        //! @param[in] alpha 固定更新間の補間割合 0〜1
        [[nodiscard]] Matrix DrawWorldMatrix(const Matrix& root, float alpha) const noexcept;
        //! DrawWorldMatrix の描く時だけのずれを offset に置き換えた描画姿勢を返す
        [[nodiscard]] Matrix DrawWorldMatrixWithOffset(const Matrix& root, float alpha, const Vector3& offset) const noexcept;

        //! 描く形と欄のマテリアルの描く D3DMaterial が揃っている場合 true
        [[nodiscard]] bool CanDraw() const noexcept;
        //! 欄のマテリアルと DrawColor で、world に描く DrawItem を作る
        //! 混ぜ方はマテリアルの物。CanDraw が true の時だけ呼ぶ
        [[nodiscard]] Gfx::DrawItem MakeDrawItem(const Matrix& world) const noexcept;
        //! 描く形の局所の境界。差された境界を先に、無ければメッシュの境界。どちらも無ければ nullptr
        [[nodiscard]] const AABB* DrawnLocalBounds() const noexcept;

        //! 体を描く段と順と箱。root は持ち主の根の今の固定フレームの世界行列
        //! 段は、SetDrawsAfterSky の後は AfterSky、他は欄のマテリアルの混ぜ方が不透明なら Opaque、それ以外は Transparent
        //! 中心は根の位置、優先度は欄のマテリアルの renderPriority
        //! 箱は、SetNeverCulled の後は原点を中心に全域を覆い、他は描く形の局所の境界を根で包む。
        //! 描く倍率は前と今の大きい方で包み、描く時だけのずれを足す。描くメッシュが無ければ空の箱
        [[nodiscard]] Gfx::RenderBounds Bounds(const Matrix& root) const noexcept;
        //! DrawWorldMatrix(root, context.alpha) の体の DrawItem を out に積む。描けなければ何も積まない
        //! @param[in] root 持ち主の根の世界行列。context.alpha で補間した物を渡す
        void Collect(const Gfx::RenderContext& context, const Matrix& root, std::vector<Gfx::DrawItem>& out) const;

        //! 体と一緒に描く写しの列と、半透明の段で中心の距離が同じ時の優先度を書く。前の列は捨てる
        //! 写しはこの Model のメッシュ・マテリアル・骨格の定数・姿で、半透明で描く
        void SetCopies(std::span<const ModelDraw> copies, int priority);
        //! SetCopies で書いた写しの列。書かれていなければ空
        [[nodiscard]] std::span<const ModelDraw> Copies() const noexcept { return m_copies; }
        //! SetCopies で書いた優先度。書かれていなければ 0
        [[nodiscard]] int CopyPriority() const noexcept { return m_copyPriority; }
        //! 写しを描くか。写しの列が空でなければ真
        [[nodiscard]] bool DrawsCopies() const noexcept { return !m_copies.empty(); }
        //! 写しの組の段と順と箱。DrawsCopies が真の時だけ呼ぶ。段は Transparent、中心は根の位置、優先度は CopyPriority
        //! 箱は、体の箱と、写しごとの箱 (行列を控えた写しはその行列、ずれの写しはずれの分を広げた体の箱) を包む
        //! @param[in] root 持ち主の根の今の固定フレームの世界行列
        [[nodiscard]] Gfx::RenderBounds CopiesBounds(const Matrix& root) const noexcept;
        //! 写しごとに半透明の DrawItem を積む。骨格の姿は写しの行列に合わせない。DrawsCopies が真の時だけ呼ぶ
        //! @param[in] root 持ち主の根の世界行列。context.alpha で補間した物を渡す
        void CollectCopies(const Gfx::RenderContext& context, const Matrix& root, std::vector<Gfx::DrawItem>& out) const;

        //! 空の後の段で描く。画面の深度と空のキューブマップを読むシェーダーの物が呼ぶ
        void SetDrawsAfterSky() noexcept { m_drawsAfterSky = true; }

        //! 視錐台で間引かない。頂点シェーダーがメッシュの範囲の外へ動かす物が呼ぶ
        void SetNeverCulled() noexcept { m_neverCulled = true; }

        //! 固定更新の先頭で補間用の姿勢を控える。時間停止中も前後の値を揃える
        void Snapshot() noexcept;

        NS_REFLECT_BEGIN(Model, Object)
        NS_GLOBAL_FIELD(m_colorOverride, "基本色", "色の上書き")
        NS_GLOBAL_FIELD(m_mesh, "メッシュ")
        NS_GLOBAL_FIELD(m_material, "マテリアル")
        NS_REFLECT_END()

    private:
        // 体の箱。Bounds の箱の決まりで作る
        [[nodiscard]] AABB WorldBounds(const Matrix& root) const noexcept;
        // copies の写しごとに、この Model の姿で半透明の DrawItem を積む。描けなければ何も積まない
        void CollectDraws(const Gfx::RenderContext& context,
                          const Matrix& root,
                          std::span<const ModelDraw> copies,
                          std::vector<Gfx::DrawItem>& out) const;
        // 体の箱と copies の写しごとの箱を包む
        [[nodiscard]] AABB DrawsWorldBounds(const Matrix& root, std::span<const ModelDraw> copies) const noexcept;

        // 描く写しの列と優先度。写しを決めるアクターが毎ステップ書き直すので保存しない
        std::vector<ModelDraw> m_copies;
        int m_copyPriority = 0;

        // 素材は AssetManager が持つ。欄は nullptr にならない
        Mesh* m_mesh = nullptr;              // メッシュの欄。m_shownMesh が無い間はこれを描く
        Material* m_material = nullptr;
        Mesh* m_shownMesh = nullptr;         // 欄を替えずに描くメッシュ。保存しない
        std::optional<Color> m_colorOverride;
        bool m_drawsAfterSky = false; // 保存しない。決めるのはアクターのクラス
        bool m_neverCulled = false;   // 保存しない。決めるのはアクターのクラス

        // 追加 VS 定数。cpuData があれば描画ごとに転送する
        const Gfx::D3DBuffer* m_perObjectVsCb = nullptr;
        const void* m_perObjectVsData = nullptr;
        std::size_t m_perObjectVsSize = 0;
        uint m_perObjectVsSlot = Gfx::k_BonePaletteSlot;

        // Animation が現在ポーズから与える局所境界
        AABB m_localBoundsOverride{};
        bool m_hasLocalBoundsOverride = false;

        // 描く時だけの局所の回転。同居する部品が毎フレーム書き直すので保存しない
        Interpolated<Quaternion> m_localRotation{Quaternion::Identity};

        // 差したメッシュに合わせる局所の倍率。メッシュを差す部品が書くので保存しない
        Vector3 m_localScale{1.0f, 1.0f, 1.0f};

        // 描く時だけの姿勢。根の Transform と当たりには効かず、保存しない
        Vector3 m_drawOffset{0.0f, 0.0f, 0.0f};
        Vector3 m_drawScale{1.0f, 1.0f, 1.0f};      // SetDrawScale で書いた倍率
        Vector3 m_drawShapeScale{1.0f, 1.0f, 1.0f}; // SetDrawShapeScale で書いた倍率
        // 描く倍率 (2 つの倍率の積) の前と今。倍率を書くたびに今の値を積で書き直す
        Interpolated<Vector3> m_shownDrawScale{Vector3{1.0f, 1.0f, 1.0f}};
        std::array<Vector4, 4> m_drawValues{}; // シェーダへ渡す名前の無い物ごとの値
    };
#pragma warning(pop)
} // namespace NS
