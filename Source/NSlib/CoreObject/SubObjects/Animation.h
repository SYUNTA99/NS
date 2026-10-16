#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/CoreObject/Reflection/AssetRef.h"
#include "NSlib/CoreObject/SubObject.h"
#include "NSlib/CoreObject/SubObjects/Model.h"
#include "NSlib/Graphics/Animation.h"
#include "NSlib/Graphics/SkeletalMesh.h"
#include "NSlib/Graphics/Skeleton.h"

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
    class Animation : public SubObject
    {
    public:
        Animation() noexcept;
        ~Animation();

        //! 非所有の SkeletalMesh を差し替える。参照先がない間は ApplyPose を行わない
        void SetMesh(Gfx::SkeletalMesh* mesh) noexcept;
        //! 骨格を差し替える。呼び出し側が寿命を保つ。参照先がない間は ApplyPose を行わない
        void SetSkeleton(const Gfx::Skeleton* skeleton) noexcept;
        //! 使っている骨格。モデルを使っていなければ nullptr
        [[nodiscard]] const Gfx::Skeleton* Skeleton() const noexcept { return m_skeleton; }

        void Play() noexcept;
        //! @brief clip を今のクリップにして、時刻を 0 から再生する。今と同じクリップなら何もしない
        //! @details clip のトラックの骨を、今の骨格で名前から 1 回だけ引いて控える。clip の寿命は AssetManager が保つ
        //! nullptr を渡すとクリップを外し、骨格の基準の姿勢を描く
        void Play(const Gfx::AnimClip* clip);
        void Pause() noexcept;
        //! 再生時刻を 0 に戻して停止する
        void Stop() noexcept;
        //! @brief 再生速度を差し替える
        //! @details 非有限値は無視して直前の値を残す。有限の負値は 0 にクランプする
        //! @param[in] speed 再生速度。1.0 が等倍
        void SetSpeed(float speed) noexcept;
        void SetLooping(bool looping) noexcept;

        //! 今のクリップ。無ければ nullptr
        [[nodiscard]] const Gfx::AnimClip* CurrentClip() const noexcept { return m_clip; }
        [[nodiscard]] float Time() const noexcept;
        //! 現在クリップの尺。無ければ 0
        [[nodiscard]] float Duration() const noexcept;
        [[nodiscard]] bool IsPlaying() const noexcept;

        //! 骨の定数バッファを作り、持ち主の Model へ差して今の時刻の姿勢を書く
        void BindToModel();
        //! 再生中なら体の時間ぶん時刻を進め、今の時刻の姿勢を書く
        void Update();

        //! @brief モデルの欄で読んだ骨付きのモデルの骨格とメッシュを使い、持ち主の Model に同じメッシュを差す
        //! @details 呼ぶのは持ち主の OnAssetsLoaded だけ。モデルが空・読めなければ、骨格もメッシュも空にする
        //! 今のクリップは残し、トラックの骨を新しい骨格で引き直す
        //! 骨付きのアクターは Model のメッシュの欄を空のままにする。差すのはこちら
        void UseModel();

        // 再生速度 / ループを Inspector へ公開する。毎フレーム読まれるのでライブで効き、負速度なら逆再生になる
        NS_REFLECT_BEGIN(Animation, SubObject)
        NS_REFLECT_FIELD(m_speed, "再生速度")
        NS_REFLECT_FIELD(m_looping, "ループ再生")
        NS_REFLECT_FIELD(m_model, "モデル")
        NS_REFLECT_END()

    private:
        void ApplyPose(float time);
        // 今のクリップのトラックの骨を、今の骨格で名前から引き直す
        void FindClipBones();

        Gfx::SkeletalMesh* m_mesh = nullptr; // 更新先の SkeletalMesh (非所有)
        AssetRef<const SkinnedModel> m_model;
        const Gfx::Skeleton* m_skeleton = nullptr; // ボーンパレット計算用の骨格 (非所有)
        const Gfx::AnimClip* m_clip = nullptr;     // 今のクリップ (非所有)
        std::vector<int> m_clipBones;              // m_clip のトラックごとの骨の番号。-1 は骨格に無い骨
        float m_time = 0.0f;
        float m_speed = 1.0f;
        bool m_playing = true;
        bool m_looping = true;
        std::vector<Gfx::BonePose> m_poseScratch; // サンプリング結果の一時ポーズ
        std::vector<Matrix> m_paletteScratch;     // ボーンパレットの一時バッファ

        // ボーンパレットはオブジェクト単位の状態なので mesh でなく本 SubObject が所有する
        std::unique_ptr<Gfx::Buffer> m_bonePaletteCB; // VS b1 用の定数バッファ
        Gfx::BonePaletteCB m_palette;                 // CPU 側パレット、描画側が毎描画 GPU へ上げる

        Model* m_renderer = nullptr; // 同じ object の描画 SubObject。パレットと境界の差し先 (非所有)
    };

} // namespace NS
