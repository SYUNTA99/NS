#pragma once

// 値はコードの既定値、.actor の値、シーンで上書きできる欄の順に重ねる。
// 部品の構成を決めるのは .actor まで。シーンのファイルのアクターの保存は .actor の値との差分だけを持つ。

#include "NSlib/CoreObject/ActorJson.h"
#include "NSlib/Core/Logger.h"

#include <cstddef>
#include <format>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace NS
{
    class SceneActor;
    class Object;
    struct ClassObjInfo;
    struct FieldDesc;

    /// .actor の文書の中の class。無いか文字列でなければ空
    [[nodiscard]] std::string_view ActorFileClass(const nlohmann::json& actorFile) noexcept;
    /// .actor の文書へ class を書く
    void SetActorFileClass(nlohmann::json& actorFile, std::string_view className);

    /// 派生の .actor の文書の中の、元にする .actor の名前 base。元の .actor の文書か、文字列でなければ空
    [[nodiscard]] std::string_view ActorFileBase(const nlohmann::json& actorFile) noexcept;
    /// .actor の文書へ、元にする .actor の名前 base を書く
    void SetActorFileBase(nlohmann::json& actorFile, std::string_view baseName);

    /// .actor の文書の中の名前の束の並び hitReactions。無ければ nullptr。並びの形は確かめない
    [[nodiscard]] const nlohmann::json* ActorFileHitReactions(const nlohmann::json& actorFile) noexcept;
    /// .actor の文書へ名前の束の並び hitReactions を書く。空の並びなら鍵ごと消す
    void SetActorFileHitReactions(nlohmann::json& actorFile, nlohmann::json list);

    /// .actor の文書の中の動作の並び actions。無ければ nullptr。並びの形は確かめない
    [[nodiscard]] const nlohmann::json* ActorFileActions(const nlohmann::json& actorFile) noexcept;
    /// .actor の文書へ動作の並び actions を書く。空の並びなら鍵ごと消す
    void SetActorFileActions(nlohmann::json& actorFile, nlohmann::json list);

    /// .actor の名前付き項目の並びの要素 1 つを読んだ結果。読まない要素は entry が空で、reason に理由を持つ
    template <typename T>
    struct ListedActorFileItem
    {
        std::optional<T> entry;
        std::string reason;
    };

    /// 名前付き項目の並びを要素ごとに読む。読むか読まないかの決まりは、読む時と書き戻す時でこの 1 か所を通す
    template <typename T>
    [[nodiscard]] std::vector<ListedActorFileItem<T>>
    ListNamedActorFileItems(const nlohmann::json& list,
                            std::optional<T> (*parse)(const nlohmann::json&, std::string&),
                            std::string_view itemLabel)
    {
        std::vector<ListedActorFileItem<T>> listed(list.size());
        std::set<std::string, std::less<>> names;
        for (std::size_t i = 0; i < list.size(); ++i)
        {
            std::optional<T> entry = parse(list[i], listed[i].reason);
            if (!entry.has_value())
            {
                continue;
            }
            // 同じ名前を 2 つ持つと、始める物が並びの順で黙って決まる
            if (!names.insert(entry->name).second)
            {
                listed[i].reason = std::format("名前 {} が前の{}と重なる", entry->name, itemLabel);
                continue;
            }
            listed[i].entry = std::move(entry);
        }
        return listed;
    }

    /// .actor の文書の名前付き項目の並びを読む。壊れた要素や重複名は ERROR に出して飛ばす
    template <typename T>
    [[nodiscard]] std::vector<T>
    ReadNamedActorFileItems(std::string_view actorFileName,
                            const nlohmann::json* list,
                            std::optional<T> (*parse)(const nlohmann::json&, std::string&),
                            std::string_view keyName,
                            std::string_view itemLabel)
    {
        std::vector<T> items;
        if (list == nullptr)
        {
            return items;
        }
        if (!list->is_array())
        {
            NS_LOG_ERROR(
                Scene, ".actor {} の{}の並び {} が配列でないので、{}を読まない", actorFileName, itemLabel, keyName, itemLabel);
            return items;
        }
        std::vector<ListedActorFileItem<T>> listed = ListNamedActorFileItems(*list, parse, itemLabel);
        for (std::size_t i = 0; i < listed.size(); ++i)
        {
            if (!listed[i].entry.has_value())
            {
                NS_LOG_ERROR(
                    Scene, ".actor {} の {} 番目の{}を読まない。{}", actorFileName, i, itemLabel, listed[i].reason);
                continue;
            }
            items.push_back(std::move(*listed[i].entry));
        }
        return items;
    }

    /// .actor の名前付き項目の並びを書き戻す。読まなかった要素は字面のまま同じ位置に残す
    template <typename T>
    [[nodiscard]] nlohmann::json
    BuildNamedActorFileItemsJson(const nlohmann::json* current,
                                 const std::vector<T>& items,
                                 std::optional<T> (*parse)(const nlohmann::json&, std::string&),
                                 nlohmann::json (*toJson)(const T&),
                                 std::string_view itemLabel)
    {
        nlohmann::json list = nlohmann::json::array();
        std::size_t next = 0;
        if (current != nullptr && current->is_array())
        {
            const std::vector<ListedActorFileItem<T>> listed = ListNamedActorFileItems(*current, parse, itemLabel);
            for (std::size_t i = 0; i < listed.size(); ++i)
            {
                if (!listed[i].entry.has_value())
                {
                    list.push_back((*current)[i]);
                    continue;
                }
                if (next < items.size())
                {
                    list.push_back(toJson(items[next]));
                    ++next;
                }
            }
        }
        for (; next < items.size(); ++next)
        {
            list.push_back(toJson(items[next]));
        }
        return list;
    }

    /// path の .actor を読み、参照の欄を落とした文書を返す
    /// 開けない・大きすぎる・JSON でない・知らない頭の鍵・class が無いか登録されていない物は ERROR で std::nullopt
    [[nodiscard]] std::optional<nlohmann::json> ReadActorFileAt(const std::string& path);
    /// path へ .actor の文書を辞書順の鍵で書く。書けなければ ERROR で false で、前のファイルは残る
    [[nodiscard]] bool WriteActorFileAt(const std::string& path, const nlohmann::json& actorFile);

    /// 名前ごとの既定値を Directory()/<名前>.actor から読む。作るクラスはファイルの中の class
    /// ActorRef はシーン内の参照なので既定値には含めない
    class ActorFileLibrary
    {
    public:
        [[nodiscard]] static ActorFileLibrary& Get();

        ~ActorFileLibrary();

        [[nodiscard]] const std::string& Directory();

        /// ディレクトリを差し替えて読み直す。試しは一時ディレクトリへ向ける
        void SetDirectory(std::string directory);

        /// Directory() の *.actor を全て読み直し、元 → 派生の順に全部の値を作る
        /// 開けないファイル・読めない JSON・知らない頭の鍵・class が無いか登録されていない物は ERROR を出して飛ばす。
        /// 全部の値に登録した項目の行が欠けた物と、元に重ねられない派生も ERROR を出して飛ばす
        void Reload();

        /// 持っている .actor の名前。名前の順
        [[nodiscard]] std::vector<std::string> Names();

        /// name の .actor の文書。頭の鍵を全部持つ。派生なら書いた項目だけ。無ければ nullptr
        [[nodiscard]] const nlohmann::json* Find(std::string_view name);

        /// name の全部の値。元ならその文書、派生なら元の全部の値に派生の項目を重ねた物
        /// 登録した項目の行を全部持つ。無ければ nullptr。Set・Erase・Reload の後は前に返した物を使わない
        [[nodiscard]] const nlohmann::json* Values(std::string_view name);

        /// name の .actor の中の class。.actor が無ければ空
        /// クラス名を比べる所はここで引き、.actor の名前とクラス名を同じと見なさない
        [[nodiscard]] std::string ClassNameOf(std::string_view name);

        /// name の .actor の文書を差し替え、全部の値を作り直す。ファイルへは書かない。参照の欄は落とす
        /// Reload が読まない文書は ERROR を出して入れず、false を返す。元を差し替えると、その派生の全部の値も変わる
        bool Set(std::string_view name, nlohmann::json actorFile);

        /// name の .actor の文書を消し、全部の値を作り直す。ファイルは消さない
        void Erase(std::string_view name);

        /// name の .actor の文書を消し、Directory() のファイルも消す。ファイルを消せなければ false
        [[nodiscard]] bool Delete(std::string_view name);

        /// name の .actor の文書を丸ごと Directory() へ書く。無いか書けなければ false
        [[nodiscard]] bool Save(std::string_view name);


    private:
        ActorFileLibrary();
        // まだ読んでいなければ Directory() を読む
        void EnsureLoaded();
        // m_actorFiles の全部から、元 → 派生の順に m_mergedValues を作り直す。作れない名前は入れない
        void RebuildValues();
        // name の文書から全部の値を作る。派生は m_mergedValues の元に重ねる。作れなければ ERROR を出して std::nullopt
        [[nodiscard]] std::optional<nlohmann::json> BuildValues(std::string_view name, const nlohmann::json& actorFile);

        std::string m_directory;                                                     // 空なら既定のディレクトリ
        std::map<std::string, nlohmann::json, std::less<>> m_actorFiles;             // 名前から .actor の文書
        std::map<std::string, nlohmann::json, std::less<>> m_mergedValues;           // 名前から全部の値
        bool m_loaded = false;
    };

    /// actor の ActorFileName の .actor の全部の値を写す。シーンで上書きした項目は写さず、部品は足さない
    void ApplyActorFile(SceneActor* actor);

    /// シーンに置いたアクターの JSON に書いた項目で、actor のシーンの上書きの一覧を作り直す
    /// 参照の欄は一覧に入れない。項目が書いてよいかは見ないので、先に MergeLayer で確かめる
    void ReadSceneOverrides(SceneActor* actor, const nlohmann::json& actorJson);

    /// actor の行 row の部品の欄 fieldName をシーンの上書きの一覧から外し、.actor の全部の値の同じ項目を写す
    /// 全部の値か、その項目が無ければ ERROR を出し、一覧も値も変えずに false
    [[nodiscard]] bool RevertSceneOverride(SceneActor* actor, const ClassObjInfo& row, std::string_view fieldName);

    /// 欄の値を書く所
    enum class FieldDestination
    {
        ActorFile,        // 元の .actor。派生も含めて、元を指すアクターの全部が同じ値を使う
        DerivedActorFile, // 派生の .actor。その派生を指すアクターの全部が同じ値を使う
        Scene,            // シーンに置いたアクターごとに値を持つ
    };

    /// actor の欄 field の値を書く所
    /// 参照の欄とグローバルの項目 (姿勢の位置・回転・スケールを含む) はシーン。
    /// プロテクトの項目は、actor が派生の .actor を指していれば派生、ほかは元の .actor。
    /// シーンの読み書き・エディタの書く先・行の地の色は、どれもこの答えで分ける
    [[nodiscard]] FieldDestination FieldDestinationOf(const SceneActor* actor, const FieldDesc& field) noexcept;

    /// actor の欄を destination へ書く時の .actor の名前
    /// 元の .actor は、actor が派生を指していればその元の名前、ほかは actor が指す名前。
    /// 派生の .actor は actor が指す名前。シーンは空
    [[nodiscard]] std::string ActorFileToWrite(const SceneActor* actor, FieldDestination destination);

    /// layer の上書きに、欄 field を書いてよいか
    /// 元の .actor は参照の欄の他の全部、派生の .actor はプロテクトとグローバルの項目、
    /// シーンはグローバルの項目と参照の欄を書ける
    [[nodiscard]] bool CanWriteField(FieldDestination layer, const FieldDesc& field) noexcept;

    /// 全部の値 base に、上書き written の "classObjs" に書いた項目を重ねる
    /// 元の .actor・派生の .actor・シーンの上書きは、どれもこの関数で重ねる。written の頭の鍵は見ない。
    /// layer に書いてよくない項目か、actor に無い部品・欄が 1 つでもあれば ERROR を出して std::nullopt
    [[nodiscard]] std::optional<nlohmann::json> MergeLayer(const nlohmann::json& base,
                                                           const nlohmann::json& written,
                                                           FieldDestination layer,
                                                           const SceneActor* actor);
} // namespace NS
