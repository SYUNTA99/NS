#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/Core/NonCopyable.h"
#include "NSlib/CoreObject/ClassObjs/VirtualCamera.h"
#include "NSlib/CoreObject/Reflection/Curve.h"

#include "NSlib/Core/Types.h"
#include <memory>
#include <vector>

namespace NS
{
    //! 効果を掛ける時の姿勢の軸。仮想カメラとブレンドまで済んだ姿勢の視線から作る。どちらも長さ 1
    struct CameraAxes
    {
        Vector3 right{1.0f, 0.0f, 0.0f}; // 視線の水平から作った右
        Vector3 up{0.0f, 1.0f, 0.0f};    // 視線と右から作った上
    };

    //! CameraManager が所有する姿勢の効果。Order 順に適用し、完了すると外す
    //! 最初の Tick は進めず、追加したフレームに初期姿勢を描く
    class CameraModifier : public NonCopyable
    {
    public:
        virtual ~CameraModifier() noexcept;

        //! @brief 同じ種類の効果かを見分ける印
        //! @details 管理役は同じ印の効果を積み直すと前の物を外す。nullptr の効果は幾つでも重ねて積める
        [[nodiscard]] virtual const void* Kind() const noexcept { return nullptr; }

        //! 掛ける順。小さいほど先に掛かる
        [[nodiscard]] virtual int Order() const noexcept { return 0; }

        //! 固定ステップで 1 つ進める。積んだ直後の 1 回は進めない
        void Tick() noexcept;

        //! 姿勢へ効果を掛ける。axes は効果を掛ける前の姿勢の軸
        virtual void Modify(CameraPose& pose, const CameraAxes& axes) const noexcept = 0;

        //! 効果を描き終えたか。真になった効果は管理役が外す
        [[nodiscard]] virtual bool IsFinished() const noexcept = 0;

        //! 揺れの効果の場合 true、それ以外の場合は false。揺れには管理役が設定の倍率 (SetShakeScale) を掛ける
        [[nodiscard]] virtual bool IsShake() const noexcept { return false; }

    protected:
        //! @brief Frame を 1 つ進めた後に呼ぶ。番号のほかに進める物がある効果だけが上書きする
        virtual void Advance() noexcept {}

        //! 積んだフレームを 0 とした番号
        [[nodiscard]] int Frame() const noexcept { return m_frame; }

    private:
        bool m_justAdded = true; // 積んだ後まだ Tick を通っていないか
        int m_frame = 0;
    };

    //! カメラの右と上へずらす揺れ。振れ幅は直線で減り、seed から反転間隔を決める
    //! 縦の初回は下向き。longestFlipFrames が 2 以上なら同じ間隔を連続させない
    struct CameraShakeDesc
    {
        float sideAmplitude = 0.0f;                       // 最初の振れの横の大きさ (m)
        float upAmplitude = 0.0f;                         // 最初の振れの縦の大きさ (m)
        int frames = 0;                                   // 揺れを描くフレーム数。始めたフレームを含む
        int longestFlipFrames = 1;                        // 向きが入れ替わるまでの最長フレーム数。横と縦の両方に掛かる
        Vector3 firstSideDirection{1.0f, 0.0f, 0.0f}; // 最初の横の振れを向ける世界の向き
        uint32 seed = 0;                              // 入れ替わりの間隔を選ぶ種
    };

    //! @brief 寄りと傾きの設定
    //! @details 始めたフレームから倍率と傾きを全部入れ、holdFrames の間保ち、returnFrames で滑らかに元へ戻す
    struct CameraZoomRollDesc
    {
        float zoom = 1.0f;                           // 画面に写る大きさの倍率。1 で寄らない
        float rollDegrees = 0.0f;                    // 視線の軸まわりの傾き (度)
        Vector3 rollDirection{1.0f, 0.0f, 0.0f}; // 画面の上端を倒す側を決める世界の向き
        int holdFrames = 0;                          // 倍率と傾きを保つフレーム数。始めたフレームを含む
        int returnFrames = 0;                        // 元へ戻すフレーム数
    };

    //! @brief 今のフレームの寄りと傾き
    struct CameraZoomRoll
    {
        float zoom = 1.0f;        // 画面に写る大きさの倍率
        float rollDegrees = 0.0f; // 視線の軸まわりの傾き (度)。正は画面の上端をカメラの右へ倒す向き
    };

    //! @brief 画面揺れ。position と target を同じだけ動かす平行移動なので視線の向きは回らない
    //! @details カメラ相対の入力が読む水平の前は揺れで変わらない
    class CameraShakeModifier final : public CameraModifier
    {
    public:
        //! @brief 揺れを作る
        //! @details 非数・負の振れ幅・フレーム数 0 以下・最長 1 未満は壊れた設定で nullptr
        //! @param[in] desc 揺れの形
        //! @param[in] firstSideSign 最初の横の振れの向き。負なら左、それ以外は右
        [[nodiscard]] static std::unique_ptr<CameraShakeModifier> Create(const CameraShakeDesc& desc,
                                                                         float firstSideSign);

        //! 揺れの種類の印
        [[nodiscard]] static const void* StaticKind() noexcept;
        [[nodiscard]] const void* Kind() const noexcept override { return StaticKind(); }

        //! 揺れはブレンドの直後、寄りと傾きより先に掛ける
        [[nodiscard]] int Order() const noexcept override { return 100; }

        void Modify(CameraPose& pose, const CameraAxes& axes) const noexcept override;
        [[nodiscard]] bool IsFinished() const noexcept override;
        [[nodiscard]] bool IsShake() const noexcept override { return true; }

        //! 今のフレームのずれ。x がカメラの右、y が上 (m)。描き終えたら 0
        [[nodiscard]] Vector2 Offset() const noexcept;

    private:
        CameraShakeModifier() noexcept = default;

        std::vector<Vector2> m_offsets; // フレームごとのずれ (m)。x が右、y が上
    };

    //! @brief 寄りと傾き。寄りは視野角、傾きは視線の軸まわりの上の向きで掛け、注視点 - 位置は変えない
    class CameraZoomRollModifier final : public CameraModifier
    {
    public:
        //! @brief 寄りと傾きを作る
        //! @details 倍率 1 未満か非数・傾きか向きが非数・フレーム数が負は壊れた設定で nullptr
        //! @param[in] desc 寄りと傾き
        //! @param[in] rollSign 傾きの向き。負なら上端を左へ、それ以外は右へ倒す
        [[nodiscard]] static std::unique_ptr<CameraZoomRollModifier> Create(const CameraZoomRollDesc& desc,
                                                                            float rollSign);

        //! 寄りと傾きの種類の印
        [[nodiscard]] static const void* StaticKind() noexcept;
        [[nodiscard]] const void* Kind() const noexcept override { return StaticKind(); }

        //! 揺れの後に掛ける
        [[nodiscard]] int Order() const noexcept override { return 200; }

        void Modify(CameraPose& pose, const CameraAxes& axes) const noexcept override;
        [[nodiscard]] bool IsFinished() const noexcept override;

        //! 今のフレームの寄りと傾き。描き終えたら倍率 1・傾き 0
        [[nodiscard]] CameraZoomRoll Current() const noexcept;

    private:
        CameraZoomRollModifier() noexcept = default;

        CameraZoomRoll m_full{}; // 保つ間の倍率と向きを付けた傾き
        int m_holdFrames = 0;    // 保つフレーム数
        int m_returnFrames = 0;  // 戻すフレーム数
    };

    //! 沈む・震える・保持・跳ね返りの縦揺れ。高さ 1080 基準の画素で、下が負
    struct CameraSinkDesc
    {
        float bottomPixels = 0.0f;   // 底の深さ (画素)。0 以上
        int sinkFrames = 2;          // 底へ届くまでのフレーム数。始めたフレームを含む
        float tremblePixels = 0.0f;  // 震えの最初の大きさ (画素)
        int trembleFrames = 6;       // 震えるフレーム数
        int tremblePeriodFrames = 2; // 震えの 1 往復のフレーム数
        int bounceStartFrame = 0;    // 跳ね返りが始まるフレーム。始めたフレームを 0 と数える
        float overshootRatio = 0.5f; // 跳ね返りで 0 を越えて上へ出る量の、底の深さに対する割合。0 より大きく 1 未満
        int bouncePeriodFrames = 8;  // 跳ね返りの 1 往復のフレーム数
        int frames = 0;              // 描くフレーム数。始めたフレームを含む
    };

    //! 開始を 0 とした frame の縦ずれ。画素単位で下が負、範囲外は 0
    [[nodiscard]] float CameraSinkPixelsAt(const CameraSinkDesc& desc, int frame) noexcept;

    //! 射影後の画面を縦にずらす。カメラとの距離や遊びの入力方向には影響しない
    class CameraSinkModifier final : public CameraModifier
    {
    public:
        //! @brief 沈む揺れを作る
        //! @details 非数・負の深さか震え・フレーム数 0 以下・1 往復 1 未満・行き過ぎの割合が 0〜1 の外は壊れた設定で
        //! nullptr
        [[nodiscard]] static std::unique_ptr<CameraSinkModifier> Create(const CameraSinkDesc& desc);

        //! 沈む揺れの種類の印
        [[nodiscard]] static const void* StaticKind() noexcept;
        [[nodiscard]] const void* Kind() const noexcept override { return StaticKind(); }

        //! 揺れはブレンドの直後、寄りと傾きより先に掛ける
        [[nodiscard]] int Order() const noexcept override { return 100; }

        void Modify(CameraPose& pose, const CameraAxes& axes) const noexcept override;
        [[nodiscard]] bool IsFinished() const noexcept override;
        [[nodiscard]] bool IsShake() const noexcept override { return true; }

        //! 今のフレームの縦のずれ (画素、下が負)。描き終えたら 0
        [[nodiscard]] float Pixels() const noexcept;

    private:
        CameraSinkModifier() noexcept = default;

        CameraSinkDesc m_desc{}; // 4 拍の形
    };

    //! @brief トラウマの揺れの形。場面ごとに持つ (外れ・溜め)
    //! @details 振れ幅は トラウマ^exponent × 最大の角度。トラウマは時間で直線に減る
    struct CameraTraumaShape
    {
        float yawDegrees = 0.0f;     // トラウマ 1 の横の首振りの最大 (度)
        float pitchDegrees = 0.0f;   // トラウマ 1 の縦の首振りの最大 (度)
        float rollDegrees = 0.0f;    // トラウマ 1 の傾きの最大 (度)
        float frequency = 10.0f;     // ノイズの格子を 1 秒に進める数。高いほど細かく揺れる
        float decayPerSecond = 1.0f; // トラウマが 1 秒に減る量
        float exponent = 2.0f;       // トラウマを振れ幅にする指数
    };

    //! peakFrames で最大になる、衝撃方向への減衰回転
    struct CameraKick
    {
        float degrees = 0.0f;    // 山の大きさ (度)
        int peakFrames = 1;      // 山のフレーム
        Vector2 direction{}; // 画面の上の向き。x が右、y が上。長さ 0 なら振らない
    };

    //! @brief トラウマを足す設定
    struct CameraTraumaDesc
    {
        float trauma = 0.0f;     // 足すトラウマ 0〜1。足した後も 1 で頭打ち
        CameraTraumaShape shape; // 揺れの形。足した後はこの形で揺れる
        uint32 seed = 0;     // ノイズの種。同じ当たりは同じ揺れになる
        CameraKick kick;         // 重ねる一撃。大きさ 0 なら重ねない
    };

    //! トラウマに応じて視線と傾きを揺らす。追加分は上限 1 まで重ね、遊びの ViewPose には適用しない
    class CameraTraumaModifier final : public CameraModifier
    {
    public:
        CameraTraumaModifier() noexcept = default;

        //! トラウマの揺れの種類の印
        [[nodiscard]] static const void* StaticKind() noexcept;
        [[nodiscard]] const void* Kind() const noexcept override { return StaticKind(); }

        //! 揺れなので平行移動の揺れと同じ順。寄りと傾きより先に掛ける
        [[nodiscard]] int Order() const noexcept override { return 100; }

        //! @brief トラウマを足し、形と種を置き換える。一撃があれば始め直す
        //! @param[in] desc 足す量と形
        void AddTrauma(const CameraTraumaDesc& desc) noexcept;

        //! 毎歩 level 0〜1 を下限に保つ。現在値以上の場合だけ形を置き換え、未指定の歩から減衰する
        void HoldTrauma(float level, const CameraTraumaShape& shape) noexcept;

        //! 今のトラウマ 0〜1
        [[nodiscard]] float Trauma() const noexcept { return m_trauma; }

        //! 今のトラウマから作った振れの大きさ。トラウマ^指数
        [[nodiscard]] float ShakeAmount() const noexcept;

        //! 今のフレームの角度 (度)。x が横の首振り (右が正)、y が縦の首振り (上が正)、z が傾き
        //! (上端が右へ倒れる向きが正)
        [[nodiscard]] Vector3 Angles() const noexcept;

        void Modify(CameraPose& pose, const CameraAxes& axes) const noexcept override;
        [[nodiscard]] bool IsFinished() const noexcept override;
        [[nodiscard]] bool IsShake() const noexcept override { return true; }

    private:
        void Advance() noexcept override;

        CameraTraumaShape m_shape{}; // 揺れの形
        float m_trauma = 0.0f;       // 今のトラウマ
        float m_held = 0.0f;         // このフレームに保つと頼まれたトラウマ。次の Advance で下ろす
        uint32 m_seed = 0;       // ノイズの種
        CameraKick m_kick{};         // 重ねている一撃
        int m_kickFrame = 0;         // 一撃のフレーム。足したフレームが 1、一撃が無ければ 0
    };

    //! @brief カメラのずれの設定
    //! @details 位置と注視点を同じだけずらすので、向きと地平線は変わらない
    struct CameraNudgeDesc
    {
        Vector3 direction{}; // ずらす世界の向き。長さ 1
        Curve distance{};        // 始めたフレームを 0 とした番号を横軸にした、ずらす距離 (m)。負は逆の向き
        int frames = 0;          // 描くフレーム数。始めたフレームを含む
        bool onScreen = false;   // 向きをカメラの右と上へ写した画面の上の向きでずらすか。偽は世界の向きのまま
    };

    //! 位置と注視点を同じだけずらす。画面上の方向がなければ動かさない
    //! 世界方向と画面方向は併用でき、同じ方式の再指定は前の効果を置き換える
    class CameraNudgeModifier final : public CameraModifier
    {
    public:
        //! @brief カメラのずれを作る
        //! @details 非数を含む・長さ 1 でない向き・フレーム数 0 以下・非数の点を持つ曲線は壊れた設定で nullptr
        [[nodiscard]] static std::unique_ptr<CameraNudgeModifier> Create(const CameraNudgeDesc& desc);

        [[nodiscard]] static const void* KindFor(bool onScreen) noexcept;
        [[nodiscard]] const void* Kind() const noexcept override { return KindFor(m_desc.onScreen); }

        //! 揺れなので平行移動の揺れと同じ順。寄りと傾きより先に掛ける
        [[nodiscard]] int Order() const noexcept override { return 100; }

        void Modify(CameraPose& pose, const CameraAxes& axes) const noexcept override;
        [[nodiscard]] bool IsFinished() const noexcept override;
        [[nodiscard]] bool IsShake() const noexcept override { return true; }

        //! 作った時の設定
        [[nodiscard]] const CameraNudgeDesc& Desc() const noexcept { return m_desc; }

        //! 今のフレームのずらす距離 (m)。描き終えたら 0
        [[nodiscard]] float Distance() const noexcept;

    private:
        CameraNudgeModifier() noexcept = default;

        CameraNudgeDesc m_desc{}; // 作った時の設定
    };
} // namespace NS
