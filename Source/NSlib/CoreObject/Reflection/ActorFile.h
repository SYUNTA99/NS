#pragma once

// 値はコードの既定値、種類の既定値、個体の上書きの順に重ねる。
// 部品の追加は種類の既定値まで。保存は種類の既定値との差分だけを持つ。

#include "NSlib/CoreObject/ObjectJson.h"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>

namespace NS
{
    class SceneActor;
    class Object;
    struct ClassObjInfo;

    //! .actor の文書の中の class。無いか文字列でなければ空
    [[nodiscard]] std::string_view ActorFileClass(const nlohmann::json& actorFile) noexcept;
    //! .actor の文書へ class を書く
    void SetActorFileClass(nlohmann::json& actorFile, std::string_view className);

    //! 名前ごとの既定値を Directory()/<名前>.actor から読む。作るクラスはファイルの中の class
    //! ActorRef はシーン内の参照なので既定値には含めない
    class ActorFileLibrary
    {
    public:
        [[nodiscard]] static ActorFileLibrary& Get();

        ~ActorFileLibrary();

        [[nodiscard]] const std::string& Directory();

        //! ディレクトリを差し替えて読み直す。試しは一時ディレクトリへ向ける
        void SetDirectory(std::string directory);

        //! @brief Directory() の *.actor を全て読み直す
        //! @details 読めない JSON・知らない頭の鍵・class が無いか登録されていない物は ERROR を出して飛ばす
        void Reload();

        //! name の .actor の文書。頭の鍵を全部持つ。無ければ nullptr
        [[nodiscard]] const nlohmann::json* Find(std::string_view name);

        //! name の .actor の中の class。.actor が無ければ空
        //! クラス名を比べる所はここで引き、.actor の名前とクラス名を同じと見なさない
        [[nodiscard]] std::string ClassNameOf(std::string_view name);

        //! name の .actor の文書を差し替える。ファイルへは書かない。参照の欄は落とす
        void Set(std::string_view name, nlohmann::json actorFile);

        //! name の .actor の文書を消す。ファイルは消さない
        void Erase(std::string_view name);

        //! name の .actor の文書を丸ごと Directory() へ書く。無いか書けなければ false
        [[nodiscard]] bool Save(std::string_view name);

        //! @brief name の .actor を敷いた、配置の上書きの無い比較用 SceneActor。シーンへ入らない
        //! @details クラスは .actor の中の class。
        //! .actor の無い name は型から作ったアクターの物なので、クラス名として作る。
        //! .actor を変えると作り直すので、返した参照はそのフレームの間だけ使う
        [[nodiscard]] const SceneActor& Baseline(std::string_view name);

        //! @brief 既定の 1 体を全部捨てる。次の Baseline で作り直す
        //! @details 既定の 1 体は作った時の AssetManager の素材を指すので、その表を空にする時と差し替える時に呼ぶ
        void ClearBaselines() noexcept;

    private:
        ActorFileLibrary();
        // まだ読んでいなければ Directory() を読む
        void EnsureLoaded();

        std::string m_directory;                                                     // 空なら既定のディレクトリ
        std::map<std::string, nlohmann::json, std::less<>> m_actorFiles;             // 名前から .actor の文書
        std::map<std::string, std::unique_ptr<SceneActor>, std::less<>> m_baselines; // .actor の名前から既定の 1 体
        bool m_loaded = false;
    };

    //! actor の ActorFileName の .actor の欄の値を写す。部品は足さない
    void ApplyActorFile(SceneActor& actor);

    //! 個体の差分を全欄へ展開する。部品構成は Baseline に揃え、個体だけの部品は除く
    [[nodiscard]] nlohmann::json ExpandObjectJson(const nlohmann::json& object);

    //! @brief 全欄を持つ配置物の JSON から、種類の既定値と同じ欄を除く
    //! @details 欄が全部既定と同じ部品も、空の件として残す。位置・回転・拡縮の部品 Transform は個体の物なので除かない
    [[nodiscard]] nlohmann::json DiffObjectJson(const nlohmann::json& object);

    // 下の 4 つの row は actor のクラスの表 (NS_CLASSOBJ) の行。ForEachReflectedClassObj で回した行を渡す

    //! actor の行 row の部品に対応する、actor のクラスの Baseline の部品。どちらかが作られていないか型が違えば nullptr
    //! 種類の既定値を変えると指す先が作り直されるので、そのフレームの間だけ使う
    [[nodiscard]] const Object* FindBaselineClassObj(const SceneActor& actor, const ClassObjInfo& row);

    //! actor の行 row の部品の欄 fieldName が、actor のクラスの種類の既定値と違うか
    [[nodiscard]] bool IsFieldOverridden(const SceneActor& actor, const ClassObjInfo& row, std::string_view fieldName);

    //! 種類の既定値へ上げてよい欄か。参照の欄と位置・回転・拡縮は個体の物なので上げられない
    //! 行の部品が作られていなければ上げられない
    [[nodiscard]] bool IsActorFileField(const SceneActor& actor, const ClassObjInfo& row, std::string_view fieldName);

    //! @brief actor の行 row の部品の欄 fieldName の今の値を、actor のクラスの種類の既定値へ書く
    //! @details ファイルへは書かない。上げられない欄は何もせず false
    [[nodiscard]] bool WriteFieldToActorFile(const SceneActor& actor,
                                             const ClassObjInfo& row,
                                             std::string_view fieldName);
} // namespace NS
