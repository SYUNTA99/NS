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

        //! 非所有の D3DSkeletalMesh を差し替える。参照先がない間は ApplyPose を行わない
        void SetMesh(Gfx::D3DSkeletalMesh* mesh) noexcept;
        //! 骨格を差し替える。呼び出し側が寿命を保つ。参照先がない間は ApplyPose を行わない
        void SetSkeleton(const Anim::Skeleton* skeleton) noexcept;
        //! 使っている骨格。モデルを使っていなければ nullptr
        [[nodiscard]] const Anim::Skeleton* Skeleton() const noexcept { return m_skeleton; }

        void Play() noexcept;
        //! @brief clip を今のクリップにして、時刻を 0 から再生する。今と同じクリップなら何もしない
        //! @details clip のトラックの骨を、今の骨格で名前から 1 回だけ引いて控える。clip の寿命は AssetManager が保つ
        //! nullptr を渡すとクリップを外し、骨格の基準の姿勢を描く
        //! blendSeconds が正なら、最後に作った姿勢を保存し、そこから clip の姿勢へ blendSeconds かけて混ぜる。秒は dt で数え、再生速度を掛けない。
        //! 止めている時・前のクリップが無いか有効でない時・まだ姿勢を作っていない時・blendSeconds が 0 以下か非数の時は混ぜない
        void Play(const Anim::AnimClip* clip, float blendSeconds = 0.0f);
        void Pause() noexcept;
        //! 再生時刻を 0 に戻して停止する。補間の途中ならやめる
        void Stop() noexcept;
        //! @brief 再生速度を差し替える
        //! @details 非有限値は無視して直前の値を残す。有限の負値は 0 にクランプする
        //! @param[in] speed 再生速度。1.0 が等倍
        void SetSpeed(float speed) noexcept;
        void SetLooping(bool looping) noexcept;

        //! 今のクリップ。無ければ nullptr
        [[nodiscard]] const Anim::AnimClip* CurrentClip() const noexcept { return m_clip; }
        [[nodiscard]] float Time() const noexcept;
        //! 現在クリップの尺。無ければ 0
        [[nodiscard]] float Duration() const noexcept;
        [[nodiscard]] bool IsPlaying() const noexcept;
        //! 最後の Update で作った各骨のローカルの姿勢。補間の途中なら混ぜた後の姿勢。骨格が無い間は空
        [[nodiscard]] std::span<const Anim::BonePose> LocalPose() const noexcept { return m_pose; }

        //! 骨の定数バッファを作り、renderer へ差して今の時刻の姿勢を書く
        void BindToModel(Model* renderer);
        //! 再生中なら dt (体の倍率を掛けた 1 フレームの秒) ぶん時刻を進め、
        //! 今の時刻の姿勢と描く範囲を renderer へ書く
        void Update(Model* renderer, float dt);

        //! @brief モデルの欄で読んだ骨付きのモデルの骨格とメッシュを使い、renderer に同じメッシュを差す
        //! @details 呼ぶのは持ち主の OnAssetsLoaded だけ。モデルが空・読めなければ、骨格もメッシュも空にする
        //! 今のクリップは残し、トラックの骨を新しい骨格で引き直す
        //! 骨付きのアクターは Model のメッシュの欄を空のままにする。差すのはこちら
        void UseModel(Model* renderer);

        // 再生速度 / ループを Inspector へ公開する。毎フレーム読まれるのでライブで効き、負速度なら逆再生になる
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

        Gfx::D3DSkeletalMesh* m_mesh = nullptr; // 更新先の D3DSkeletalMesh (非所有)
        SkinnedModel* m_model = nullptr;
        const Anim::Skeleton* m_skeleton = nullptr; // ボーンパレット計算用の骨格 (非所有)
        const Anim::AnimClip* m_clip = nullptr;     // 今のクリップ (非所有)
        std::vector<int> m_clipBones;              // m_clip のトラックごとの骨の番号。-1 は骨格に無い骨
        float m_time = 0.0f;
        float m_speed = 1.0f;
        bool m_playing = true;
        bool m_looping = true;
        std::vector<Anim::BonePose> m_pose;        // 最後に作った姿勢
        std::vector<Anim::BonePose> m_blendFrom;   // 切り替えの瞬間に保存した姿勢。混ぜていない間は空
        float m_blendSeconds = 0.0f;
        float m_blendElapsed = 0.0f;
        std::vector<Anim::BonePose> m_poseScratch; // サンプリング結果の一時ポーズ
        std::vector<Matrix> m_paletteScratch;     // ボーンパレットの一時バッファ

        // ボーンパレットはオブジェクト単位の状態なので mesh でなく本部品が所有する
        std::unique_ptr<Gfx::D3DBuffer> m_bonePaletteCB; // VS b1 用の定数バッファ
        Gfx::BonePaletteCB m_palette;                 // CPU 側パレット、描画側が毎描画 GPU へ上げる
    };

} // namespace NS
