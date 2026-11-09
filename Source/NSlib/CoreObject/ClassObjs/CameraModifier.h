#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/Core/NonCopyable.h"
#include "NSlib/CoreObject/ClassObjs/VirtualCamera.h"

#include "NSlib/Core/Types.h"
#include <memory>

namespace NS
{
    class IGetCamera;

    //! 効果を掛ける時の姿勢の軸。仮想カメラとブレンドまで済んだ姿勢の視線から作る。どちらも長さ 1
    struct CameraAxes
    {
        Vector3 right{1.0f, 0.0f, 0.0f}; // 視線の水平から作った右
        Vector3 up{0.0f, 1.0f, 0.0f};    // 視線と右から作った上
    };

    //! 効果を掛ける順 (CameraModifier::Order)。揺れとずれを先に掛け、寄りと傾きはその後に掛ける
    inline constexpr int k_CameraShakeOrder = 100;
    inline constexpr int k_CameraLensOrder = 200;

    //! CameraManager が所有する姿勢の効果。Order 順に適用し、完了すると外す
    //! 最初の Tick は進めず、追加したフレームに初期姿勢を描く
    class CameraModifier : public NonCopyable
    {
    public:
        virtual ~CameraModifier() noexcept;

        //! 同じ種類の効果かを見分けるタグ
        //! 管理役は同じタグの効果を積み直すと前の物を外す。nullptr の効果は幾つでも重ねて積める
        [[nodiscard]] virtual const void* Kind() const noexcept { return nullptr; }

        //! 掛ける順。小さいほど先に掛かる
        [[nodiscard]] virtual int Order() const noexcept { return 0; }

        //! 固定ステップで 1 つ進める。積んだ直後の 1 回は進めない
        void Tick() noexcept;

        //! 姿勢へ効果を掛ける。axes は効果を掛ける前の姿勢の軸
        virtual void Modify(CameraPose& pose, const CameraAxes& axes) const noexcept = 0;

        //! 効果を描き終えたか。真になった効果は管理役が外す
        [[nodiscard]] virtual bool IsFinished() const noexcept = 0;

        //! 揺れの効果の場合 true。管理役が設定の倍率 (SetShakeScale) を掛ける
        [[nodiscard]] virtual bool IsShake() const noexcept { return false; }

        //! 頼んだ物を書く。管理役は同じ種類のタグと同じ頼んだ物の効果だけを置き換え、頼んだ物ごとに外せる
        //! 見分けるだけで指す先は読まない。nullptr は頼んだ物を問わない効果
        void SetRequester(const IGetCamera* requester) noexcept { m_requester = requester; }
        [[nodiscard]] const IGetCamera* Requester() const noexcept { return m_requester; }

    protected:
        //! Frame を 1 つ進めた後に呼ばれる派生クラス用フック
        virtual void Advance() noexcept {}

        //! 積んだフレームを 0 とした番号
        [[nodiscard]] int Frame() const noexcept { return m_frame; }

    private:
        bool m_justAdded = true;
        int m_frame = 0;
        const IGetCamera* m_requester = nullptr;
    };

    //! 振れ幅は トラウマ^exponent × 最大角度。トラウマは時間で線形減衰する
    struct CameraTraumaShape
    {
        float yawDegrees = 0.0f;     // トラウマ 1 時の最大ヨー角 (度)
        float pitchDegrees = 0.0f;   // トラウマ 1 時の最大ピッチ角 (度)
        float rollDegrees = 0.0f;    // トラウマ 1 時の最大ロール角 (度)
        float frequency = 10.0f;     // ノイズ周波数 (1/秒)
        float decayPerSecond = 1.0f; // 減衰速度 (1/秒)
        float exponent = 2.0f;
    };

    //! peakFrames で最大になる、衝撃方向への減衰回転
    struct CameraKick
    {
        float degrees = 0.0f; // 最大角度 (度)
        int peakFrames = 1;
        Vector2 direction{};  // 画面上の方向 (x: 右, y: 上)。長さ 0 なら無効
    };

    struct CameraTraumaDesc
    {
        float trauma = 0.0f; // 加算量 (0〜1、加算後も上限 1)
        CameraTraumaShape shape;
        uint32 seed = 0;
        CameraKick kick;     // degrees == 0 なら無効
    };

    //! トラウマに応じて視線と傾きを揺らす。追加分は上限 1 まで重ね、ViewPose には適用しない
    class CameraTraumaModifier final : public CameraModifier
    {
    public:
        CameraTraumaModifier() noexcept = default;

        [[nodiscard]] static const void* StaticKind() noexcept;
        [[nodiscard]] const void* Kind() const noexcept override { return StaticKind(); }
        [[nodiscard]] int Order() const noexcept override { return k_CameraShakeOrder; }

        //! トラウマを加算し、波形とシードを更新する。kick が有効なら再開する
        void AddTrauma(const CameraTraumaDesc& desc) noexcept;

        [[nodiscard]] float Trauma() const noexcept { return m_trauma; }
        //! トラウマ^exponent
        [[nodiscard]] float ShakeAmount() const noexcept;

        //! 現在のフレームの回転角 (度)。x: ヨー (右が正)、y: ピッチ (上が正)、z: ロール (上端右倒しが正)
        [[nodiscard]] Vector3 Angles() const noexcept;

        void Modify(CameraPose& pose, const CameraAxes& axes) const noexcept override;
        [[nodiscard]] bool IsFinished() const noexcept override;
        [[nodiscard]] bool IsShake() const noexcept override { return true; }

    private:
        void Advance() noexcept override;

        CameraTraumaShape m_shape{};
        float m_trauma = 0.0f;
        uint32 m_seed = 0;
        CameraKick m_kick{};
        int m_kickFrame = 0;
    };
} // namespace NS
