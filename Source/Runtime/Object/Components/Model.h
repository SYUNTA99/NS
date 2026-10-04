#pragma once

#include "Runtime/Core/AABB.h"
#include "Runtime/Core/Math.h"
#include "Runtime/Graphics/DrawItem.h"
#include "Runtime/Object/Component.h"
#include "Runtime/Object/IRenderable.h"

#include <string>
#include <utility>
#include <vector>

namespace NS::Gfx
{
    class Mesh;
    class Material;
    class Buffer;
} // namespace NS::Gfx

namespace NS::Obj
{

    // 震えの定数の 16 byte 整列を値で抱えるため、想定どおりのパディングが入る。C4324 を黙らせる
#pragma warning(push)
#pragma warning(disable : 4324)
    //! @brief Mesh と Material を描く Component
    //! @details Collect が DrawWorldMatrix(context.alpha) を FrameCB へ詰めた DrawItem を積む
    //! 固定ステップの物理結果を、可変フレームレートでなめらかに補間して描く
    //! 描く時だけの局所の回転を持ち、根の行列より先に掛ける。根の Transform は書き換えない
    //! 描く時だけの世界の軸の倍率も持ち、描く形の下端の真ん中を中心に掛ける
    //! 描く時だけの世界のずれと震えも持つ。どちらも補間せず、ずれは倍率の後に足し、震えは頂点のシェーダーへ渡す
    class Model : public Component, public IRenderable
    {
    public:
        //! Mesh / Material は非所有の生ポインタ。空で作り、後から SetMesh / SetMaterial で入れる
        //! 寿命は AssetManager 等の所有側が保証する
        Model() noexcept = default;

        //! Material を共有したまま配置物ごとに変える個体色。lighting とは別系統
        void SetBaseColor(const NS::Core::Vector3& color) noexcept { m_baseColor = color; }

        //! 描画に使う Material を差し替える。material=nullptr で Collect は何も積まなくなる
        //! bucket は Bucket() が Material::Blend() から都度判定するため opaque↔transparent も即反映される
        void SetMaterial(NS::Gfx::Material* material) noexcept { m_material = material; }
        //! 現在の Material で非所有。未設定なら nullptr
        [[nodiscard]] NS::Gfx::Material* GetMaterial() const noexcept { return m_material; }

        //! 描画に使う Mesh を差し替える。mesh=nullptr で Collect は何も積まなくなる
        //! リフレクションでは Mesh を運べないので、MeshRef から解決したものをここで差す
        void SetMesh(NS::Gfx::Mesh* mesh) noexcept { m_mesh = mesh; }
        //! build 時に解決された実体 Mesh を返す。未解決なら nullptr
        [[nodiscard]] const NS::Gfx::Mesh* GetMesh() const noexcept { return m_mesh; }

        //! 描くメッシュの参照。builtin 名または ContentRoot 配下の相対パス。空 / 解決不可なら構築側が既定にする
        [[nodiscard]] const std::string& MeshRef() const noexcept { return m_meshRef; }
        //! build 時にこの文字列から mesh を解決する
        void SetMeshRef(std::string ref) noexcept { m_meshRef = std::move(ref); }

        //! 描画 material の参照。player / water / shadow といった共有 material 名または .mat 相対パス
        //! 空・解決不可は ResolveAssets が既定 material にする
        [[nodiscard]] const std::string& MaterialRef() const noexcept { return m_materialRef; }
        //! build 時にこの文字列から material を解決する
        void SetMaterialRef(std::string ref) noexcept { m_materialRef = std::move(ref); }

        //! skinned のボーンパレット等、オブジェクト単位の追加 VS 定数を差す。同じ object 上の別 component が OnStart
        //! で配線する cpuData 非 null なら描画側が毎描画 buffer へアップロードしてから bind する
        void SetPerObjectVsConstant(const NS::Gfx::Buffer* cb,
                                    const void* cpuData,
                                    std::size_t cpuDataSize,
                                    unsigned slot) noexcept;

        //! skinned など mesh 固定の LocalBounds では現在ポーズを包めない時に、同じ object の component が毎フレーム
        //! 現在ポーズの局所境界を差す。差された間は WorldBounds がこれを world 変換して使う
        void SetLocalBoundsOverride(const NS::Core::AABB& localBounds) noexcept
        {
            m_localBoundsOverride = localBounds;
            m_hasLocalBoundsOverride = true;
        }

        //! @brief 描く時だけの局所の回転を書く。根の行列より先に掛かるので、根のスケールは局所の回転と一緒に回らない
        //! @details 今のフレームの値だけを書き、前のフレームの値は固定ステップの頭で Snapshot が控える。保存はしない
        //! 固定ステップの中で書いた値は補間される。補間させない時は SnapLocalRotation で書く
        void SetLocalRotation(const NS::Core::Quaternion& rotation) noexcept { m_localRotation = rotation; }
        //! @brief 今と前のフレームの局所の回転を同じ値にする
        //! @details 補間せずにこの姿勢で描く。mesh を差し替えたフレームに、差し替える前の回転から補間されないようにする
        void SnapLocalRotation(const NS::Core::Quaternion& rotation) noexcept
        {
            m_localRotation = rotation;
            m_previousLocalRotation = rotation;
        }
        //! 今のフレームの局所の回転を返す。書かれていなければ単位回転
        [[nodiscard]] const NS::Core::Quaternion& LocalRotation() const noexcept { return m_localRotation; }

        //! @brief 描く時だけ世界の x / y / z に掛ける倍率を書く
        //! @details 局所の回転と根の行列を掛けた後に、世界の軸で掛ける。中心は描く形の下端の真ん中で、
        //! 縦に潰しても下端の高さは変わらない。描く形は、差された局所の境界か mesh の局所の境界を
        //! 根の補間 world 行列で包んだ箱で、局所の回転は含めない。どちらも無ければ根の原点を中心にする
        //! 保存はせず、根の Transform と当たりは変えない
        //! 今のフレームの値だけを書き、前のフレームの値は固定ステップの頭で Snapshot が控える
        //! 固定ステップの中で書いた値は補間される。補間させない時は SnapDrawScale で書く
        //! 有限の正でない成分 (非数・無限大・0 以下) を含む倍率は何も変えない
        //! @param[in] scale 世界の軸ごとの倍率。(1, 1, 1) で倍率の無い形
        //! @return 成分が全部有限の正で書いた場合 true、それ以外の場合は false
        [[nodiscard]] bool SetDrawScale(const NS::Core::Vector3& scale) noexcept;
        //! @brief 今と前のフレームの描く時だけの倍率を同じ値にする
        //! @details 補間せずにこの倍率で描く。書くのが Snapshot の前でも後でも、Snapshot が回らなくても変わらない
        //! 有限の正でない成分 (非数・無限大・0 以下) を含む倍率は何も変えない
        //! @param[in] scale 世界の軸ごとの倍率。(1, 1, 1) で倍率の無い形
        //! @return 成分が全部有限の正で書いた場合 true、それ以外の場合は false
        [[nodiscard]] bool SnapDrawScale(const NS::Core::Vector3& scale) noexcept;
        //! 今のフレームの描く時だけの倍率を返す。書かれていなければ (1, 1, 1)
        [[nodiscard]] const NS::Core::Vector3& DrawScale() const noexcept { return m_drawScale; }

        //! @brief 描く時だけ世界で足すずれを書く
        //! @details 局所の回転・根の行列・描く時だけの倍率を掛けた後に、世界で足す。補間しないので、
        //! 1 フレームごとに向きが入れ替わる揺れが描画の回数に依らず同じ形で見える。保存はせず、根の Transform と
        //! 当たりは変えない。有限でない成分を含むずれは何も変えない
        //! @param[in] offset 世界の長さのずれ (m)。(0, 0, 0) でずれの無い形
        //! @return 成分が全部有限で書いた場合 true、それ以外の場合は false
        [[nodiscard]] bool SetDrawOffset(const NS::Core::Vector3& offset) noexcept;
        //! 描く時だけのずれを返す。書かれていなければ (0, 0, 0)
        [[nodiscard]] const NS::Core::Vector3& DrawOffset() const noexcept { return m_drawOffset; }

        //! @brief 描く時だけの震えを書く
        //! @details Collect がそのまま頂点のシェーダーへ渡す。補間しないので、経過のフレーム数は描画の回数に依らない
        //! 保存はせず、根の Transform と当たりは変えない。有限でない欄を含む震えは何も変えない
        //! @param[in] tremor 震え。振れ幅 0 で震えない
        //! @return 欄が全部有限で書いた場合 true、それ以外の場合は false
        [[nodiscard]] bool SetTremor(const NS::Gfx::TremorCB& tremor) noexcept;
        //! 描く時だけの震えを返す。書かれていなければ振れ幅 0
        [[nodiscard]] const NS::Gfx::TremorCB& Tremor() const noexcept { return m_tremor; }

        //! @brief 描く時だけの残像の離れを書く
        //! @details 描く形を根から +離れ と −離れ だけずらした所へ、半透明の写しを 1 つずつ描く。描く時だけのずれは
        //! 写しに足さないので、体が片側へ振れても写しは根を挟んで左右に残る。写しの不透明度は共有の water の
        //! material が決める。補間しない。保存はせず、根の Transform と当たりは変えない。有限でない成分を含む離れは
        //! 何も変えない
        //! @param[in] spread 世界の長さの離れ (m)。(0, 0, 0) で写しを描かない
        //! @return 成分が全部有限で書いた場合 true、それ以外の場合は false
        [[nodiscard]] bool SetGhostSpread(const NS::Core::Vector3& spread) noexcept;
        //! 描く時だけの残像の離れを返す。書かれていなければ (0, 0, 0)
        [[nodiscard]] const NS::Core::Vector3& GhostSpread() const noexcept { return m_ghostSpread; }
        //! @brief 残像 1 つの world 行列を返す
        //! @details DrawWorldMatrix の描く時だけのずれを、side × 離れ に置き換えた行列
        //! @param[in] alpha 前の固定フレームから今の固定フレームまでの補間の割合 0..1
        //! @param[in] side 離れに掛ける向き。+1 か −1
        [[nodiscard]] NS::Core::Matrix GhostWorldMatrix(float alpha, float side) const noexcept;

        //! @brief 描く world 行列を返す
        //! @details 前と今の局所の回転を alpha で補間した行列を、根の補間 world 行列の前に掛ける
        //! 前と今の描く時だけの倍率を alpha で補間し、描く形の下端の真ん中を中心に世界の軸で掛ける
        //! 補間した倍率が (1, 1, 1) の時は掛けない。描く時だけのずれは最後に足す
        //! 持ち主が無い時は局所の回転の行列だけを返す
        //! @param[in] alpha 前の固定フレームから今の固定フレームまでの補間の割合 0..1
        [[nodiscard]] NS::Core::Matrix DrawWorldMatrix(float alpha) const noexcept;

        //! DrawWorldMatrix(context.alpha) を詰めた DrawItem を out に積む。IsActive()==false なら何も積まない
        void Collect(const NS::Gfx::RenderContext& context, std::vector<NS::Gfx::DrawItem>& out) override;

        //! Material の BlendMode から bucket を返し、Opaque 以外は Transparent。Material 不在は Opaque
        [[nodiscard]] RenderBucket Bucket() const noexcept override;
        //! Owner の world 行列の平行移動成分で半透明ソート用の中心
        [[nodiscard]] NS::Core::Vector3 SortCenter() const noexcept override;
        //! Material の renderPriority で距離同値時のタイブレークに使う
        [[nodiscard]] int SortPriority() const noexcept override;

        //! @brief 描く形の局所の境界を owner の world 行列で包んだワールド AABB を返す
        //! @details 局所の境界は差された境界を優先し、無ければ mesh の局所 AABB
        //! mesh か owner が無い時は、中心が原点で半分の幅が 1 の AABB
        //! 描く時だけの倍率がある間は、前と今の倍率の成分ごとの大きい方を、下端の真ん中を中心に掛ける
        //! 描く時だけのずれは中心へ足す
        [[nodiscard]] NS::Core::AABB WorldBounds() const noexcept override;

        //! OwningScene に self と残像を IRenderable として登録する。Owner/Scene が null なら何もしない
        void OnAppear() override { Model::OnStart(); }
        void OnKill() noexcept override { Model::OnEndPlay(); }
        void OnStart() override;
        //! Owner の OwningScene から self と残像を解除する。無効ポインタを残さないよう Scene 破棄前に呼ぶ
        void OnEndPlay() override;
        //! @brief 今の局所の回転と描く時だけの倍率を前のフレームの値として控える
        //! @details 持ち主の Actor の SnapshotForInterpolation が、根の Transform と一緒に固定ステップの頭で呼ぶ。
        //! 世界を止めている間も毎回呼ばれ、前と今が揃って補間が凍る。部品の 1 歩としては回らない
        void Snapshot() noexcept;

        //! meshRef / matRef の参照文字列から実体の Mesh / Material を引き当てる
        //! 共有 material 名を先に引き、外れたら .mat 相対パスとして読む。解決不可は cube と既定 material にする
        //! 残像には共有の water の material を当てる
        void ResolveAssets(AssetManager& assets) override;

        NS_REFLECT_BEGIN(Model, Component)
        NS_REFLECT_FIELD(m_baseColor, "基本色")
        NS_REFLECT_FIELD(m_meshRef, "メッシュ")
        NS_REFLECT_FIELD(m_materialRef, "マテリアル")
        NS_REFLECT_END()

    private:
        // 残像の写しを半透明の並びで描く。不透明の並びで描くと、後に描く空と物が写しを上書きする
        class Ghosts final : public IRenderable
        {
        public:
            explicit Ghosts(Model& model) noexcept : m_model(model) {}
            void Collect(const NS::Gfx::RenderContext& context, std::vector<NS::Gfx::DrawItem>& out) override;
            [[nodiscard]] NS::Core::AABB WorldBounds() const noexcept override;
            [[nodiscard]] RenderBucket Bucket() const noexcept override { return RenderBucket::Transparent; }
            [[nodiscard]] NS::Core::Vector3 SortCenter() const noexcept override { return m_model.SortCenter(); }

        private:
            Model& m_model;
        };

        // 描く world 行列。描く時だけのずれの代わりに offset を足す
        [[nodiscard]] NS::Core::Matrix DrawWorldMatrixWithOffset(float alpha,
                                                                 const NS::Core::Vector3& offset) const noexcept;
        // material と world 行列を詰めた DrawItem を作る。他の定数は形と描く設定から詰める
        [[nodiscard]] NS::Gfx::DrawItem MakeDrawItem(const NS::Gfx::RenderContext& context,
                                                     NS::Gfx::Material* material,
                                                     const NS::Core::Matrix& world) const noexcept;

        // 描く形の局所の境界。差された境界を先に、無ければ mesh の境界。どちらも無ければ nullptr
        [[nodiscard]] const NS::Core::AABB* DrawnLocalBounds() const noexcept;

        NS::Gfx::Mesh* m_mesh = nullptr;                 // 描画する Mesh (非所有)
        NS::Gfx::Material* m_material = nullptr;         // 描画に使う Material (非所有)
        NS::Core::Vector3 m_baseColor{1.0f, 1.0f, 1.0f}; // 個体色、lighting と別系統
        // 保存・編集される参照文字列。build 時に解決して m_mesh / m_material へ実体を当てる
        std::string m_meshRef{};
        std::string m_materialRef{};

        // オブジェクト単位の追加 VS 定数 (skinned のボーンパレット)。同じ object 上の別 component が
        // SetPerObjectVsConstant で差す
        const NS::Gfx::Buffer* m_perObjectVsCb = nullptr;
        const void* m_perObjectVsData = nullptr;
        std::size_t m_perObjectVsSize = 0;
        unsigned m_perObjectVsSlot = 1;

        // skinned の現在ポーズ境界。同じ object の SkeletalAnimation が毎フレーム差す
        NS::Core::AABB m_localBoundsOverride{};
        bool m_hasLocalBoundsOverride = false;

        // 描く時だけの局所の回転。同居する component が毎フレーム書き直すので保存しない
        NS::Core::Quaternion m_localRotation = NS::Core::Quaternion::Identity;
        NS::Core::Quaternion m_previousLocalRotation = NS::Core::Quaternion::Identity; // 前のフレームの値。補間の始点

        // 描く時だけの世界の軸の倍率。他の component が書き直すので保存しない
        NS::Core::Vector3 m_drawScale{1.0f, 1.0f, 1.0f};
        NS::Core::Vector3 m_previousDrawScale{1.0f, 1.0f, 1.0f}; // 前のフレームの値。補間の始点

        // 描く時だけの世界のずれ。他の component が書き直すので保存しない。補間しない
        NS::Core::Vector3 m_drawOffset{0.0f, 0.0f, 0.0f};
        // 描く時だけの震え。他の component が書き直すので保存しない。補間しない
        NS::Gfx::TremorCB m_tremor{};
        // 描く時だけの残像の離れ。他の component が書き直すので保存しない。補間しない
        NS::Core::Vector3 m_ghostSpread{0.0f, 0.0f, 0.0f};
        NS::Gfx::Material* m_ghostMaterial = nullptr; // 残像の共有 water material (非所有)
        Ghosts m_ghosts{*this};                       // 残像を描く物。Scene へは self と並べて登録する
    };
#pragma warning(pop)
} // namespace NS::Obj
