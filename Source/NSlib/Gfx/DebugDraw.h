#pragma once

// 線分と AABB / OBB / 球 / カプセルの枠線、半透明の三角形を溜めて、まとめて描くデバッグ描画

#include "NSlib/Core/AABB.h"
#include "NSlib/Core/Math.h"
#include "NSlib/Core/OBB.h"
#include "NSlib/Core/Sphere.h"

#include <cstddef>
#include <vector>

namespace NS
{
    namespace Gfx
    {
        class Renderer;

        //! 描画しても消えないデバッグ図形の記録。破棄は Clear で行う
        //! 線は 4096 頂点、面は 12960 三角形を超えると古いものから捨てる
        class DebugShapes
        {
        public:
            //! 2点間に線分を追加する
            void Line(const Vector3& a, const Vector3& b, const Color& color);

            //! AABB の枠線を追加する
            void AABB(const NS::AABB& box, const Color& color);

            //! OBB の枠線を追加する
            void OBB(const NS::OBB& obb, const Color& color);

            //! @brief 球の枠線を追加する
            //! @details 座標面ごとの大円を 3 本描く。どの向きから見ても 1 本は線に潰れないので半径が読める
            void Sphere(const NS::Sphere& sphere, const Color& color);

            //! @brief 円の枠線を追加する
            //! @param[in] center 円の中心の座標
            //! @param[in] u 円の平面を張る 1 本目の軸。長さが半径
            //! @param[in] v u と直交する 2 本目の軸。長さは u と揃える
            void Circle(const Vector3& center, const Vector3& u, const Vector3& v, const Color& color);

            //! カプセルの円柱部分の枠線。端の半球の弧は描かない
            //! base は中心、axis は中心から半球中心へのベクトル
            void Capsule(const Vector3& base, const Vector3& axis, float radius, const Color& color);

            //! @brief 半透明の三角形を追加する
            //! @details 線より先に、裏表の両方を深度を見ずに描く。手前の物にも隠れない。透け具合は色のアルファで決める
            //! @param[in] color 描画色。アルファが不透明度
            void Triangle(const Vector3& a, const Vector3& b, const Vector3& c, const Color& color);

            //! @brief 溜めた図形を描く。溜めた図形は残る
            //! @details 面を先に、線を後に描く。空なら何もしない。GPU の準備に失敗した時と、
            //! renderer の Shaders に debug_line のシェーダーが無い時は何も描かない
            //! @param[in,out] renderer 描画コマンドの発行先
            //! @param[in] viewProjection ビュー・プロジェクション行列
            void Draw(Renderer& renderer, const Matrix& viewProjection) const noexcept;

            //! 溜めた線と面を全て捨てる
            void Clear() noexcept;

            //! 溜めた線の頂点の総数を返す
            [[nodiscard]] std::size_t VertexCount() const noexcept { return m_lines.size(); }

            //! 溜めた面の頂点の総数を返す
            [[nodiscard]] std::size_t FaceVertexCount() const noexcept { return m_faces.size(); }

        private:
            //! POSITION(12) + COLOR(16) の 28 バイト。頂点バッファの 1 頂点と同じ並び
            struct Vertex
            {
                Vector3 position;
                Color color;
            };

            std::vector<Vertex> m_lines; // 線分の頂点。2 つで 1 本
            std::vector<Vertex> m_faces; // 三角形の頂点。3 つで 1 枚
        };
    } // namespace Gfx
} // namespace NS

//! この固定ステップの図形を積む自由関数。積み先は 1 つの静的な DebugShapes
//! 捨てるのは Scene で、段を回す歩の頭・世界の組み直し・終わりの 3 か所。描いても捨てないので次の歩まで描かれ続ける
namespace NS
{
    namespace Gfx
    {
        namespace DebugDraw
        {
            //! @brief 2点間に線分を追加する
            void Line(const Vector3& a, const Vector3& b, const Color& color) noexcept;

            //! @brief AABB の枠線を追加する
            void AABB(const NS::AABB& box, const Color& color) noexcept;

            //! @brief OBB の枠線を追加する
            void OBB(const NS::OBB& obb, const Color& color) noexcept;

            //! @brief 球の枠線を追加する
            //! @details 座標面ごとの大円を 3 本描く。どの向きから見ても 1 本は線に潰れないので半径が読める
            void Sphere(const NS::Sphere& sphere, const Color& color) noexcept;

            //! @brief 円の枠線を追加する
            //! @param[in] center 円の中心の座標
            //! @param[in] u 円の平面を張る 1 本目の軸。長さが半径
            //! @param[in] v u と直交する 2 本目の軸。長さは u と揃える
            void Circle(const Vector3& center,
                        const Vector3& u,
                        const Vector3& v,
                        const Color& color) noexcept;

            //! カプセルの円柱部分の枠線。端の半球の弧は描かない
            //! base は中心、axis は中心から半球中心へのベクトル
            void Capsule(const Vector3& base,
                         const Vector3& axis,
                         float radius,
                         const Color& color) noexcept;

            //! @brief 前の固定ステップで積んだ図形を捨てる
            //! @details 固定ステップの頭で呼ぶ。描画までに固定ステップが複数進んでも、最後の 1 回で積んだ図形だけが残る
            void BeginStep() noexcept;

            //! @brief この固定ステップの図形を描く。溜めた図形は残る
            //! @details ビューを何枚描いても、歩の無いフレームでも同じ図形が出る
            //! @param[in,out] renderer 描画コマンドの発行先
            //! @param[in] viewProjection ビュー・プロジェクション行列
            void Draw(Renderer& renderer, const Matrix& viewProjection) noexcept;

            //! 蓄積した頂点を全て捨てる
            void Clear() noexcept;

            //! 現在バッファに蓄積されている頂点の総数を返す
            [[nodiscard]] std::size_t VertexCount() noexcept;

            //! @brief 半透明の三角形を追加する
            //! @details 線より先に、裏表の両方を深度を見ずに描く。手前の物にも隠れない。透け具合は色のアルファで決める
            //! 上限の 12960 枚を超える時は最も古い三角形を捨てる
            //! @param[in] color 描画色。アルファが不透明度
            void Triangle(const Vector3& a,
                          const Vector3& b,
                          const Vector3& c,
                          const Color& color) noexcept;

            //! 現在バッファに蓄積されている三角形の頂点の総数を返す
            [[nodiscard]] std::size_t FaceVertexCount() noexcept;
        } // namespace DebugDraw
    } // namespace Gfx
} // namespace NS
