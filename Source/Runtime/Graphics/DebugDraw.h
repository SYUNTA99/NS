#pragma once

// 線分と AABB / OBB / 球 / カプセルの枠線、半透明の三角形を溜めて、まとめて描くデバッグ描画

#include "Runtime/Core/AABB.h"
#include "Runtime/Core/Math.h"
#include "Runtime/Core/OBB.h"
#include "Runtime/Core/Sphere.h"

#include <cstddef>
#include <vector>

namespace NS::Gfx
{
    class Renderer;

    //! @brief 線と半透明の面を溜める入れ物。値の型で、持ち主が好きな寿命で捨てる
    //! @details 描いても捨てない (Draw は const)。捨てるのは持ち主が呼ぶ Clear だけ
    //! 線の上限は 4096 頂点、面の上限は三角形 12960 枚。超えたら最も古い物から捨てる
    //! 描く GPU のシェーダと頂点バッファは DebugDraw と共有する 1 組で、持ち主ごとには作らない
    //! 依存: NS::Core::AABB, NS::Core::OBB, NS::Core::Sphere
    class DebugShapes
    {
    public:
        //! 2点間に線分を追加する
        void Line(const NS::Core::Vector3& a, const NS::Core::Vector3& b, const NS::Core::Color& color);

        //! AABB の枠線を追加する
        void AABB(const NS::Core::AABB& box, const NS::Core::Color& color);

        //! OBB の枠線を追加する
        void OBB(const NS::Core::OBB& obb, const NS::Core::Color& color);

        //! @brief 球の枠線を追加する
        //! @details 座標面ごとの大円を 3 本描く。どの向きから見ても 1 本は線に潰れないので半径が読める
        void Sphere(const NS::Core::Sphere& sphere, const NS::Core::Color& color);

        //! @brief 円の枠線を追加する
        //! @param[in] center 円の中心の座標
        //! @param[in] u 円の平面を張る 1 本目の軸。長さが半径
        //! @param[in] v u と直交する 2 本目の軸。長さは u と揃える
        //! @param[in] color 描画色
        void Circle(const NS::Core::Vector3& center,
                    const NS::Core::Vector3& u,
                    const NS::Core::Vector3& v,
                    const NS::Core::Color& color);

        //! @brief カプセル形状の枠線を追加する
        //! @details 円柱の両端の円 2 つと、軸に平行な側面の線 4 本を追加する。両端の半球の弧は描かない
        //! @param[in] base カプセル中心の座標
        //! @param[in] axis 中心から端の半球中心へ向かうベクトル
        //! @param[in] radius カプセルの半径
        //! @param[in] color 描画色
        void Capsule(const NS::Core::Vector3& base,
                     const NS::Core::Vector3& axis,
                     float radius,
                     const NS::Core::Color& color);

        //! @brief 半透明の三角形を追加する
        //! @details 線より先に、裏表の両方を深度を見ずに描く。手前の物にも隠れない。透け具合は色のアルファで決める
        //! @param[in] a 1 つ目の頂点の座標
        //! @param[in] b 2 つ目の頂点の座標
        //! @param[in] c 3 つ目の頂点の座標
        //! @param[in] color 描画色。アルファが不透明度
        void Triangle(const NS::Core::Vector3& a,
                      const NS::Core::Vector3& b,
                      const NS::Core::Vector3& c,
                      const NS::Core::Color& color);

        //! @brief 溜めた図形を描く。溜めた図形は残る
        //! @details 面を先に、線を後に描く。空なら何もしない。GPU の準備に失敗した時は何も描かない
        //! @param[in,out] renderer 描画コマンドの発行先
        //! @param[in] viewProjection ビュー・プロジェクション行列
        void Draw(Renderer& renderer, const NS::Core::Matrix& viewProjection) const noexcept;

        //! 溜めた線と面を全て捨てる
        void Clear() noexcept;

        //! 溜めた線の頂点の総数を返す
        [[nodiscard]] std::size_t VertexCount() const noexcept { return m_lines.size(); }

        //! 溜めた面の頂点の総数を返す
        [[nodiscard]] std::size_t FaceVertexCount() const noexcept { return m_faces.size(); }

    private:
        //! POSITION(12) + COLOR(16) の 28 byte。頂点バッファの 1 頂点と同じ並び
        struct Vertex
        {
            NS::Core::Vector3 position;
            NS::Core::Color color;
        };

        std::vector<Vertex> m_lines; // 線分の頂点。2 つで 1 本
        std::vector<Vertex> m_faces; // 三角形の頂点。3 つで 1 枚
    };
} // namespace NS::Gfx

//! この固定ステップの図形を積む自由関数。積み先は 1 つの静的な DebugShapes
//! 捨てるのは Scene で、段を回す歩の頭・世界の組み直し・終わりの 3 か所。描いても捨てないので次の歩まで描かれ続ける
namespace NS::Gfx::DebugDraw
{
    //! @brief 2点間に線分を追加する
    //! @param[in] a 始点の座標
    //! @param[in] b 終点の座標
    //! @param[in] color 描画色
    void Line(const NS::Core::Vector3& a, const NS::Core::Vector3& b, const NS::Core::Color& color) noexcept;

    //! @brief AABB の枠線を追加する
    //! @param[in] box 描画するAABBデータ
    //! @param[in] color 描画色
    void AABB(const NS::Core::AABB& box, const NS::Core::Color& color) noexcept;

    //! @brief OBB の枠線を追加する
    //! @param[in] obb 描画するOBBデータ
    //! @param[in] color 描画色
    void OBB(const NS::Core::OBB& obb, const NS::Core::Color& color) noexcept;

    //! @brief 球の枠線を追加する
    //! @details 座標面ごとの大円を 3 本描く。どの向きから見ても 1 本は線に潰れないので半径が読める
    //! @param[in] sphere 描画する球データ
    //! @param[in] color 描画色
    void Sphere(const NS::Core::Sphere& sphere, const NS::Core::Color& color) noexcept;

    //! @brief 円の枠線を追加する
    //! @param[in] center 円の中心の座標
    //! @param[in] u 円の平面を張る 1 本目の軸。長さが半径
    //! @param[in] v u と直交する 2 本目の軸。長さは u と揃える
    //! @param[in] color 描画色
    void Circle(const NS::Core::Vector3& center,
                const NS::Core::Vector3& u,
                const NS::Core::Vector3& v,
                const NS::Core::Color& color) noexcept;

    //! @brief カプセル形状の枠線を追加する
    //! @details 円柱の両端の円 2 つと、軸に平行な側面の線 4 本を追加する。両端の半球の弧は描かない
    //! @param[in] base カプセル中心の座標
    //! @param[in] axis 中心から端の半球中心へ向かうベクトル
    //! @param[in] radius カプセルの半径
    //! @param[in] color 描画色
    void Capsule(const NS::Core::Vector3& base,
                 const NS::Core::Vector3& axis,
                 float radius,
                 const NS::Core::Color& color) noexcept;

    //! @brief 前の固定ステップで積んだ図形を捨てる
    //! @details 固定ステップの頭で呼ぶ。描画までに固定ステップが複数進んでも、最後の 1 回で積んだ図形だけが残る
    void BeginStep() noexcept;

    //! @brief この固定ステップの図形を描く。溜めた図形は残る
    //! @details ビューを何枚描いても、歩の無いフレームでも同じ図形が出る
    //! @param[in,out] renderer 描画コマンドの発行先
    //! @param[in] viewProjection ビュー・プロジェクション行列
    void Draw(Renderer& renderer, const NS::Core::Matrix& viewProjection) noexcept;

    //! 蓄積した頂点を全て捨てる
    void Clear() noexcept;

    //! 現在バッファに蓄積されている頂点の総数を返す
    [[nodiscard]] std::size_t VertexCount() noexcept;

    //! @brief 半透明の三角形を追加する
    //! @details 線より先に、裏表の両方を深度を見ずに描く。手前の物にも隠れない。透け具合は色のアルファで決める
    //! 上限の 12960 枚を超える時は最も古い三角形を捨てる
    //! @param[in] a 1 つ目の頂点の座標
    //! @param[in] b 2 つ目の頂点の座標
    //! @param[in] c 3 つ目の頂点の座標
    //! @param[in] color 描画色。アルファが不透明度
    void Triangle(const NS::Core::Vector3& a,
                  const NS::Core::Vector3& b,
                  const NS::Core::Vector3& c,
                  const NS::Core::Color& color) noexcept;

    //! 現在バッファに蓄積されている三角形の頂点の総数を返す
    [[nodiscard]] std::size_t FaceVertexCount() noexcept;
} // namespace NS::Gfx::DebugDraw