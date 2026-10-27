#pragma once

#include "NSlib/Core/Animation/AnimClip.h"
#include "NSlib/Core/Animation/Skeleton.h"
#include "NSlib/Core/Math.h"
#include "NSlib/CoreObject/ClassObjs/Model.h"
#include "NSlib/CoreObject/Object.h"
#include "NSlib/CoreObject/Reflection/AssetField.h"
#include "NSlib/Gfx/D3DSkeletalMesh.h"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace NS
{
    //! 固定更新後にボーンの姿勢を計算する部品
    //! メッシュ・骨格・クリップは非所有で、所有元が寿命を保証する
    class Animation : public Object
    {
    public:
        Animation();
        ~Animation();

        //! 非所有。nullptr の間は姿勢を適用しない
        void SetMesh(Gfx::D3DSkeletalMesh* mesh) noexcept;
        //! 非所有。nullptr の間は姿勢を適用しない
        void SetSkeleton(const Anim::Skeleton* skeleton) noexcept;
        [[nodiscard]] const Anim::Skeleton* Skeleton() const noexcept { return m_skeleton; }

        void Play() noexcept;
        //! @brief clip を今のクリップにして、時刻を 0 から再生する。今と同じクリップなら何もしない
        //! @details clip のトラックの骨を、今の骨格で名前から 1 回だけ引いて控える。clip の寿命は AssetManager が保つ
        //! nullptr を渡すとクリップを外し、骨格の基準の姿勢を描く
        //! blendSeconds が正なら、最後に作った姿勢を保存し、そこから clip の姿勢へ blendSeconds かけて混ぜる。秒は dt で数え、再生速度を掛けない。
        //! 止めていた時は止めた姿勢から混ぜる。前のクリップが無いか有効でない時・まだ姿勢を作っていない時・blendSeconds が 0 以下か非数の時は混ぜない
        void Play(const Anim::AnimClip* clip, float blendSeconds = 0.0f);
        void Pause() noexcept;
        //! 再生時刻を 0 に戻して停止する。補間の途中ならやめる
        void Stop() noexcept;
        //! 非有限値は無視して直前の値を残す。負値は 0 にクランプする
        void SetSpeed(float speed) noexcept;
        void SetLooping(bool looping) noexcept;

        [[nodiscard]] const Anim::AnimClip* CurrentClip() const noexcept { return m_clip; }
        [[nodiscard]] float Time() const noexcept;
        //! 現在クリップの尺 (秒)。無ければ 0
        [[nodiscard]] float Duration() const noexcept;
        [[nodiscard]] bool IsPlaying() const noexcept;
        //! 最後の Update で作った各骨のローカルの姿勢。補間の途中なら混ぜた後の姿勢。骨格が無い間は空
        [[nodiscard]] std::span<const Anim::BonePose> LocalPose() const noexcept { return m_pose; }

        //! ボーンパレット用の定数バッファを作成して renderer へ登録し、現在時刻の姿勢を適用する
        void BindToModel(Model* renderer);
        //! 再生中なら dt (秒) ぶん時刻を進め、現在時刻の姿勢と描画境界を renderer へ反映する
        void Update(Model* renderer, float dt);

        //! モデルの欄で読んだ骨格とメッシュを自身と renderer へ設定する。持ち主の OnAssetsLoaded から呼ぶ
        //! モデルが空または未読込なら骨格とメッシュを空にする。現在のクリップは維持する
        void UseModel(Model* renderer);

        NS_REFLECT_BEGIN(Animation, Object)
        NS_FIELD(m_speed, "再生速度")
        NS_FIELD(m_looping, "ループ再生")
        NS_FIELD(m_model, "モデル")
        NS_REFLECT_END()

    private:
        // 今の時刻の姿勢を m_pose に作る。補間の途中なら保存した姿勢と混ぜる。メッシュは見ない
        void EvaluatePose(float time);
        // m_pose からボーンパレットと描く範囲を作って renderer へ書く
        void ApplyPose(Model* renderer);
        // 今のクリップのトラックの骨を、今の骨格で名前から引き直す
        void FindClipBones();

        Gfx::D3DSkeletalMesh* m_mesh = nullptr;
        SkinnedModel* m_model = nullptr;
        const Anim::Skeleton* m_skeleton = nullptr;
        const Anim::AnimClip* m_clip = nullptr;
        std::vector<int> m_clipBones; // トラックごとのボーン番号 (-1 は骨格に無いボーン)
        float m_time = 0.0f;          // 秒
        float m_speed = 1.0f;
        bool m_playing = true;
        bool m_looping = true;
        std::vector<Anim::BonePose> m_pose;        // 最後に作った姿勢
        std::vector<Anim::BonePose> m_blendFrom;   // 切り替えの瞬間に保存した姿勢。混ぜていない間は空
        float m_blendSeconds = 0.0f;
        float m_blendElapsed = 0.0f;
        std::vector<Anim::BonePose> m_poseScratch; // サンプリング結果の一時ポーズ
        std::vector<Matrix> m_paletteScratch;     // ボーンパレットの一時バッファ

        std::unique_ptr<Gfx::D3DBuffer> m_bonePaletteCB;
        Gfx::BonePaletteCB m_palette;
    };

} // namespace NS
