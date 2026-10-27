#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/Core/NonCopyable.h"
#include "NSlib/CoreObject/ClassObjs/VirtualCamera.h"
#include "NSlib/CoreObject/Reflection/Curve.h"

#include "NSlib/Core/Types.h"
#include <array>
#include <memory>
#include <vector>

namespace NS
{
    class IGetCamera;

    /// 効果を掛ける時の姿勢の軸。仮想カメラとブレンドまで済んだ姿勢の視線から作る。どちらも長さ 1
    struct CameraAxes
    {
        Vector3 right{1.0f, 0.0f, 0.0f}; // 視線の水平から作った右
        Vector3 up{0.0f, 1.0f, 0.0f};    // 視線と右から作った上
    };

    /// CameraManager が所有する姿勢の効果。Order 順に適用し、完了すると外す
    /// 最初の Tick は進めず、追加したフレームに初期姿勢を描く
    class CameraModifier : public NonCopyable
    {
    public:
        virtual ~CameraModifier() noexcept;

        /// 同じ種類の効果かを見分けるタグ
        /// 管理役は同じタグの効果を積み直すと前の物を外す。nullptr の効果は幾つでも重ねて積める
        [[nodiscard]] virtual const void* Kind() const noexcept { return nullptr; }

        /// 掛ける順。小さいほど先に掛かる
        [[nodiscard]] virtual int Order() const noexcept { return 0; }

        /// 固定ステップで 1 つ進める。積んだ直後の 1 回は進めない
        void Tick() noexcept;

        /// 姿勢へ効果を掛ける。axes は効果を掛ける前の姿勢の軸
        virtual void Modify(CameraPose& pose, const CameraAxes& axes) const noexcept = 0;

        /// 効果を描き終えたか。真になった効果は管理役が外す
        [[nodiscard]] virtual bool IsFinished() const noexcept = 0;

        /// 揺れの効果の場合 true。管理役が設定の倍率 (SetShakeScale) を掛ける
        [[nodiscard]] virtual bool IsShake() const noexcept { return false; }

        /// 頼んだ物を書く。管理役は同じ種類のタグと同じ頼んだ物の効果だけを置き換え、頼んだ物ごとに外せる
        /// 見分けるだけで指す先は読まない。nullptr は頼んだ物を問わない効果
        void SetRequester(const IGetCamera* requester) noexcept { m_requester = requester; }
        [[nodiscard]] const IGetCamera* Requester() const noexcept { return m_requester; }

    protected:
        /// Frame を 1 つ進めた後に呼ばれる派生クラス用フック
        virtual void Advance() noexcept {}

        /// 積んだフレームを 0 とした番号
        [[nodiscard]] int Frame() const noexcept { return m_frame; }

    private:
        bool m_justAdded = true;
        int m_frame = 0;
        const IGetCamera* m_requester = nullptr;
    };

    /// カメラの右と上へずらす揺れ。振れ幅は直線で減り、seed から反転間隔を決める
    /// 縦の初回は下向き。longestFlipFrames が 2 以上なら同じ間隔を連続させない
    struct CameraShakeDesc
    {
        float sideAmplitude = 0.0f;                   // 初回の横振幅 (m)
        float upAmplitude = 0.0f;                     // 初回の縦振幅 (m)
        int frames = 0;                               // 開始フレームを含む総フレーム数
        int longestFlipFrames = 1;                    // 反転までの最長フレーム数
        Vector3 firstSideDirection{1.0f, 0.0f, 0.0f}; // 初回の横振れの世界方向
        uint32 seed = 0;
    };

    /// delayFrames の後に倍率と傾きを適用し、holdFrames の間保ち、returnFrames で滑らかに元へ戻す
    struct CameraZoomRollDesc
    {
        float zoom = 1.0f;                       // 1 で等倍
        float rollDegrees = 0.0f;                // 視線軸まわりの傾き (度)
        Vector3 rollDirection{1.0f, 0.0f, 0.0f}; // 画面上端を倒す側を決める世界方向
        int holdFrames = 0;                      // 開始フレームを含む保持フレーム数
        int returnFrames = 0;
        int delayFrames = 0;
    };

    struct CameraZoomRoll
    {
        float zoom = 1.0f;
        float rollDegrees = 0.0f; // 視線軸まわりの傾き (度、正は画面上端をカメラの右へ倒す向き)
    };

    /// 画面揺れ。position と target を同じだけ動かす平行移動なので視線の向きは回らない
    class CameraShakeModifier final : public CameraModifier
    {
    public:
        /// firstSideSign が負なら初回は左、それ以外は右へ振る
        /// 非有限値・負の振幅・frames <= 0・longestFlipFrames < 1 の場合は nullptr を返す
        [[nodiscard]] static std::unique_ptr<CameraShakeModifier> Create(const CameraShakeDesc& desc,
                                                                         float firstSideSign);

        [[nodiscard]] static const void* StaticKind() noexcept;
        [[nodiscard]] const void* Kind() const noexcept override { return StaticKind(); }
        [[nodiscard]] int Order() const noexcept override { return 100; }

        void Modify(CameraPose& pose, const CameraAxes& axes) const noexcept override;
        [[nodiscard]] bool IsFinished() const noexcept override;
        [[nodiscard]] bool IsShake() const noexcept override { return true; }

        /// 現在のフレームのずれ (m)。x がカメラの右、y が上。完了後は 0
        [[nodiscard]] Vector2 Offset() const noexcept;

    private:
        CameraShakeModifier() noexcept = default;

        std::vector<Vector2> m_offsets; // フレームごとのずれ (m、x: 右, y: 上)
    };

    /// 寄りと傾き。寄りは視野角、傾きは視線の軸まわりの上の向きで掛け、視線ベクトルは変えない
    class CameraZoomRollModifier final : public CameraModifier
    {
    public:
        /// rollSign が負なら上端を左へ、それ以外は右へ倒す
        /// 非有限値・zoom < 1・負のフレーム数の場合は nullptr を返す
        [[nodiscard]] static std::unique_ptr<CameraZoomRollModifier> Create(const CameraZoomRollDesc& desc,
                                                                            float rollSign);

        [[nodiscard]] static const void* StaticKind() noexcept;
        [[nodiscard]] const void* Kind() const noexcept override { return StaticKind(); }
        [[nodiscard]] int Order() const noexcept override { return 200; }

        void Modify(CameraPose& pose, const CameraAxes& axes) const noexcept override;
        [[nodiscard]] bool IsFinished() const noexcept override;

        /// 現在のフレームの寄りと傾き。完了後は zoom = 1, rollDegrees = 0
        [[nodiscard]] CameraZoomRoll Current() const noexcept;

    private:
        CameraZoomRollModifier() noexcept = default;

        [[nodiscard]] int ZoomFrame() const noexcept { return Frame() - m_delayFrames; }

        CameraZoomRoll m_full{};
        int m_holdFrames = 0;
        int m_returnFrames = 0;
        int m_delayFrames = 0;
    };

    /// 沈む・震える・保持・跳ね返りの縦揺れ。高さ 1080 基準の画素で、下が負
    struct CameraSinkDesc
    {
        float bottomPixels = 0.0f;   // 底の深さ (画素、0 以上)
        int sinkFrames = 2;          // 開始フレームを含む底到達までのフレーム数
        float tremblePixels = 0.0f;  // 震えの初期振幅 (画素)
        int trembleFrames = 6;
        int tremblePeriodFrames = 2; // 震え 1 往復のフレーム数
        int bounceStartFrame = 0;    // 開始フレームを 0 とした跳ね返り開始フレーム
        float overshootRatio = 0.5f; // 底の深さに対する上方向オーバーシュート比 (0 より大きく 1 未満)
        int bouncePeriodFrames = 8;  // 跳ね返り 1 往復のフレーム数
        int frames = 0;              // 開始フレームを含む総フレーム数
    };

    /// 開始を 0 とした frame の縦ずれ (画素、下が負)。範囲外は 0
    [[nodiscard]] float CameraSinkPixelsAt(const CameraSinkDesc& desc, int frame) noexcept;

    /// 射影後の画面を縦にずらす。カメラとの距離や入力方向には影響しない
    class CameraSinkModifier final : public CameraModifier
    {
    public:
        /// 非有限値・負の深さや振幅・frames <= 0・周期 < 1・overshootRatio が (0, 1) 外の場合は nullptr を返す
        [[nodiscard]] static std::unique_ptr<CameraSinkModifier> Create(const CameraSinkDesc& desc);

        [[nodiscard]] static const void* StaticKind() noexcept;
        [[nodiscard]] const void* Kind() const noexcept override { return StaticKind(); }
        [[nodiscard]] int Order() const noexcept override { return 100; }

        void Modify(CameraPose& pose, const CameraAxes& axes) const noexcept override;
        [[nodiscard]] bool IsFinished() const noexcept override;
        [[nodiscard]] bool IsShake() const noexcept override { return true; }

        /// 現在のフレームの縦のずれ (画素、下が負)。完了後は 0
        [[nodiscard]] float Pixels() const noexcept;

        /// 次の Advance フレームから跳ね返りを開始する (設定の bounceStartFrame より遅くはしない)
        void Release() noexcept { m_releasePending = true; }

    private:
        CameraSinkModifier() noexcept = default;
        void Advance() noexcept override;

        CameraSinkDesc m_desc{};
        bool m_releasePending = false;
    };

    /// 振れ幅は トラウマ^exponent × 最大角度。トラウマは時間で線形減衰する
    struct CameraTraumaShape
    {
        float yawDegrees = 0.0f;     // トラウマ 1 時の最大ヨー角 (度)
        float pitchDegrees = 0.0f;   // トラウマ 1 時の最大ピッチ角 (度)
        float rollDegrees = 0.0f;    // トラウマ 1 時の最大ロール角 (度)
        float frequency = 10.0f;     // ノイズ周波数 (1/秒)
        float decayPerSecond = 1.0f; // 減衰速度 (1/秒)
        float exponent = 2.0f;
    };

    /// peakFrames で最大になる、衝撃方向への減衰回転
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

    /// トラウマに応じて視線と傾きを揺らす。追加分は上限 1 まで重ね、ViewPose には適用しない
    class CameraTraumaModifier final : public CameraModifier
    {
    public:
        CameraTraumaModifier() noexcept = default;

        [[nodiscard]] static const void* StaticKind() noexcept;
        [[nodiscard]] const void* Kind() const noexcept override { return StaticKind(); }
        [[nodiscard]] int Order() const noexcept override { return 100; }

        /// トラウマを加算し、波形とシードを更新する。kick が有効なら再開する
        void AddTrauma(const CameraTraumaDesc& desc) noexcept;

        /// 今フレームのトラウマ下限を level (0〜1) に保つ。現在値以上の場合のみ波形を更新し、次フレームから減衰する
        void HoldTrauma(float level, const CameraTraumaShape& shape) noexcept;

        [[nodiscard]] float Trauma() const noexcept { return m_trauma; }
        /// トラウマ^exponent
        [[nodiscard]] float ShakeAmount() const noexcept;

        /// 現在のフレームの回転角 (度)。x: ヨー (右が正)、y: ピッチ (上が正)、z: ロール (上端右倒しが正)
        [[nodiscard]] Vector3 Angles() const noexcept;

        void Modify(CameraPose& pose, const CameraAxes& axes) const noexcept override;
        [[nodiscard]] bool IsFinished() const noexcept override;
        [[nodiscard]] bool IsShake() const noexcept override { return true; }

    private:
        void Advance() noexcept override;

        CameraTraumaShape m_shape{};
        float m_trauma = 0.0f;
        float m_held = 0.0f;
        uint32 m_seed = 0;
        CameraKick m_kick{};
        int m_kickFrame = 0;
    };

    /// 位置と注視点を同じだけずらす設定
    struct CameraNudgeDesc
    {
        Vector3 direction{};   // ずらす世界方向 (単位ベクトル)
        Curve distance{};      // 開始フレームを 0 とした移動距離 (m、負は逆方向)
        int frames = 0;        // 開始フレームを含む総フレーム数
        bool onScreen = false; // true ならカメラの右・上平面へ射影した方向へずらす
    };

    /// 位置と注視点を同じだけずらす。画面射影時の方向成分が微小なら動かさない
    /// 世界方向と画面方向は併用でき、同じ方式の再指定は前の効果を置き換える
    class CameraNudgeModifier final : public CameraModifier
    {
    public:
        /// 非有限値・direction が単位長でない・frames <= 0 の場合は nullptr を返す
        [[nodiscard]] static std::unique_ptr<CameraNudgeModifier> Create(const CameraNudgeDesc& desc);

        [[nodiscard]] static const void* KindFor(bool onScreen) noexcept;
        [[nodiscard]] const void* Kind() const noexcept override { return KindFor(m_desc.onScreen); }
        [[nodiscard]] int Order() const noexcept override { return 100; }

        void Modify(CameraPose& pose, const CameraAxes& axes) const noexcept override;
        [[nodiscard]] bool IsFinished() const noexcept override;
        [[nodiscard]] bool IsShake() const noexcept override { return true; }

        [[nodiscard]] const CameraNudgeDesc& Desc() const noexcept { return m_desc; }

        /// 現在のフレームの移動距離 (m)。完了後は 0
        [[nodiscard]] float Distance() const noexcept;

    private:
        CameraNudgeModifier() noexcept = default;

        CameraNudgeDesc m_desc{};
    };

    /// ヒット演出で開始する揺れ・沈み・寄り・ずれの Kind タグ一覧 (加算式のトラウマは含まない)
    [[nodiscard]] std::array<const void*, 5> CameraHitEffectKinds() noexcept;
} // namespace NS
