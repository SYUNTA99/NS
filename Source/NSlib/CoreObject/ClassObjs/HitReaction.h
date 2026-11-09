#pragma once

#include "NSlib/Core/NonCopyable.h"
#include "NSlib/CoreObject/ClassObjs/CameraModifier.h"
#include "NSlib/CoreObject/Object.h"
#include "NSlib/CoreObject/Reflection/AssetField.h"
#include "NSlib/CoreObject/Reflection/Curve.h"
#include "NSlib/CoreObject/Reflection/Reflection.h"
#include "NSlib/CoreObject/Reflection/SoundName.h"
#include "NSlib/CoreObject/System/PadRumbleDirector.h"

#pragma warning(push, 0)
#include "ThirdParty/nlohmann/json.hpp"
#pragma warning(pop)

#include <concepts>
#include <functional>
#include <initializer_list>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace NS
{
    struct HitReactionStart;

    // 名前の束が使う項目の型。曲線の横軸は項目が始まったフレームを 0 にしたフレーム数。
    // 向きを使う項目は、束を始める時に渡す 2 つのセンサーの位置から向きを出す。
    // 種類の型は NS_HIT_REACTION_ITEM で登録し、束を始めたフレームに Start が 1 回呼ばれる

    //! カメラのトラウマを足し、相手の側へ一撃を振る。揺れの途中なら前のトラウマに足す
    //! 揺れの長さは項目の長さでなく減る速さで決まる
    struct HitCameraTraumaInfo
    {
        std::string_view typeName = "CameraTrauma";
        std::string_view label = "トラウマの揺れ";

        void Start(const HitReactionStart& start) const;

        float trauma = 0.9f;         // 足すトラウマ 0〜1
        float yawDegrees = 3.0f;     // トラウマ 1 の横の首振りの最大 (度)
        float pitchDegrees = 2.0f;   // トラウマ 1 の縦の首振りの最大 (度)
        float rollDegrees = 1.5f;    // トラウマ 1 の傾きの最大 (度)
        float frequency = 9.0f;      // ノイズの格子を 1 秒に進める数
        float decayPerSecond = 1.8f; // トラウマが 1 秒に減る量
        float exponent = 2.0f;       // トラウマを振れ幅にする指数
        float kickDegrees = 2.5f;    // 一撃の山の大きさ (度)
        int kickPeakFrames = 1;      // 一撃の山のフレーム。始めたフレームを 1 と数える

        NS_REFLECT_BEGIN(HitCameraTraumaInfo, void)
        NS_FIELD(trauma, "トラウマ")
        NS_FIELD(yawDegrees, "横の首振り")
        NS_FIELD(pitchDegrees, "縦の首振り")
        NS_FIELD(rollDegrees, "傾き")
        NS_FIELD(frequency, "細かさ")
        NS_FIELD(decayPerSecond, "減る速さ")
        NS_FIELD(exponent, "指数")
        NS_FIELD(kickDegrees, "一撃の大きさ")
        NS_FIELD(kickPeakFrames, "一撃の山のフレーム")
        NS_REFLECT_END_VALUE()
    };

    //! パッドの振動。左右のモーターの速さの曲線で、長さの間だけ書く
    //! 向き別の時は、持ち主から見た相手の向きの角度で隣り合う 2 つの向きの曲線を混ぜる。
    //! 向きは右・左・上・下で、向き無しの曲線は使わない
    struct HitPadInfo
    {
        std::string_view typeName = "Pad";
        std::string_view label = "パッドの振動";

        void Start(const HitReactionStart& start) const;

        bool byDirection = false; // 向き別の 4 組を使う場合 true。偽なら向き無しの 1 組
        bool layered = false;     // 走っている振動を始め直さずに重ねる場合 true
        Curve left{};             // 向き無しの左のモーターの速さ 0〜1
        Curve right{};            // 向き無しの右のモーターの速さ 0〜1
        Curve rightLeft{};        // 相手が右の時の左のモーター
        Curve rightRight{};
        Curve leftLeft{}; // 相手が左の時の左のモーター
        Curve leftRight{};
        Curve upLeft{}; // 相手が上の時の左のモーター
        Curve upRight{};
        Curve downLeft{}; // 相手が下の時の左のモーター
        Curve downRight{};

        NS_REFLECT_BEGIN(HitPadInfo, void)
        NS_FIELD(byDirection, "向き別")
        NS_FIELD(layered, "前の振動に重ねる")
        NS_FIELD(left, "左の速さ")
        NS_FIELD(right, "右の速さ")
        NS_FIELD(rightLeft, "右の時の左の速さ")
        NS_FIELD(rightRight, "右の時の右の速さ")
        NS_FIELD(leftLeft, "左の時の左の速さ")
        NS_FIELD(leftRight, "左の時の右の速さ")
        NS_FIELD(upLeft, "上の時の左の速さ")
        NS_FIELD(upRight, "上の時の右の速さ")
        NS_FIELD(downLeft, "下の時の左の速さ")
        NS_FIELD(downRight, "下の時の右の速さ")
        NS_REFLECT_END_VALUE()
    };

    //! 持ち主のアクターへ渡す項目。名前と値を持ち主が読んで、持ち主の演出 (残像・エフェクトなど) を始める
    //! NSlib は名前と値の意味を知らない。値の名前は持ち主が決める
    struct HitOwnerInfo
    {
        std::string_view typeName = "Owner";
        std::string_view label = "持ち主へ渡す項目";

        void Start(const HitReactionStart& start) const;

        std::string name; // 持ち主が始める演出の名前
        //! 値の名前から値。ファイルでは欄の「値」に書き、インスペクタの欄の表には載らない
        std::map<std::string, float, std::less<>> values;

        //! 欄の「値」を fields から values へ読み、鍵を fields から外す。数でない値があれば error に書いて false
        [[nodiscard]] static bool ReadExtraFields(HitOwnerInfo& item, nlohmann::json& fields, std::string& error);
        //! values を欄の「値」として fields へ書く
        static void WriteExtraFields(const HitOwnerInfo& item, nlohmann::json& fields);

        NS_REFLECT_BEGIN(HitOwnerInfo, void)
        NS_FIELD(name, "名前")
        NS_REFLECT_END_VALUE()
    };

    //! 持ち主の Sound で効果音を鳴らす。動作の項目 ActionSoundInfo と同じ頼む先
    struct HitSoundInfo
    {
        std::string_view typeName = "Sound";
        std::string_view label = "効果音";

        void Start(const HitReactionStart& start) const;

        SoundName sound{};   // 鳴らす音
        float volume = 1.0f; // 音量 0〜1

        NS_REFLECT_BEGIN(HitSoundInfo, void)
        NS_FIELD(sound, "音")
        NS_FIELD(volume, "音量")
        NS_REFLECT_END_VALUE()
    };

    //! 持ち主の EffectObj で、束を始めた位置にエフェクトを出す。動作の項目 ActionEffectInfo と同じ頼む先
    struct HitEffectInfo
    {
        std::string_view typeName = "Effect";
        std::string_view label = "エフェクト";

        void Start(const HitReactionStart& start) const;

        Effect* effect = nullptr; // 出すエフェクト

        NS_REFLECT_BEGIN(HitEffectInfo, void)
        NS_FIELD(effect, "エフェクト")
        NS_REFLECT_END_VALUE()
    };

    //! 束の項目にできる種類の型。ファイルの種類の名前 typeName・エディタに出す名前 label と、
    //! 束を始めたフレームに 1 回呼ばれる Start を持つ。欄は NS_REFLECT で並べる
    template <class Info>
    concept HitReactionItemInfo = std::copy_constructible<Info> && std::default_initializable<Info> &&
                                  requires(const Info& info, const HitReactionStart& start) {
                                      { info.typeName } -> std::convertible_to<std::string_view>;
                                      { info.label } -> std::convertible_to<std::string_view>;
                                      info.Start(start);
                                  };

    //! 種類の型ごとに 1 つの番地。HitReactionItem::As が今の種類を見分けるのに使う
    //! const にしない。読むだけの同じ値は、最適化したリンクで 1 つの番地にまとめられることがある (/OPT:ICF)
    template <class Info> [[nodiscard]] const void* HitReactionItemTag() noexcept
    {
        static char tag = 0;
        return &tag;
    }

    //! 束の項目 1 つの値。登録した種類の型のどれか 1 つを持つ。既定で作った値は種類を持たず、束には入らない
    class HitReactionItem
    {
    public:
        HitReactionItem() = default;
        template <HitReactionItemInfo Info>
        HitReactionItem(Info info) : m_value(std::make_unique<Holder<Info>>(std::move(info)))
        {
        }
        HitReactionItem(const HitReactionItem& other);
        HitReactionItem& operator=(const HitReactionItem& other);
        HitReactionItem(HitReactionItem&&) noexcept = default;
        HitReactionItem& operator=(HitReactionItem&&) noexcept = default;
        ~HitReactionItem() = default;

        //! ファイルに書く種類の名前。種類を持たなければ空
        [[nodiscard]] std::string_view TypeName() const noexcept;
        //! エディタに出す名前。種類を持たなければ空
        [[nodiscard]] std::string_view Label() const noexcept;
        //! 今の種類のリフレクション。欄の無い種類と、種類を持たない値は nullptr
        [[nodiscard]] const ReflectionInfo* Reflection() const noexcept;
        //! 今の種類の値を指す番地。Reflection の欄を読み書きする時に渡す。種類を持たなければ nullptr
        [[nodiscard]] void* Fields() noexcept;
        [[nodiscard]] const void* Fields() const noexcept;
        //! 今の種類の Start を呼ぶ。種類を持たなければ何もしない
        void Start(const HitReactionStart& start) const;
        //! 今の種類が欄の表の外に持つ値を fields から読み、読んだ鍵を外す。壊れていれば error に書いて false
        [[nodiscard]] bool ReadExtraFields(nlohmann::json& fields, std::string& error);
        //! 今の種類が欄の表の外に持つ値を fields へ書く
        void WriteExtraFields(nlohmann::json& fields) const;

        //! 今の種類が Info ならその値。違えば nullptr
        template <HitReactionItemInfo Info> [[nodiscard]] const Info* As() const noexcept
        {
            if (m_value == nullptr || m_value->Tag() != HitReactionItemTag<Info>())
            {
                return nullptr;
            }
            return &static_cast<const Holder<Info>*>(m_value.get())->info;
        }
        template <HitReactionItemInfo Info> [[nodiscard]] Info* As() noexcept
        {
            if (m_value == nullptr || m_value->Tag() != HitReactionItemTag<Info>())
            {
                return nullptr;
            }
            return &static_cast<Holder<Info>*>(m_value.get())->info;
        }

    private:
        struct Value
        {
            Value() = default;
            Value(const Value&) = delete;
            Value& operator=(const Value&) = delete;
            Value(Value&&) = delete;
            Value& operator=(Value&&) = delete;
            virtual ~Value() = default;
            [[nodiscard]] virtual std::unique_ptr<Value> Clone() const = 0;
            [[nodiscard]] virtual const void* Tag() const noexcept = 0;
            [[nodiscard]] virtual std::string_view TypeName() const noexcept = 0;
            [[nodiscard]] virtual std::string_view Label() const noexcept = 0;
            [[nodiscard]] virtual const ReflectionInfo* Reflection() const noexcept = 0;
            [[nodiscard]] virtual void* Fields() noexcept = 0;
            [[nodiscard]] virtual const void* Fields() const noexcept = 0;
            virtual void Start(const HitReactionStart& start) const = 0;
            [[nodiscard]] virtual bool ReadExtraFields(nlohmann::json& fields, std::string& error) = 0;
            virtual void WriteExtraFields(nlohmann::json& fields) const = 0;
        };

        template <class Info> struct Holder final : Value
        {
            explicit Holder(Info value) : info(std::move(value)) {}
            [[nodiscard]] std::unique_ptr<Value> Clone() const override { return std::make_unique<Holder>(info); }
            [[nodiscard]] const void* Tag() const noexcept override { return HitReactionItemTag<Info>(); }
            [[nodiscard]] std::string_view TypeName() const noexcept override { return info.typeName; }
            [[nodiscard]] std::string_view Label() const noexcept override { return info.label; }
            [[nodiscard]] const ReflectionInfo* Reflection() const noexcept override
            {
                if constexpr (requires { Info::StaticReflection(); })
                {
                    return Info::StaticReflection();
                }
                else
                {
                    return nullptr;
                }
            }
            [[nodiscard]] void* Fields() noexcept override { return &info; }
            [[nodiscard]] const void* Fields() const noexcept override { return &info; }
            void Start(const HitReactionStart& start) const override { info.Start(start); }
            // 欄の表の外に値を持つ種類だけが、静的関数 ReadExtraFields と WriteExtraFields を持つ
            [[nodiscard]] bool ReadExtraFields(nlohmann::json& fields, std::string& error) override
            {
                if constexpr (requires { Info::ReadExtraFields(info, fields, error); })
                {
                    return Info::ReadExtraFields(info, fields, error);
                }
                else
                {
                    return true;
                }
            }
            void WriteExtraFields(nlohmann::json& fields) const override
            {
                if constexpr (requires { Info::WriteExtraFields(info, fields); })
                {
                    Info::WriteExtraFields(info, fields);
                }
            }

            Info info;
        };

        std::unique_ptr<Value> m_value;
    };

    // .actor の項目の行を読み書きする共通の関数 (ItemVariant.h) が、HitReactionItem を std::variant と同じに扱うための関数
    [[nodiscard]] inline std::string_view ItemTypeName(const HitReactionItem& item) noexcept { return item.TypeName(); }
    [[nodiscard]] inline std::string_view ItemLabel(const HitReactionItem& item) noexcept { return item.Label(); }
    [[nodiscard]] inline const ReflectionInfo* ItemReflection(const HitReactionItem& item) noexcept
    {
        return item.Reflection();
    }
    [[nodiscard]] inline void* ItemFields(HitReactionItem& item) noexcept { return item.Fields(); }
    [[nodiscard]] inline const void* ItemFields(const HitReactionItem& item) noexcept { return item.Fields(); }
    //! 登録した種類の名前から、欄が全部既定値のその種類の値を作る。登録していない名前は std::nullopt
    [[nodiscard]] std::optional<HitReactionItem> MakeItemOf(std::string_view typeName,
                                                            std::type_identity<HitReactionItem>);

    //! 登録した項目の種類 1 つ
    struct HitReactionItemKind
    {
        std::string_view typeName;
        HitReactionItem (*make)() = nullptr; // 欄が全部既定値のその種類の値を作る
    };

    //! 束の項目の種類の表。NSlib と Game の種類の型が NS_HIT_REACTION_ITEM で自分を登録し、束を読む時に名前で引く
    class HitReactionItemRegistry
    {
    public:
        [[nodiscard]] static HitReactionItemRegistry& Get() noexcept;

        //! 同じ名前の二重登録は先勝ちで断り、debug では assert で落とす
        void Register(const HitReactionItemKind& kind);

        //! 名前の種類。無ければ nullptr
        [[nodiscard]] const HitReactionItemKind* Find(std::string_view typeName) const noexcept;

        //! 登録した種類。登録の順
        [[nodiscard]] const std::vector<HitReactionItemKind>& Kinds() const noexcept { return m_kinds; }

    private:
        HitReactionItemRegistry() = default;
        std::vector<HitReactionItemKind> m_kinds; // 登録の順。種類は数十なので線形に引く
    };

    //! NS_HIT_REACTION_ITEM の実体
    template <HitReactionItemInfo Info> void RegisterHitReactionItem()
    {
        HitReactionItemRegistry::Get().Register(HitReactionItemKind{
            .typeName = Info{}.typeName, .make = +[]() -> HitReactionItem { return HitReactionItem{Info{}}; }});
    }

    //! 束の 1 項目。束を始めたフレームに始まり、長さのフレーム数だけ続く
    struct HitReactionEntry
    {
        HitReactionItem item{};
        int length = 1; // 長さ (フレーム数)。0 以上
    };

    //! 名前の束。始めると全部の項目が同じフレームに始まり、止めの長さの間は持ち主の状態と体が止まる
    //! 項目の値は書いた値のまま使い、始める時に書き換えない
    struct HitReactionSet
    {
        std::string name;
        int stopFrames = 0; // 持ち主の止めのフレーム数。0 以上
        std::vector<HitReactionEntry> entries;
    };

    //! 束 1 つを JSON の形 {"name", "stopFrames", "items": [{"type", "length", "fields"}]} から読む
    //! 欠けた欄は既定のまま、知らない欄は WARN を出して飛ばす
    //! @return 名前が空・止めの長さか項目の長さが負・知らない種類の場合は std::nullopt で、error に理由を書く
    [[nodiscard]] std::optional<HitReactionSet> ParseHitReactionSet(const nlohmann::json& doc, std::string& error);

    [[nodiscard]] nlohmann::json HitReactionSetToJson(const HitReactionSet& set);

    //! .actor の文書の束の並びを読む
    //! 壊れた束と、前の束と名前の重なる束は、理由を ERROR に出して読まない
    //! @param[in] actorFileName ERROR に出す .actor の名前
    [[nodiscard]] std::vector<HitReactionSet> ReadHitReactionSets(std::string_view actorFileName,
                                                                  const nlohmann::json& actorFile);

    //! .actor の文書へ束の並びを書く。書く物が無ければ鍵ごと消す
    //! 今の並びのうち ReadHitReactionSets が読まない束は、字面のまま同じ位置に残す。読む束の位置へ
    //! sets を並びの順に書き、余った sets は末尾へ足し、足りない位置は消す。今の並びが配列でなければ sets だけを書く
    void WriteHitReactionSets(nlohmann::json& actorFile, const std::vector<HitReactionSet>& sets);

    //! 向き別の振動の 4 つの向きの重み。足すと 1
    struct HitDirectionWeights
    {
        float right = 0.0f;
        float left = 0.0f;
        float up = 0.0f;
        float down = 0.0f;
    };

    //! 右を side、上を up とした向きの角度で、隣り合う 2 つの向きを直線に混ぜた重み
    //! 向きの角度から 90 度離れると 0。side と up が両方 0 なら角度が決まらないので右を 1 にする
    [[nodiscard]] HitDirectionWeights BlendHitDirectionWeights(float side, float up) noexcept;

    //! 束から始めた、持ち主へ渡す項目 1 つの進み
    //! 持ち主は名前で演出を選び、frame と値で描く。frame は始めたフレームが 0
    struct HitOwnerRun
    {
        HitOwnerInfo item{};
        int length = 0;     // 長さ (フレーム数)。frame がこれに届いたフレームの頭で外れる
        int frame = 0;      // 始めてから進めたフレーム数
        Vector3 position{}; // 束を始めた時に渡した位置
    };

    class HitSensor;
    class Model;

    //! 束か項目を始める 1 回の呼び出しに渡す、持ち主の読む物と書く先。呼び出しの中だけ使い、覚えない
    //! 組む所は SceneActor と、束を直に呼ぶ SceneActor の派生。項目を足す時は組む所を全部揃える
    struct HitReactionSelf
    {
        //! .actor の名前・カメラ・シーンを読み、管理クラスへの依頼の主にする。事前条件: nullptr でない
        const SceneActor* actor = nullptr;
        Model* model = nullptr; // 形・揺れ・震えを書く先。持ち主に Model が無ければ nullptr
    };

    class HitReaction;

    //! 束を始める 1 回に、項目の Start へ渡す持ち主と向きと位置。始めるたびに 1 回だけ出し、呼び出しの中だけ使う
    struct HitReactionStart
    {
        HitReaction* reaction = nullptr; // 束を始めた部品
        HitReactionSelf owner{};
        Vector3 position{};    // 渡した位置
        Vector3 toOther{};     // 持ち主のセンサーの中心から相手のセンサーの中心まで
        Vector3 towardOther{}; // toOther の水平の長さ 1 の向き。決まらなければ hasToward が偽
        bool hasToward = false;
        Vector3 cameraRight{1.0f, 0.0f, 0.0f}; // 効果を掛ける前の視点の水平の右
        float side = 0.0f;                     // toOther の cameraRight の成分
        float up = 0.0f;                       // toOther の上の成分
        float bodyLength = 0.0f;               // 持ち主の体の差し渡し (m)
        uint32 seed = 0;                       // 揺れの種
        int length = 0;                        // 今始める項目の長さ (フレーム数)
    };

    //! 当たりの項目が始めた後も毎フレーム進める見た目。項目の Start が HitReaction::AddRun で預け、HitReaction が持つ
    //! 持ち主の SceneActor::Update が、体を動かした後・VisualStep の前に 1 フレームずつ進める。持ち主の止めの間も進む
    //! 持ち主は覚えず、進める・弱める・止める時に受ける
    class HitReactionRun : public NonCopyable
    {
    public:
        virtual ~HitReactionRun() noexcept = default;

        //! 1 フレーム進め、持ち主の Model へ書く。中で HitReaction の AddRun・Stop・StopAll を呼ばない
        //! @return 続ける場合 true。false を返した物は、HitReaction が Stop を呼んでから外す
        [[nodiscard]] virtual bool Advance(const HitReactionSelf& owner) = 0;

        //! 振れ幅に scale を掛ける。次の Advance から効く。事前条件: 0 以上の有限の数
        virtual void Scale(float scale) noexcept = 0;

        //! 持ち主の Model へ書いた見た目を元へ戻す。呼んだ後に HitReaction が外す
        virtual void Stop(const HitReactionSelf& owner) noexcept = 0;

        //! 同じ種類で差し替える物の識別値。HitReaction::AddRun が、同じ識別値の物を前の物と差し替える
        //! 前の物は Stop を呼ばずに外すので、識別値を返す物は前の物と同じ見た目を書き、書けないフレームは前の値のまま描かれる
        //! @return 識別値の番地。nullptr の物は差し替えず、並べて進める
        [[nodiscard]] virtual const void* Kind() const noexcept { return nullptr; }
    };

    //! 当たりの演出を頼む部品。.actor の名前の束を持ち、名前で始める
    //! カメラの効果は CameraManager、白はシーンの ScreenFlashDirector、振動は PadRumbleDirector、
    //! 始めた後も進む見た目は預かった HitReactionRun、効果音は持ち主の Sound、エフェクトは持ち主の EffectObj が出す。
    //! 止めの長さは Start が返し、持ち主の SceneActor が数える。
    //! 持ち主は覚えず、呼ぶたびに受ける。持ち主へ渡す項目の進みはここに持ち、持ち主の SceneActor::Update が進める
    class HitReaction : public Object
    {
    public:
        //! 持ち主の .actor (SceneActor::ActorFileName) から束を読み直す
        //! .actor が無ければ束は空。壊れた束は ReadHitReactionSets が ERROR を出して読まない
        void ReloadSets(const SceneActor* owner);

        //! 名前の束を始める。全部の項目が呼んだフレームに始まり、項目の値は束に書いた値のまま使う
        //! 向きを使う項目は、self の中心から other の中心への向きで決め、位置を使う項目は position を使う
        //! @param[in] name 束の名前
        //! @param[in] position 触れた位置などの世界の点
        //! @param[in] self 持ち主のセンサー。事前条件: nullptr でない
        //! @param[in] other 相手のセンサー。事前条件: nullptr でない
        //! @return 始めた場合は束の止めの長さ (フレーム数)。持ち主が SceneActor::RequestHitStop で止めに頼む。
        //! その名前の束が無い場合は WARN を出して std::nullopt
        [[nodiscard]] std::optional<int> Start(const HitReactionSelf& owner,
                                               std::string_view name,
                                               const Vector3& position,
                                               const HitSensor* self,
                                               const HitSensor* other);

        //! 持ち主の .actor の束の並び
        [[nodiscard]] const std::vector<HitReactionSet>& Sets() const noexcept { return m_sets; }

        //! 持ち主の .actor の名前の束。無ければ nullptr
        [[nodiscard]] const HitReactionSet* FindSet(std::string_view name) const noexcept;

        //! 始めて長さの終わっていない、持ち主へ渡す項目。始めた順
        [[nodiscard]] const std::vector<HitOwnerRun>& OwnerRuns() const noexcept { return m_ownerRuns; }

        //! index 以降に足された、持ち主へ渡す項目の写し。
        //! 写しで返すので、回している間に次の当たりの演出を始めて並びが伸びても、指す先が消えない
        [[nodiscard]] std::vector<HitOwnerRun> OwnerRunsSince(std::size_t index) const;

        //! 持ち主へ渡す項目を 1 フレーム進め、長さの終わった物を外す
        //! 呼ぶのは SceneActor::Update の頭だけ。持ち主の止めの間も進める
        void AdvanceOwnerRuns() noexcept;

        //! 始めた後も進む見た目を預かる。nullptr は何もしない
        //! 同じ識別値 (HitReactionRun::Kind、nullptr 以外) の物が預けてあれば、その物の Stop を呼ばずに外してから足す。
        //! 同じ種類の見た目は、新しい物が次の Advance で書き直す
        void AddRun(std::unique_ptr<HitReactionRun> run);

        //! 預かった見た目を預けた順に 1 フレーム進め、終わった物を外す
        //! 呼ぶのは SceneActor::Update だけで、体を動かした後・VisualStep の前。持ち主の止めの間も進める
        void AdvanceRuns(const HitReactionSelf& owner);

        // 下の関数の owner は管理クラスとカメラを引くアクターで、依頼の主にもなる。事前条件: nullptr でない

        //! カメラのトラウマを足す。揺れの途中なら前のトラウマに足す
        //! @param[in] desc 足す量と揺れの形
        //! @return 足した場合 true。量が壊れている・カメラの管理役が無い場合 false
        bool AddTrauma(const SceneActor* owner, const CameraTraumaDesc& desc);

        //! @brief この部品が頼んで走っている演出の強さに scale を掛ける。始め直さず、残りをその強さで続ける
        //! @details 掛けるのは白の濃さ・振動の速さ・預かった見た目 (HitReactionRun::Scale)。
        //! カメラの効果は変えない。持ち主へ渡す項目は持ち主が自分で弱める
        //! @param[in] owner 頼んだ持ち主
        //! @param[in] scale 掛ける倍率。事前条件: 0 以上の有限の数
        void ScaleRunning(const HitReactionSelf& owner, float scale);

        //! この部品が頼んだ演出を止める。白を消し、振動を 0 にし、持ち主が頼んだカメラの効果のうちトラウマのほかを外し、
        //! 持ち主へ渡す項目を外し、預かった見た目を止めて外す
        //! トラウマは残す。次の当たりが止めてから始め直しても、続けて当てたトラウマは足される。
        //! ほかの物が頼んだカメラの効果は残す
        void Stop(const HitReactionSelf& owner);

        //! 預かった見た目だけを止めて外す。画面の演出・振動・カメラの効果・持ち主へ渡す項目は残す
        void StopRuns(const HitReactionSelf& owner) noexcept;

        //! Stop に加えて、この部品が頼んだカメラの効果をトラウマも含めて全部外す。途中で外れても効果を残さない
        void StopAll(const HitReactionSelf& owner);

        // 状態は保存しない。型検索で引けるよう型名だけ登録する
        NS_REFLECT_NONE(HitReaction, Object)

    private:
        // 持ち主へ渡す項目は、始める時に m_ownerRuns へ足す
        friend struct HitOwnerInfo;

        std::vector<HitReactionSet> m_sets;   // 持ち主の .actor の束
        std::vector<HitOwnerRun> m_ownerRuns; // 進めている持ち主へ渡す項目
        uint32 m_startCount = 0;              // 束か項目を始めた回数。揺れの種に混ぜる
        std::vector<std::unique_ptr<HitReactionRun>> m_runs; // 預かった、始めた後も進む見た目。預けた順
    };
} // namespace NS

//! 束の項目の種類の型を登録する。その型の .cpp で 1 度だけ書く。型の typeName がファイルの種類の名前になる
#define NS_HIT_REACTION_ITEM(Type)                                                                                     \
    namespace                                                                                                          \
    {                                                                                                                  \
        const bool k_hitReactionItemRegistered_##Type = [] {                                                           \
            ::NS::RegisterHitReactionItem<Type>();                                                                     \
            return true;                                                                                               \
        }();                                                                                                           \
    }
