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
    //! @brief Mesh と Material を描く Component
    //! @details Collect が DrawWorldMatrix(context.alpha) を FrameCB へ詰めた DrawItem を積む
    //! 固定ステップの物理結果を、可変フレームレートでなめらかに補間して描く
    //! 描く時だけの局所の回転を持ち、根の行列より先に掛ける。根の Transform は書き換えない
    class MeshRenderer : public Component, public IRenderable
    {
    public:
        //! Mesh / Material は非所有の生ポインタ。空で作り、後から SetMesh / SetMaterial で入れる
        //! 寿命は AssetManager 等の所有側が保証する
        MeshRenderer() noexcept = default;

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
        //! @details 今のフレームの値だけを書き、前のフレームの値は OnUpdate が控える。保存はしない
        //! Update 帯の既定の優先度で回る OnUpdate より後に書くこと。先に書くと前のフレームの値と同じになり補間されない
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

        //! @brief 描く world 行列を返す
        //! @details 前と今の局所の回転を alpha で補間した行列を、根の補間 world 行列の前に掛ける
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

        //! mesh の局所 AABB を owner の world 行列で包んだワールド AABB。mesh / owner 不在なら原点の点
        [[nodiscard]] NS::Core::AABB WorldBounds() const noexcept override;

        //! OwningScene に self を IRenderable として登録する。Owner/Scene が null なら何もしない
        void OnStart() override;
        //! Owner の OwningScene から self を解除する。無効ポインタを残さないよう Scene 破棄前に呼ぶ
        void OnEndPlay() override;
        //! @brief 今の局所の回転を前のフレームの値として控える
        //! @details 局所の回転は、これより大きい優先度で書くこと。非活性の間は控えないので、活性に戻った
        //! 最初のフレームは止める前の値から補間される
        void OnUpdate() override;

        //! meshRef / matRef の参照文字列から実体の Mesh / Material を引き当てる
        //! 共有 material 名を先に引き、外れたら .mat 相対パスとして読む。解決不可は cube と既定 material にする
        void ResolveAssets(AssetManager& assets) override;

        NS_REFLECT_BEGIN(MeshRenderer, Component)
        NS_REFLECT_FIELD(m_baseColor, "基本色")
        NS_REFLECT_FIELD(m_meshRef, "メッシュ")
        NS_REFLECT_FIELD(m_materialRef, "マテリアル")
        NS_REFLECT_END()

    private:
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
    };
} // namespace NS::Obj
