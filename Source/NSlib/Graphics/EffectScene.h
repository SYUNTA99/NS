#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/Core/NonCopyable.h"

#pragma warning(push, 0)
#include <Effekseer.h>
#include <EffekseerRendererCommon/EffekseerRenderer.Renderer.h>
#pragma warning(pop)

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace NS
{
    class CameraData;
} // namespace NS

namespace NS::Gfx
{
    class Texture;

    //! @brief EffectScene::Play が返す、再生したエフェクトのハンドル
    struct EffectHandle
    {
        std::int32_t value = -1; //!< Effekseer の Manager が返したハンドル値。既定の -1 は無効

        //! value が 0 以上の場合 true、それ以外の場合は false
        //! @details エフェクトが今も残っているかは EffectScene::Exists で見る
        [[nodiscard]] bool IsValid() const noexcept { return value >= 0; }
    };

    //! @brief EffectScene::Play に渡す、再生の姿勢と見た目
    //! @details 既定の値で渡すと、原点で絵の定義どおりに出す
    struct EffectPlayDesc
    {
        NS::Vector3 position{0.0f, 0.0f, 0.0f};                   //!< 再生位置のワールド座標
        NS::Quaternion rotation = NS::Quaternion::Identity; //!< 絵の向き
        NS::Vector3 scale{1.0f, 1.0f, 1.0f};                      //!< 絵の大きさの倍率。軸ごとに掛ける
        //! 絵の全体に掛ける色。各成分 0〜1 で、白は定義の色のまま
        NS::Color color{1.0f, 1.0f, 1.0f, 1.0f};
        //! 動的入力 0〜3 番の値。書いた番号だけ、絵の定義が持つ既定の値を上書きする
        std::array<std::optional<float>, 4> dynamicInputs{};
    };

    //! @brief Effekseer のエフェクトを読み込み、再生して描くクラス
    //! @details 構築時に Gpu() の device と context を使う。NS の Renderer が未構築なら無効な状態になる
    //! 無効な状態では IsValid が false を返し、各操作は何もしない
    //! エフェクトは Preload で読み込んでから Play で再生する。Play はファイルを読まない
    class EffectScene : public NS::NonCopyable
    {
    public:
        //! @brief Effekseer の Renderer と Manager を作る
        //! @param[in] effectRoot Preload が .efkefc を探すディレクトリ
        explicit EffectScene(std::string effectRoot) noexcept;
        ~EffectScene();

        //! 構築に成功した場合 true、それ以外の場合は false
        [[nodiscard]] bool IsValid() const noexcept;

        //! @brief effectRoot から name.efkefc を読み込み、Play で再生できるようにする
        //! @param[in] name 拡張子を除いた effectRoot 相対のパス。UTF-8 で渡す
        //! @return 読み込めたか読み込み済みの場合 true、それ以外の場合は false
        //! @details 参照するテクスチャ・モデル・マテリアル・カーブを 1 つでも読めなければ失敗にする
        //! 読み込めた name は 2 回目以降ファイルを読まない
        [[nodiscard]] bool Preload(std::string_view name) noexcept;

        //! @brief Preload 済みのエフェクトを 1 つ再生する
        //! @param[in] name Preload に渡した名前
        //! @param[in] position 再生位置のワールド座標。省略すると原点
        //! @return 再生したエフェクトのハンドル。Preload していない名前なら IsValid が false のハンドル
        //! @details Preload していない名前の警告は名前ごとに 1 回だけ出す
        [[nodiscard]] EffectHandle Play(std::string_view name, NS::Vector3 position = {}) noexcept;

        //! @brief Preload 済みのエフェクトを、姿勢と見た目を決めて 1 つ再生する
        //! @param[in] name Preload に渡した名前
        //! @param[in] desc 再生の姿勢・色・動的入力
        //! @return 再生したエフェクトのハンドル。Preload していない名前なら IsValid が false のハンドル
        //! @details 絵が生まれるのは次の Update で、生まれた瞬間の姿がその後の Draw に写る。
        //! 乱数の種は name と、その name を何回目に再生したかで決まる。他のエフェクトの再生や std::rand の呼び出しに
        //! 左右されない。Preload していない名前の警告は名前ごとに 1 回だけ出す
        [[nodiscard]] EffectHandle Play(std::string_view name, const EffectPlayDesc& desc) noexcept;

        //! @brief handle のエフェクトの位置・向き・大きさを置き直す
        //! @param[in] handle Play が返したハンドル。IsValid が false なら何もしない
        //! @param[in] position ワールド座標の位置
        //! @param[in] rotation 向き
        //! @param[in] scale 大きさの倍率
        //! @details 物に付いていく層は、物が動いたフレームごとに呼ぶ。描く位置が変わるのは次の Update の後
        void SetTransform(EffectHandle handle,
                          const NS::Vector3& position,
                          const NS::Quaternion& rotation,
                          const NS::Vector3& scale) noexcept;

        //! @brief handle のエフェクトの動的入力を 1 つ書き換える
        //! @param[in] handle Play が返したハンドル。IsValid が false なら何もしない
        //! @param[in] index 動的入力の番号。0〜3 の外なら警告を出して何もしない
        //! @param[in] value 入れる値
        //! @details 生成の数のように生まれる時に読む値は、最初の Update より前に書いた時だけ効く
        void SetDynamicInput(EffectHandle handle, int index, float value) noexcept;

        //! @brief handle のエフェクトを止める
        //! @param[in] handle Play が返したハンドル。IsValid が false なら何もしない
        //! @details 子も含めて次の Update で消える。経過 0 の Update でも消える
        void Stop(EffectHandle handle) noexcept;

        //! @brief handle のエフェクトの親だけを止める
        //! @param[in] handle Play が返したハンドル。IsValid が false なら何もしない
        //! @details 新しい子は出なくなり、出ていた子は寿命まで残る。子が全部消えると Exists が false になる
        void StopRoot(EffectHandle handle) noexcept;

        //! 再生中の全エフェクトを止める
        void StopAll() noexcept;

        //! @brief handle のエフェクトが Effekseer の Manager に残っているかを返す
        //! @param[in] handle Play が返したハンドル
        //! @return 残っている場合 true、それ以外の場合は false
        [[nodiscard]] bool Exists(EffectHandle handle) const noexcept;

        //! @brief 全エフェクトを deltaSeconds だけ進める
        //! @param[in] deltaSeconds 経過秒数。0 なら進めずに Play と Stop だけを描画へ反映する。負なら何もしない
        void Update(float deltaSeconds) noexcept;

        //! @brief camera から見た全エフェクトを現在の描画先へ描く
        //! @param[in] camera ビュー行列・投影行列・位置を使う視点
        //! @details 描画先に結ばれた奥行き (D24_UNORM_S8_UINT) を別のテクスチャへ写してエフェクトへ渡し、
        //! 柔らかい粒の欄を入れた層は物と重なる所の近くほど薄く描く。写し先は大きさごとに 2 枚まで控え、
        //! 同じ大きさの描画先では作り直さない。奥行きが結ばれていない時は奥行き無しで描く。
        //! 写せない奥行きの時と写し先を作れなかった時はエラーを 1 回出して奥行き無しで描き、IsUsingDepthFallback が
        //! true を返す
        void Draw(const NS::CameraData& camera) noexcept;

        //! 直前の Draw がエフェクトへ奥行きを渡した場合 true、それ以外の場合は false
        [[nodiscard]] bool PassesDepth() const noexcept;

        //! 直前の Draw が写せない奥行きか写し先の作りそこないで奥行き無しに倒した場合 true、それ以外の場合は false
        [[nodiscard]] bool IsUsingDepthFallback() const noexcept { return m_depthFallback; }

        //! 奥行きの写し先を作った回数
        [[nodiscard]] std::uint32_t DepthCopiesCreated() const noexcept { return m_depthCopiesCreated; }

    private:
        // 奥行きの写し先 1 枚。描画先の大きさで引く
        struct DepthCopy
        {
            std::unique_ptr<Texture> texture;
            Effekseer::Backend::TextureRef effekseerTexture;
            std::uint32_t width = 0;
            std::uint32_t height = 0;
            std::uint32_t lastUsed = 0; // 最後に使った Draw の番号。3 つ目の大きさが来たら古い方を捨てる
        };

        // 描画先に結ばれた奥行きを写し先へ写してエフェクトへ渡す。渡せない時は奥行きを外す
        void PassDepth(const NS::CameraData& camera) noexcept;
        // 大きさの合う写し先を返す。無ければ作る。作れなければ nullptr
        [[nodiscard]] DepthCopy* DepthCopyFor(std::uint32_t width, std::uint32_t height) noexcept;
        // 奥行き無しへ倒す。同じ理由と大きさでは 2 回目からエラーを出さない
        void FallBackWithoutDepth(std::string_view reason, std::uint32_t width, std::uint32_t height) noexcept;

        Effekseer::ManagerRef m_manager;
        EffekseerRenderer::RendererRef m_renderer;
        std::unordered_map<std::string, Effekseer::EffectRef> m_effects;
        std::unordered_set<std::string> m_warnedMissing;
        // 名前ごとに Play した回数。乱数の種に混ぜる
        std::unordered_map<std::string, std::uint32_t> m_playCounts;
        std::string m_effectRoot;
        float m_elapsedSeconds = 0.0f;

        std::array<DepthCopy, 2> m_depthCopies{}; // Bloom と同じくゲームの絵とエディタのビューで 1 枚ずつ
        std::uint32_t m_depthCopiesCreated = 0;
        std::uint32_t m_drawCount = 0;
        bool m_depthFallback = false;
        std::string m_failedDepthKey; // 最後にエラーを出した理由と大きさ。同じ失敗を毎フレーム出さない
    };
} // namespace NS::Gfx
