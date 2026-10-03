#pragma once

#include "Game/Level/HitTier.h"
#include "Runtime/Object/Reflection/Curve.h"
#include "Runtime/Object/Reflection/Reflection.h"

#pragma warning(push, 0)
#include "ThirdParty/nlohmann/json.hpp"
#pragma warning(pop)

#include <cstddef>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace NS::Game::Level
{
    //! @brief 事象が起きる外れの向き。面の上の位置 (u, v) の絶対値の大きい方の軸と符号で決まる
    //! @details 番号はファイルに書かず、名前 (HitDirectionName) で書く
    enum class HitDirection
    {
        Any,   //!< どの向きの当たりでも起きる
        Right, //!< 自機から見て右の外れ
        Left,  //!< 自機から見て左の外れ
        Up,    //!< 上の外れ
        Down,  //!< 下の外れ
    };

    //! @brief 自機の止め。始まりから長さの間、自機の状態と身体を止める
    struct HitStopEvent
    {
        static constexpr std::string_view k_Name = "HitStop";     //!< ファイルに書く種類の名前
        static constexpr std::string_view k_Label = "自機の止め"; //!< パネルに出す名前
        //! 触れる前 (マイナスのフレーム) に置けるか。当たりの結果 (相手・反動・威力) を読む種類は置けない
        static constexpr bool k_BeforeContact = false;
    };

    //! @brief 自機の描く形の倍率。始まりからのフレーム数を横軸にした曲線 2 本で、縮み・潰れ・伸びを 1 本の流れで持つ
    //! @details 突進の向きの倍率は、当たりの水平の向きの軸成分の 2 乗で x と z へ混ぜる。貫通の当たりでは起こさない
    struct ShapeEvent
    {
        static constexpr std::string_view k_Name = "Shape";
        static constexpr std::string_view k_Label = "自機の形";
        static constexpr bool k_BeforeContact = true; //!< 触れる前の縮みを置く

        NS::Obj::Curve along{};  //!< 突進の向きの倍率。元の形が 1
        NS::Obj::Curve height{}; //!< 高さの倍率。元の形が 1

        NS_REFLECT_BEGIN(ShapeEvent, void)
        NS_REFLECT_FIELD(along, "突進の向きの倍率")
        NS_REFLECT_FIELD(height, "高さの倍率")
        NS_REFLECT_END_VALUE()
    };

    //! @brief 相手の止め。置かれていた相手を食い込ませ、長さの間だけ往復させて形を縮める
    struct TargetFreezeEvent
    {
        static constexpr std::string_view k_Name = "TargetFreeze";
        static constexpr std::string_view k_Label = "相手の止め";
        static constexpr bool k_BeforeContact = false;

        float pushInDistance = 0.06f; //!< 止めの頭で飛ぶ向きへ食い込ませる距離 (m)
        float swingAmplitude = 0.05f; //!< 往復の振れ幅 (m)。相手の質量 + 1 で割ってから渡す
        float squashThickness = 0.7f; //!< 相手の飛ぶ向きの厚みの倍率
        float squashHeight = 1.1f;    //!< 相手の高さの倍率

        NS_REFLECT_BEGIN(TargetFreezeEvent, void)
        NS_REFLECT_FIELD(pushInDistance, "食い込み距離")
        NS_REFLECT_FIELD(swingAmplitude, "往復の振れ幅")
        NS_REFLECT_FIELD(squashThickness, "潰れの厚み")
        NS_REFLECT_FIELD(squashHeight, "潰れの伸び上がり")
        NS_REFLECT_END_VALUE()
    };

    //! @brief 相手を飛ばす (貫通の当たりでは壊させる)。始まりのフレームに 1 回だけ起きる
    struct TargetLaunchEvent
    {
        static constexpr std::string_view k_Name = "TargetLaunch";
        static constexpr std::string_view k_Label = "相手を飛ばす";
        static constexpr bool k_BeforeContact = false;
    };

    //! @brief 自機の反動を始める (貫通の当たりでは突き抜ける速度を書く)。始まりのフレームに 1 回だけ起きる
    //! @details 自機は止めの始まりからこの事象まで止まったまま。この事象が無いタイムラインは止めの終わりで動き出す
    struct ReboundEvent
    {
        static constexpr std::string_view k_Name = "Rebound";
        static constexpr std::string_view k_Label = "反動を始める";
        static constexpr bool k_BeforeContact = false;
    };

    //! @brief カメラの揺れ。長さが揺れのフレーム数
    //! @details 最初の振れの大きさは 強さ × 威力 × 質量の効き で、横と縦の重みの向きへ分ける
    struct CameraShakeEvent
    {
        static constexpr std::string_view k_Name = "CameraShake";
        static constexpr std::string_view k_Label = "カメラの揺れ";
        static constexpr bool k_BeforeContact = false; //!< 振れは威力と質量で決まる

        float strength = 0.06f;    //!< 威力 1・質量の効き 1 の最初の振れの大きさ (m)
        float sideWeight = 0.0f;   //!< 振れの向きの横の重み
        float upWeight = 1.0f;     //!< 振れの向きの縦の重み
        int longestFlipFrames = 1; //!< 振れの向きが入れ替わるまでの最長フレーム数

        NS_REFLECT_BEGIN(CameraShakeEvent, void)
        NS_REFLECT_FIELD(strength, "強さ")
        NS_REFLECT_FIELD(sideWeight, "横の重み")
        NS_REFLECT_FIELD(upWeight, "縦の重み")
        NS_REFLECT_FIELD(longestFlipFrames, "入れ替わりの最長フレーム数")
        NS_REFLECT_END_VALUE()
    };

    //! @brief トラウマの揺れ (外れ)。始まりのフレームにトラウマを足し、外した側へ一撃を振る。始まりのフレームに 1
    //! 回だけ起きる
    //! @details 足すトラウマは 欄のトラウマ × 威力。揺れの長さはトラウマが減る速さで決まる。
    //! 続けて当てると前のトラウマに足される
    struct CameraTraumaEvent
    {
        static constexpr std::string_view k_Name = "CameraTrauma";
        static constexpr std::string_view k_Label = "トラウマの揺れ";
        static constexpr bool k_BeforeContact = false; //!< 量は威力、一撃の向きは面の上の位置で決まる

        float trauma = 0.9f;         //!< 威力 1 で足すトラウマ 0〜1
        float yawDegrees = 3.0f;     //!< トラウマ 1 の横の首振りの最大 (度)
        float pitchDegrees = 2.0f;   //!< トラウマ 1 の縦の首振りの最大 (度)
        float rollDegrees = 1.5f;    //!< トラウマ 1 の傾きの最大 (度)
        float frequency = 9.0f;      //!< ノイズの格子を 1 秒に進める数
        float decayPerSecond = 1.8f; //!< トラウマが 1 秒に減る量
        float exponent = 2.0f;       //!< トラウマを振れ幅にする指数
        float kickDegrees = 2.5f;    //!< 一撃の山の大きさ (度)
        int kickPeakFrames = 1;      //!< 一撃の山のフレーム。始まりのフレームを 1 と数える

        NS_REFLECT_BEGIN(CameraTraumaEvent, void)
        NS_REFLECT_FIELD(trauma, "トラウマ")
        NS_REFLECT_FIELD(yawDegrees, "横の首振り")
        NS_REFLECT_FIELD(pitchDegrees, "縦の首振り")
        NS_REFLECT_FIELD(rollDegrees, "傾き")
        NS_REFLECT_FIELD(frequency, "細かさ")
        NS_REFLECT_FIELD(decayPerSecond, "減る速さ")
        NS_REFLECT_FIELD(exponent, "指数")
        NS_REFLECT_FIELD(kickDegrees, "一撃の大きさ")
        NS_REFLECT_FIELD(kickPeakFrames, "一撃の山のフレーム")
        NS_REFLECT_END_VALUE()
    };

    //! @brief カメラの寄りと傾き。長さは保つフレーム数と戻すフレーム数の和
    struct ZoomRollEvent
    {
        static constexpr std::string_view k_Name = "ZoomRoll";
        static constexpr std::string_view k_Label = "寄りと傾き";
        static constexpr bool k_BeforeContact = false; //!< 傾きの向きは相手の飛ぶ向きで決まる

        float zoom = 1.15f;       //!< 寄りの倍率。1 は寄らない
        float rollDegrees = 3.0f; //!< 傾き (度)。正は画面の上端を、相手の飛ぶ向きがカメラの右なら右へ倒す
        int returnFrames = 6;     //!< 長さのうち、元へ戻すのに使う末尾のフレーム数

        NS_REFLECT_BEGIN(ZoomRollEvent, void)
        NS_REFLECT_FIELD(zoom, "寄りの倍率")
        NS_REFLECT_FIELD(rollDegrees, "傾き")
        NS_REFLECT_FIELD(returnFrames, "戻すフレーム数")
        NS_REFLECT_END_VALUE()
    };

    //! @brief パッドの振動。始まりからのフレーム数を横軸にした、左右のモーターの速さの曲線 2 本。長さの間だけ書く
    struct PadVibrationEvent
    {
        static constexpr std::string_view k_Name = "PadVibration";
        static constexpr std::string_view k_Label = "パッドの振動";
        static constexpr bool k_BeforeContact = false;

        NS::Obj::Curve left{};  //!< 左のモーターの速さ 0〜1
        NS::Obj::Curve right{}; //!< 右のモーターの速さ 0〜1

        NS_REFLECT_BEGIN(PadVibrationEvent, void)
        NS_REFLECT_FIELD(left, "左の速さ")
        NS_REFLECT_FIELD(right, "右の速さ")
        NS_REFLECT_END_VALUE()
    };

    //! @brief 画面の白い光。長さの間に始めの濃さから直線で薄れる
    struct FlashEvent
    {
        static constexpr std::string_view k_Name = "Flash";
        static constexpr std::string_view k_Label = "白い光";
        static constexpr bool k_BeforeContact = false;

        float alpha = 0.5f; //!< 始めの濃さ 0〜1

        NS_REFLECT_BEGIN(FlashEvent, void)
        NS_REFLECT_FIELD(alpha, "濃さ")
        NS_REFLECT_END_VALUE()
    };

    //! @brief 当たりのエフェクトを出す。始まりのフレームに 1 回だけ起きる
    struct HitEffectEvent
    {
        static constexpr std::string_view k_Name = "HitEffect";
        static constexpr std::string_view k_Label = "当たりのエフェクト";
        static constexpr bool k_BeforeContact = false; //!< 絵は当たりの記録から置く
    };

    //! @brief 相手の飛び出しのエフェクトを出す。始まりのフレームに 1 回だけ起きる
    struct FlightEffectEvent
    {
        static constexpr std::string_view k_Name = "FlightEffect";
        static constexpr std::string_view k_Label = "飛び出しのエフェクト";
        static constexpr bool k_BeforeContact = false;
    };

    //! @brief 段階的な明け。始まりのフレームに世界を遅くし、実時間で普段の速さへ戻す。始まりのフレームに 1 回だけ起きる
    struct GradualReleaseEvent
    {
        static constexpr std::string_view k_Name = "GradualRelease";
        static constexpr std::string_view k_Label = "段階的な明け";
        static constexpr bool k_BeforeContact = false;

        float startSpeed = 0.2f;    //!< 始まりの世界の速さ 0〜1
        float returnSeconds = 0.3f; //!< 普段の速さへ戻るまでの実時間 (秒)
        NS::Obj::Curve shape{};     //!< 戻り方。横軸は 0〜1 の経過の割合、縦軸は 0〜1 の戻りの割合。点が無ければ直線

        NS_REFLECT_BEGIN(GradualReleaseEvent, void)
        NS_REFLECT_FIELD(startSpeed, "始まりの速さ")
        NS_REFLECT_FIELD(returnSeconds, "戻る秒")
        NS_REFLECT_FIELD(shape, "戻り方")
        NS_REFLECT_END_VALUE()
    };

    //! @brief 事象の種類ごとの値。種類を足す時はここへ型を足す
    using HitEventValue = std::variant<HitStopEvent,
                                       ShapeEvent,
                                       TargetFreezeEvent,
                                       TargetLaunchEvent,
                                       ReboundEvent,
                                       CameraShakeEvent,
                                       ZoomRollEvent,
                                       PadVibrationEvent,
                                       FlashEvent,
                                       HitEffectEvent,
                                       FlightEffectEvent,
                                       GradualReleaseEvent,
                                       CameraTraumaEvent>;

    //! @brief タイムラインの 1 行。触れたフレームを 0 にしたフレーム数で、始まりと長さを持つ
    struct HitEvent
    {
        HitEventValue value{};                      //!< 種類と、その種類の値
        int start = 0;                              //!< 始まりのフレーム。触れる前はマイナス
        int length = 1;                             //!< 長さ (フレーム数)。0 以上
        HitDirection direction = HitDirection::Any; //!< 起きる外れの向き
    };

    //! @brief 当たり方 1 つの事象の並び。同じフレームに始まる事象は並びの順に起きる
    struct HitTimeline
    {
        std::vector<HitEvent> events; //!< 事象の並び
    };

    //! value の種類のファイルに書く名前
    [[nodiscard]] std::string_view HitEventTypeName(const HitEventValue& value) noexcept;

    //! value の種類のパネルに出す名前
    [[nodiscard]] std::string_view HitEventLabel(const HitEventValue& value) noexcept;

    //! value の種類を触れる前 (マイナスのフレーム) に置ける場合 true。種類の k_BeforeContact
    [[nodiscard]] bool CanStartBeforeContact(const HitEventValue& value) noexcept;

    //! @brief 種類の名前から、その種類の既定の値を作る
    //! @return 知らない名前の場合は std::nullopt
    [[nodiscard]] std::optional<HitEventValue> MakeHitEventValue(std::string_view typeName);

    //! value の種類のリフレクション。欄の無い種類は nullptr
    [[nodiscard]] const NS::Obj::ReflectionInfo* HitEventReflection(const HitEventValue& value) noexcept;

    //! value の今の種類の値を指す番地。HitEventReflection の欄を読み書きする時に渡す
    [[nodiscard]] void* HitEventFields(HitEventValue& value) noexcept;
    [[nodiscard]] const void* HitEventFields(const HitEventValue& value) noexcept;

    //! @brief 面の上の位置から外れの向きを決める
    //! @details 絶対値の大きい方の軸の符号で決め、絶対値が等しい時は左右にする
    //! @param[in] u 面の上の左右の位置。自機から見て右が正
    //! @param[in] v 面の上の上下の位置。上が正
    //! @return 右・左・上・下のどれか
    [[nodiscard]] HitDirection HitDirectionOf(float u, float v) noexcept;

    //! direction のファイルに書く名前
    [[nodiscard]] std::string_view HitDirectionName(HitDirection direction) noexcept;

    //! @brief ファイルに書く名前から向きを引く
    //! @return 知らない名前の場合は std::nullopt
    [[nodiscard]] std::optional<HitDirection> ParseHitDirection(std::string_view name) noexcept;

    //! @brief タイムラインのファイルの JSON を読む
    //! @details 種類の欄に無い鍵は 1 件ずつ警告し、残りを読む
    //! @param[in] doc ファイルの JSON
    //! @param[out] error 読めなかった場合に、読めなかった所を書く
    //! @return 読めた場合はタイムライン。版が違う・形が壊れている・知らない種類がある場合は std::nullopt
    [[nodiscard]] std::optional<HitTimeline> ParseHitTimeline(const nlohmann::json& doc, std::string& error);

    //! timeline をファイルの JSON の形 {"version": 1, "events": [...]} へ書き出す
    [[nodiscard]] nlohmann::json HitTimelineToJson(const HitTimeline& timeline);

    //! @brief 段のタイムラインのファイル名 (拡張子を除く)
    //! @return 真ん中は "center"、外れは "miss"。番号から作った段の外の値は空
    [[nodiscard]] std::string_view HitTimelineNameOf(HitTier tier) noexcept;

    //! @brief 当たりのタイムラインの置き場。1 つの段につき Directory()/<名前>.json を 1 つ持つ
    //! @details 初めて引いた時に Directory() の *.json を全て読む。壊れたファイルは読む時にエラーを 1 回出して持たない
    class HitTimelineLibrary
    {
    public:
        [[nodiscard]] static HitTimelineLibrary& Get();

        //! 読み書きするディレクトリ。既定は ContentRoot/Assets/HitTimelines
        [[nodiscard]] const std::string& Directory();

        //! ディレクトリを差し替えて読み直す。試しは一時ディレクトリへ向ける
        void SetDirectory(std::string directory);

        //! Directory() の *.json を全て読み直す
        void Reload();

        //! name のタイムライン。無いか壊れていれば nullptr
        [[nodiscard]] const HitTimeline* Find(std::string_view name);

        //! @brief tier の段のタイムライン
        //! @details 無いか壊れている段、段の外の値は nullptr で、段ごとに 1 回だけエラーを出す
        [[nodiscard]] const HitTimeline* FindForTier(HitTier tier);

        //! name のタイムラインを差し替える。ファイルへは書かない
        void Set(std::string_view name, HitTimeline timeline);

        //! name のタイムラインを Directory() へ書く。無いか書けなければ false
        [[nodiscard]] bool Save(std::string_view name);

    private:
        HitTimelineLibrary() = default;
        void EnsureLoaded();

        std::string m_directory;                                     // 空なら既定のディレクトリ
        std::map<std::string, HitTimeline, std::less<>> m_timelines; // 名前からタイムライン
        std::set<int> m_reportedTiers;                               // 引けないとエラーを出した段の番号
        bool m_loaded = false;
    };
} // namespace NS::Game::Level
