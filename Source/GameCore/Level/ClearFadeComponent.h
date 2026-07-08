#pragma once

/// @file ClearFadeComponent.h
/// @brief クリア暗転の状態機械。 ScreenFade を所有し Overlay バケットで自己描画する
///
/// @details 暗転 Out → 全黒でリスタート → 明転 In → None の一方向で進み、 進行中の再開始は無視する
/// 進行は PlayFlowComponent が dt を渡して駆動し、 描画は scene の Overlay 描画から呼び戻される

#include <cstdint>
#include <memory>

namespace NS::Graphics
{
    class ScreenFade;
} // namespace NS::Graphics

namespace NS::GameCore::Level
{
    /// ゴール接触からレベル再開へ繋ぐ暗転 / 明転。 全黒到達で PlayFlowComponent のリスタートを呼ぶ
    class ClearFadeComponent : public NS::Scene::Component, public NS::Scene::IRenderable
    {
    public:
        /// ゴール到達の達成を一拍味わわせ、 暗転で区切って「もう一周」へ自然に送り出すためのテンポ
        /// 短すぎると唐突、 長いと待たされるため、 プラットフォーマーの仕切り感として前後 0.4 秒に置く
        static constexpr float kFadeOutSeconds = 0.4f;
        static constexpr float kFadeInSeconds = 0.4f;

        // ScreenFade を前方宣言のまま unique_ptr で持つため、 ctor / dtor は cpp 側で定義する
        ClearFadeComponent() noexcept;
        ~ClearFadeComponent() noexcept override;

        /// 暗転を開始する。 進行中の再呼び出しは無視する
        void Begin() noexcept;

        /// 暗転を dt だけ進める。 暗転しきった瞬間にレベルを頭から再開し、 明転しきったら通常へ戻す
        void Advance(float dt) noexcept;

        /// 暗転 / 明転が進行中か。 進行中は PlayFlowComponent がプレイ更新を止めてタイマーだけ進める
        [[nodiscard]] bool IsFading() const noexcept { return m_stage != Stage::None; }

        /// 全画面に重ねる黒の不透明度。 0 で透明、 1 で全黒
        [[nodiscard]] float Alpha() const noexcept { return m_alpha; }

        void OnStart() override;
        void OnEndPlay() override;

        void Draw(const NS::Scene::RenderContext& context) override;
        [[nodiscard]] NS::Scene::RenderBucket Bucket() const noexcept override
        {
            return NS::Scene::RenderBucket::Overlay;
        }

        // 暗転状態は保存しない。live の型検索が反射照合で引けるよう型名だけ登録する
        NS_REFLECT_NONE(ClearFadeComponent, NS::Scene::Component)

    private:
        // ゴール接触からレベル再開へ繋ぐ暗転の段階。 None は通常プレイ
        enum class Stage : std::uint8_t
        {
            None,
            Out,
            In
        };

        Stage m_stage = Stage::None;
        // 現在の暗転段階の経過秒。 段階の開始ごとに 0 へ戻す
        float m_timer = 0.0f;
        // 全画面に重ねる黒の不透明度。 0 で透明、 1 で全黒。 Draw が読む
        float m_alpha = 0.0f;

        // 暗転 / 明転を全画面へ重ねる描画資源。 構築失敗時は演出なしで続行する
        std::unique_ptr<NS::Graphics::ScreenFade> m_screenFade;
    };

} // namespace NS::GameCore::Level
