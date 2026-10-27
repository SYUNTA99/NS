#pragma once

// 線分と AABB / OBB / 球 / カプセルの枠線、半透明の三角形を溜めて、まとめて描くデバッグ描画

#include "NSlib/Core/AABB.h"
#include "NSlib/Core/Math.h"
#include "NSlib/Core/NonCopyable.h"
#include "NSlib/Core/OBB.h"
#include "NSlib/Core/Sphere.h"
#include "NSlib/Core/Types.h"
#include "NSlib/Gfx/D3dCommon.h"

#include <cstddef>
#include <memory>
#include <vector>

namespace NS::Gfx
{
        class D3DBuffer;
        class D3DDevice;
        class D3DPipeline;
        class D3DShader;
        struct BuiltinShaders;

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

            //! 溜めた線と面を全て捨てる
            void Clear() noexcept;

            //! 溜めた線の頂点の総数を返す
            [[nodiscard]] std::size_t VertexCount() const noexcept { return m_lines.size(); }

            //! 溜めた面の頂点の総数を返す
            [[nodiscard]] std::size_t FaceVertexCount() const noexcept { return m_faces.size(); }

        private:
            // 頂点の並びをそのまま頂点バッファへ写すため
            friend class DebugShapePass;

            //! POSITION(12) + COLOR(16) の 28 バイト。頂点バッファの 1 頂点と同じ並び
            struct Vertex
            {
                Vector3 position;
                Color color;
            };

            std::vector<Vertex> m_lines; // 線分の頂点。2 つで 1 本
            std::vector<Vertex> m_faces; // 三角形の頂点。3 つで 1 枚
        };

        //! @brief DebugShapes を描く段。頂点バッファ・定数バッファ・面のパイプライン・入力レイアウトを持つ
        //! @details 資源は作った時の装置の物なので、装置を差し替えたら作り直す
        //! @warning D3DDevice より先に破棄する。シェーダーの持ち主が捨てた後は描かない
        class DebugShapePass : public NonCopyable
        {
        public:
            //! @brief 頂点バッファ・定数バッファ・面のパイプラインを作る
            //! @param[in] shaders debugLineVs と debugLinePs を指すだけで持たない。使えるかは描く度に決める
            explicit DebugShapePass(const BuiltinShaders& shaders) noexcept;
            ~DebugShapePass();

            //! 頂点バッファ・定数バッファ・面のパイプラインを作れた場合 true、それ以外の場合は false
            [[nodiscard]] bool IsValid() const noexcept { return m_valid; }

            //! @brief 溜めた図形を描く。溜めた図形は残る
            //! @details 面を先に、線を後に描く。空・資源を作れなかった時・シェーダーが使えない時は何も描かない
            //! 入力レイアウトは頂点シェーダーの BytecodeId が作った時と違えば作り直す
            //! @param[in,out] device 描画コマンドの発行先
            void Draw(D3DDevice& device, const DebugShapes& shapes, const Matrix& viewProjection) noexcept;

        private:
            // 頂点シェーダーの今の中身で入力レイアウトを選ぶ。作れなければ空のまま
            void SelectInputLayout(const D3DShader& vertexShader) noexcept;

            D3DShader* m_vs = nullptr;
            D3DShader* m_ps = nullptr;
            std::unique_ptr<D3DBuffer> m_lineVb;
            std::unique_ptr<D3DBuffer> m_faceVb;
            std::unique_ptr<D3DBuffer> m_cb;
            // 面は奥の側も見せるので裏表とも描く。深度を見ないので、物の表面と重なった面もちらつかずに透ける
            std::unique_ptr<D3DPipeline> m_facePipeline;
            ComPtr<ID3D11InputLayout> m_inputLayout;
            // m_inputLayout を作った時の頂点シェーダーの BytecodeId。作れなかった時も控え、番号が変わるまで作り直さない
            uint64 m_layoutBytecodeId = 0;
            bool m_valid = false;
        };
} // namespace NS::Gfx

//! この固定ステップの図形を積む自由関数。積み先は 1 つの静的な DebugShapes
//! 捨てるのは Scene で、段を回す歩の頭・世界の組み直し・終わりの 3 か所。描いても捨てないので次の歩まで描かれ続ける
namespace NS::Gfx::DebugDraw
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

            //! @brief この固定ステップの図形。描く側が DebugShapePass で描く
            //! @details 描いても捨てないので、ビューを何枚描いても、固定ステップが進まないフレームでも同じ図形が出る
            [[nodiscard]] const DebugShapes& StepShapes() noexcept;

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
} // namespace NS::Gfx::DebugDraw
