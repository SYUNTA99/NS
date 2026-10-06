#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/Core/NonCopyable.h"
#include "NSlib/Object/Components/VirtualCamera.h"
#include "NSlib/Object/Reflection/Curve.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace NS::Obj
{
    //! 効果を掛ける時の姿勢の軸。仮想カメラとブレンドまで済んだ姿勢の視線から作る。どちらも長さ 1
    struct CameraAxes
    {
        NS::Vector3 right{1.0f, 0.0f, 0.0f}; // 視線の水平から作った右
        NS::Vector3 up{0.0f, 1.0f, 0.0f};    // 視線と右から作った上
    };

    //! @brief カメラの姿勢へ後から掛ける効果 1 つ。UE の UCameraModifier に当たる
    //! @details CameraManager が積んで持ち、仮想カメラとブレンドの後に Order の小さい順に掛ける
    //! 効果を足す側は管理役の中身を触らず、モディファイアを 1 つ積むだけでよい
    //! 固定ステップごとに Tick で進み、IsFinished が真になると管理役が外す
    //! 積んだ後の最初の Tick では進めない。積んだフレームに最初の姿を描く
    class CameraModifier : public NS::NonCopyable
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
        //! 1 フレーム進める
        virtual void Advance() noexcept = 0;

    private:
        bool m_justAdded = true; // 積んだ後まだ Tick を通っていないか
    };

    //! @brief 画面揺れの形の設定
    //! @details ずれはカメラの右と上の向きへの平行移動
    //! 大きさは始めたフレームが最大で、残りのフレーム数に比例して直線に減る
    //! 横と縦の向きはそれぞれ 1〜longestFlipFrames フレームごとに入れ替わる
    //! longestFlipFrames が 2 以上なら続けて同じ間隔にならない
    //! 間隔は seed から選び、横と縦は別の並びになる。縦の最初の振れは下
    struct CameraShakeDesc
    {
        float sideAmplitude = 0.0f; // 最初の振れの横の大きさ (m)
        float upAmplitude = 0.0f;   // 最初の振れの縦の大きさ (m)
        int frames = 0;             // 揺れを描くフレーム数。始めたフレームを含む
        int longestFlipFrames = 1;  // 向きが入れ替わるまでの最長フレーム数。横と縦の両方に掛かる
        NS::Vector3 firstSideDirection{1.0f, 0.0f, 0.0f}; // 最初の横の振れを向ける世界の向き
        std::uint32_t seed = 0;                                 // 入れ替わりの間隔を選ぶ種
    };

    //! @brief 寄りと傾きの設定
    //! @details 始めたフレームから倍率と傾きを全部入れ、holdFrames の間保ち、returnFrames で滑らかに元へ戻す
    struct CameraZoomRollDesc
    {
        float zoom = 1.0f;                                 // 画面に写る大きさの倍率。1 で寄らない
        float rollDegrees = 0.0f;                          // 視線の軸まわりの傾き (度)
        NS::Vector3 rollDirection{1.0f, 0.0f, 0.0f}; // 画面の上端を倒す側を決める世界の向き
        int holdFrames = 0;                                // 倍率と傾きを保つフレーム数。始めたフレームを含む
        int returnFrames = 0;                              // 元へ戻すフレーム数
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
        [[nodiscard]] NS::Vector2 Offset() const noexcept;

    private:
        CameraShakeModifier() noexcept = default;
        void Advance() noexcept override;

        std::vector<NS::Vector2> m_offsets; // フレームごとのずれ (m)。x が右、y が上
        int m_frame = 0;                          // m_offsets の今のフレームの番号
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
        void Advance() noexcept override;

        CameraZoomRoll m_full{}; // 保つ間の倍率と向きを付けた傾き
        int m_holdFrames = 0;    // 保つフレーム数
        int m_returnFrames = 0;  // 戻すフレーム数
        int m_frame = 0;         // 始めたフレームからの番号。保つと戻すの和に達したら終わり
    };

    //! @brief 沈む揺れ (真ん中) の 4 拍の形
    //! @details 画面の縦のずれを、高さ 1080 の画面の画素で持つ。下が負。
    //! 沈む: 始めたフレームから sinkFrames で底へ (sin の 4 分の 1 周)。
    //! 震える: 続く trembleFrames の間、底を中心に tremblePixels から直線に弱まる cos で揺れる。
    //! こらえる: 跳ね返りの頭まで底のまま。
    //! 跳ね返る: bounceStartFrame から、1 往復 bouncePeriodFrames・行き過ぎの割合 overshootRatio の減衰振動で 0 へ戻る
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

    //! @brief 沈む揺れの frame フレーム目の縦のずれを返す
    //! @param[in] desc 4 拍の形
    //! @param[in] frame 始めたフレームを 0 とした番号
    //! @return 縦のずれ (画素、下が負)。0 未満か frames 以上のフレームは 0
    [[nodiscard]] float CameraSinkPixelsAt(const CameraSinkDesc& desc, int frame) noexcept;

    //! @brief 沈む揺れ (真ん中)。射影の後の画面をずらすので、カメラの位置と向きは変えない
    //! @details 姿の画面のずれ (CameraPose::screenOffset) の縦に足す。カメラとの距離で揺れの画素数が変わらず、
    //! 遊びが読む前の向きも変わらない
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
        void Advance() noexcept override;

        CameraSinkDesc m_desc{}; // 4 拍の形
        int m_frame = 0;         // 始めたフレームからの番号
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

    //! @brief 衝撃の向きへカメラを一度振って戻す一撃
    //! @details 角度は degrees × (f ÷ peakFrames) × e^(1 − f ÷ peakFrames)。f は足したフレームを 1 と数える。
    //! f が peakFrames のフレームに最大になり、その後は滑らかに戻る
    struct CameraKick
    {
        float degrees = 0.0f;          // 山の大きさ (度)
        int peakFrames = 1;            // 山のフレーム
        NS::Vector2 direction{}; // 画面の上の向き。x が右、y が上。長さ 0 なら振らない
    };

    //! @brief トラウマを足す設定
    struct CameraTraumaDesc
    {
        float trauma = 0.0f;     // 足すトラウマ 0〜1。足した後も 1 で頭打ち
        CameraTraumaShape shape; // 揺れの形。足した後はこの形で揺れる
        std::uint32_t seed = 0;  // ノイズの種。同じ当たりは同じ揺れになる
        CameraKick kick;         // 重ねる一撃。大きさ 0 なら重ねない
    };

    //! @brief トラウマの揺れ。カメラの位置は動かさず、視線を横と縦に首振りし、傾ける
    //! @details 角度は種と時刻から引く値ノイズ (ValueNoise1D) × トラウマ^指数 × 最大の角度に、一撃の角度を足す。
    //! 隣り合うフレームでずれが跳ばない。続けて足すとトラウマが足される (上限 1)。
    //! 遊びが読む向き (CameraManager::ViewPose) には掛からない
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

        //! @brief このフレームのトラウマを少なくとも level に保つ
        //! @details 毎フレーム呼ぶ。呼ばなかったフレームからは減り始める。level
        //! が今のトラウマ以上の時だけ形を置き換える
        //! @param[in] level 保つトラウマ 0〜1
        //! @param[in] shape 揺れの形
        void HoldTrauma(float level, const CameraTraumaShape& shape) noexcept;

        //! 今のトラウマ 0〜1
        [[nodiscard]] float Trauma() const noexcept { return m_trauma; }

        //! 今のトラウマから作った振れの大きさ。トラウマ^指数
        [[nodiscard]] float ShakeAmount() const noexcept;

        //! 今のフレームの角度 (度)。x が横の首振り (右が正)、y が縦の首振り (上が正)、z が傾き
        //! (上端が右へ倒れる向きが正)
        [[nodiscard]] NS::Vector3 Angles() const noexcept;

        void Modify(CameraPose& pose, const CameraAxes& axes) const noexcept override;
        [[nodiscard]] bool IsFinished() const noexcept override;
        [[nodiscard]] bool IsShake() const noexcept override { return true; }

    private:
        void Advance() noexcept override;

        CameraTraumaShape m_shape{}; // 揺れの形
        float m_trauma = 0.0f;       // 今のトラウマ
        float m_held = 0.0f;         // このフレームに保つと頼まれたトラウマ。次の Advance で下ろす
        std::uint32_t m_seed = 0;    // ノイズの種
        int m_frame = 0;             // 積んでから進めたフレーム数。ノイズの時刻
        CameraKick m_kick{};         // 重ねている一撃
        int m_kickFrame = 0;         // 一撃のフレーム。足したフレームが 1、一撃が無ければ 0
    };

    //! @brief カメラのずれの設定
    //! @details 位置と注視点を同じだけずらすので、向きと地平線は変わらない
    struct CameraNudgeDesc
    {
        NS::Vector3 direction{}; // ずらす世界の向き。長さ 1
        Curve distance{}; // 始めたフレームを 0 とした番号を横軸にした、ずらす距離 (m)。負は逆の向き
        int frames = 0;   // 描くフレーム数。始めたフレームを含む
        bool onScreen = false; // 向きをカメラの右と上へ写した画面の上の向きでずらすか。偽は世界の向きのまま
    };

    //! @brief カメラのずれ。位置と注視点を、決まった向きへ曲線の距離だけ同じだけずらす
    //! @details 画面へ写す時は奥へ向かう分を捨て、画面の上で見える向きだけでずらす。写した向きがほぼ無い
    //! (向きが真っすぐ画面の奥か手前を指す) 時は動かさない。種類の印は向きの取り方 (世界 / 画面) ごとに分かれ、
    //! 同じ取り方のずれを積み直すと前の物が外れる。取り方の違うずれは重なる
    class CameraNudgeModifier final : public CameraModifier
    {
    public:
        //! @brief カメラのずれを作る
        //! @details 非数を含む・長さ 1 でない向き・フレーム数 0 以下・非数の点を持つ曲線は壊れた設定で nullptr
        [[nodiscard]] static std::unique_ptr<CameraNudgeModifier> Create(const CameraNudgeDesc& desc);

        //! @brief 向きの取り方ごとの種類の印を返す
        //! @param[in] onScreen 画面へ写すずれか
        //! @return 種類の印
        [[nodiscard]] static const void* KindFor(bool onScreen) noexcept;

        //! 世界の向きのままずらす物の種類の印
        [[nodiscard]] static const void* StaticKind() noexcept { return KindFor(false); }
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
        void Advance() noexcept override;

        CameraNudgeDesc m_desc{}; // 作った時の設定
        int m_frame = 0;          // 始めたフレームからの番号
    };
} // namespace NS::Obj
