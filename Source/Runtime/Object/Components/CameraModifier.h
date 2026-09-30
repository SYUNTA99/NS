#pragma once

#include "Runtime/Core/Math.h"
#include "Runtime/Core/NonCopyable.h"
#include "Runtime/Object/Components/VirtualCamera.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace NS::Obj
{
    //! 効果を掛ける時の姿勢の軸。仮想カメラとブレンドまで済んだ姿勢の視線から作る。どちらも長さ 1
    struct CameraAxes
    {
        NS::Core::Vector3 right{1.0f, 0.0f, 0.0f}; // 視線の水平から作った右
        NS::Core::Vector3 up{0.0f, 1.0f, 0.0f};    // 視線と右から作った上
    };

    //! @brief カメラの姿勢へ後から掛ける効果 1 つ。UE の UCameraModifier に当たる
    //! @details CameraManager が積んで持ち、仮想カメラとブレンドの後に Order の小さい順に掛ける
    //! 効果を足す側は管理役の中身を触らず、モディファイアを 1 つ積むだけでよい
    //! 固定ステップごとに Tick で進み、IsFinished が真になると管理役が外す
    //! 積んだ後の最初の Tick では進めない。積んだフレームに最初の姿を描く
    class CameraModifier : public NS::Core::NonCopyable
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
        NS::Core::Vector3 firstSideDirection{1.0f, 0.0f, 0.0f}; // 最初の横の振れを向ける世界の向き
        std::uint32_t seed = 0;                                 // 入れ替わりの間隔を選ぶ種
    };

    //! @brief 寄りと傾きの設定
    //! @details 始めたフレームから倍率と傾きを全部入れ、holdFrames の間保ち、returnFrames で滑らかに元へ戻す
    struct CameraZoomRollDesc
    {
        float zoom = 1.0f;                                 // 画面に写る大きさの倍率。1 で寄らない
        float rollDegrees = 0.0f;                          // 視線の軸まわりの傾き (度)
        NS::Core::Vector3 rollDirection{1.0f, 0.0f, 0.0f}; // 画面の上端を倒す側を決める世界の向き
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

        //! 今のフレームのずれ。x がカメラの右、y が上 (m)。描き終えたら 0
        [[nodiscard]] NS::Core::Vector2 Offset() const noexcept;

    private:
        CameraShakeModifier() noexcept = default;
        void Advance() noexcept override;

        std::vector<NS::Core::Vector2> m_offsets; // フレームごとのずれ (m)。x が右、y が上
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
} // namespace NS::Obj
