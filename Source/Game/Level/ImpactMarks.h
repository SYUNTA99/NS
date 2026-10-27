#pragma once

#include "NSlib/Core/AABB.h"
#include "NSlib/Core/Math.h"
#include "NSlib/CoreObject/IRenderable.h"
#include "NSlib/CoreObject/Object.h"
#include "NSlib/CoreObject/Reflection/AssetField.h"

#include <cstddef>
#include <vector>

namespace GL
{
    namespace Level
    {
        //! 置物が床に残す半透明の跡。決まった数を持ち、時間で縮めて消す
        //! 跡は動かないので補間せず、出した位置にそのまま描く。描くのは持ち主が出ている間だけ
        class ImpactMarks : public NS::Object, public NS::IRenderable
        {
        public:
            //! 板とマテリアルの欄は、作る時に既定の名前の素材を AssetManager::Get() から引く
            ImpactMarks();

            //! position に跡を 1 つ出す
            //! 跡の数の欄が埋まっていれば一番古い跡を置き換える。欄が 0 以下なら出さない
            void Spawn(const NS::Vector3& position);
            //! 出ている跡を dt 秒進めて縮め、寿命を過ぎた跡を消す。持ち主の見た目の段が呼ぶ
            void Step(float dt) noexcept;

            //! 出ている跡の数
            [[nodiscard]] std::size_t LiveCount() const noexcept;
            //! 出ている跡の描く世界行列。並びは置き場の順で、出した順ではない
            [[nodiscard]] std::vector<NS::Matrix> MarkWorldMatrices() const;
            [[nodiscard]] int Capacity() const noexcept { return m_capacity; }
            [[nodiscard]] float LifeSeconds() const noexcept { return m_lifeSeconds; }

            void Collect(const NS::Gfx::RenderContext& context,
                         const NS::RenderSource& source,
                         std::vector<NS::Gfx::DrawItem>& out) override;
            //! 出ている跡を全部包む箱。跡が無ければ持ち主の根の位置の大きさ 0 の箱
            [[nodiscard]] NS::AABB WorldBounds(const NS::RenderSource& source) const noexcept override;
            [[nodiscard]] NS::RenderBucket Bucket() const noexcept override { return NS::RenderBucket::Transparent; }
            //! 一番新しい跡の位置。跡が無ければ持ち主の根の位置
            [[nodiscard]] NS::Vector3 SortCenter(const NS::RenderSource& source) const noexcept override;

            NS_REFLECT_BEGIN(ImpactMarks, NS::Object)
            NS_FIELD(m_mesh, "メッシュ")
            NS_FIELD(m_material, "マテリアル")
            NS_FIELD(m_capacity, "跡の数")
            NS_FIELD(m_diameter, "出た直後の大きさ")
            NS_FIELD(m_lifeSeconds, "消えるまでの秒")
            NS_FIELD(m_opacity, "不透明度")
            NS_REFLECT_END()

        private:
            struct Mark
            {
                NS::Vector3 position{};
                float age = 0.0f; // 出てからの経過秒
                bool live = false;
            };

            // 寿命に対する経過から、今の水平の大きさを返す
            [[nodiscard]] float SizeOf(const Mark& mark) const noexcept;

            // 素材は AssetManager が持つ。欄は nullptr にならない
            NS::Mesh* m_mesh = nullptr;
            NS::Material* m_material = nullptr;
            int m_capacity = 4;
            float m_diameter = 1.5f; // 単位は m
            float m_lifeSeconds = 6.0f;
            float m_opacity = 1.0f; // 0..1
            std::vector<Mark> m_marks;
            std::size_t m_next = 0;            // 次に書く置き場
            std::size_t m_newest = 0; // 一番新しく書いた置き場
        };
    } // namespace Level
} // namespace GL
