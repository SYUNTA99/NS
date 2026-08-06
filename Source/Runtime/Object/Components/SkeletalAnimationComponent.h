#pragma once

#include "Runtime/Graphics/Animation.h"
#include "Runtime/Graphics/SkeletalMesh.h"
#include "Runtime/Graphics/Skeleton.h"
#include "Runtime/Math/Math.h"
#include "Runtime/Object/Component.h"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace NS::Object
{
    class MeshRendererComponent;

    /// @brief クリップを時間再生して SkeletalMesh のボーンパレットを更新する
    /// @details fixed step ごとに再生時刻を進める。AnimationClip をサンプリングしたポーズを
    /// Skeleton でボーンパレット化し、SkeletalMesh に渡す。再生 / 停止 / 速度 / ループ / クリップ選択を制御できる
    /// mesh / skeleton / clips は全て非所有で、AssetManager 等の所有側が寿命を保証する。priority は Update 帯の後方
    /// (+100、移動の後に骨を追従させる)
    class SkeletalAnimationComponent : public Component
    {
    public:
        SkeletalAnimationComponent() noexcept;
        ~SkeletalAnimationComponent();

        /// 更新先の SkeletalMesh を差し替える。非所有で null の間 ApplyPose は何もしない
        void SetMesh(NS::Graphics::SkeletalMesh* mesh) noexcept;
        /// ボーンパレット計算用の骨格を差し替える。非所有で呼出側が寿命を保証する。null の間 ApplyPose は何もしない
        void SetSkeleton(const NS::Graphics::Skeleton* skeleton) noexcept;

        /// この Component が表す skinned モデルの参照。 ContentRoot 配下の glTF 相対パス
        [[nodiscard]] const std::string& ModelRef() const noexcept { return m_modelRef; }
        /// build 時にこの文字列から mesh / skeleton / clips を解決する。 同じ object の MeshRendererComponent の mesh も
        /// こちらが差すので、 skinned の配置物は MeshRenderer 側の Mesh 参照を空のままにする
        void SetModelRef(std::string ref) noexcept { m_modelRef = std::move(ref); }

        /// 追加で読むアニメーション glTF の参照一覧。 セミコロン区切りの ContentRoot 相対パス
        [[nodiscard]] const std::string& ClipsRef() const noexcept { return m_clipsRef; }
        /// 各エントリのクリップを骨名で model の骨格へ結合して後ろに足す。 空エントリと前後の空白は無視し、
        /// 解決できないエントリは読み飛ばして残りを続ける
        void SetClipsRef(std::string ref) noexcept { m_clipsRef = std::move(ref); }

        void Play() noexcept;
        void Pause() noexcept;
        /// 再生時刻を 0 に戻して停止する
        void Stop() noexcept;
        /// 負値は 0 にクランプする
        void SetSpeed(float speed) noexcept;
        void SetLooping(bool looping) noexcept;
        bool SelectClip(std::size_t index) noexcept;
        bool SelectClip(std::string_view name) noexcept;

        /// クリップを後から追加する。既存の選択・再生位置は維持
        /// 各要素のアドレスを控えるだけなので、格納元の寿命は呼出側が保証する。右辺値の vector を渡すと参照が宙に浮く
        void AddClips(std::span<const NS::Graphics::AnimationClip> clips);

        [[nodiscard]] std::size_t ClipCount() const noexcept;
        [[nodiscard]] std::size_t CurrentClip() const noexcept;
        [[nodiscard]] float Time() const noexcept;
        /// 現在クリップの尺。 無ければ 0
        [[nodiscard]] float Duration() const noexcept;
        [[nodiscard]] bool IsPlaying() const noexcept;

        void OnStart() override;
        void OnUpdate() override;

        /// modelRef から skinned glTF を解決し mesh / skeleton / clips を差す。 同じ object の MeshRendererComponent があれば
        /// 同じ mesh を差す。 空 / 解決不可はそのまま何もしない (SetMesh 等の手動配線を壊さない)
        void ResolveAssets(AssetManager& assets) override;

        // 再生速度 / ループを Inspector へ公開する。 毎ステップ読まれるのでライブで効き、 負速度なら逆再生になる
        NS_REFLECT_BEGIN(SkeletalAnimationComponent, Component)
        NS_REFLECT_FIELD(m_speed, "再生速度")
        NS_REFLECT_FIELD(m_looping, "ループ再生")
        NS_REFLECT_FIELD(m_modelRef, "モデル")
        NS_REFLECT_FIELD(m_clipsRef, "クリップ")
        NS_REFLECT_END()

    private:
        void ApplyPose(float time);

        NS::Graphics::SkeletalMesh* m_mesh = nullptr; // 更新先の SkeletalMesh (非所有)
        std::string m_modelRef{}; // 保存・編集される参照文字列。 ResolveAssets が mesh/skeleton/clips へ実体を当てる
        std::string m_clipsRef{}; // 追加アニメーションの参照一覧。 セミコロン区切りで ResolveAssets が結合する
        const NS::Graphics::Skeleton* m_skeleton = nullptr;      // ボーンパレット計算用の骨格 (非所有)
        std::vector<const NS::Graphics::AnimationClip*> m_clips; // 再生できるクリップ一覧 (非所有)
        std::size_t m_current = 0;                               // 選択中クリップの添字
        float m_time = 0.0f;                                     // 現在の再生時刻
        float m_speed = 1.0f;                                    // 再生速度
        bool m_playing = true;                                   // 再生中か
        bool m_looping = true;                                   // 末尾でループするか
        std::vector<NS::Graphics::BonePose> m_poseScratch;       // サンプリング結果の一時ポーズ
        std::vector<NS::Math::Matrix> m_paletteScratch;          // ボーンパレットの一時バッファ

        // ボーンパレットはオブジェクト単位の状態なので mesh でなく本 component が所有する
        std::unique_ptr<NS::Graphics::Buffer> m_bonePaletteCB; // VS b1 用の定数バッファ
        NS::Graphics::BonePaletteCB m_palette;                 // CPU 側パレット、描画側が毎描画 GPU へ上げる

        MeshRendererComponent* m_renderer = nullptr; // 同じ object の描画 component。パレットと境界の差し先 (非所有)
    };

} // namespace NS::Object
