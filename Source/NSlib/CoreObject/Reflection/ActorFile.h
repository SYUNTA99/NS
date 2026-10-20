#pragma once

// 値はコードの既定値、.actor の値、配置物の印の付いた欄の順に重ねる。
// 部品の構成を決めるのは .actor まで。配置物の保存は .actor の値との差分だけを持つ。

#include "NSlib/CoreObject/ObjectJson.h"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace NS
{
    class SceneActor;
    class Object;
    struct ClassObjInfo;
    struct FieldDesc;

    //! .actor の文書の中の class。無いか文字列でなければ空
    [[nodiscard]] std::string_view ActorFileClass(const nlohmann::json& actorFile) noexcept;
    //! .actor の文書へ class を書く
    void SetActorFileClass(nlohmann::json& actorFile, std::string_view className);

    //! .actor の文書の中の名前の束の並び hitReactions。無ければ nullptr。並びの形は確かめない
    [[nodiscard]] const nlohmann::json* ActorFileHitReactions(const nlohmann::json& actorFile) noexcept;
    //! .actor の文書へ名前の束の並び hitReactions を書く。空の並びなら鍵ごと消す
    void SetActorFileHitReactions(nlohmann::json& actorFile, nlohmann::json list);

    //! .actor の文書の中の動作の並び actions。無ければ nullptr。並びの形は確かめない
    [[nodiscard]] const nlohmann::json* ActorFileActions(const nlohmann::json& actorFile) noexcept;
    //! .actor の文書へ動作の並び actions を書く。空の並びなら鍵ごと消す
    void SetActorFileActions(nlohmann::json& actorFile, nlohmann::json list);

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
        //! @details 開けないファイル・読めない JSON・知らない頭の鍵・class が無いか登録されていない物は ERROR を出して飛ばす
        void Reload();

        //! 持っている .actor の名前。名前の順
        [[nodiscard]] std::vector<std::string> Names();

        //! name の .actor の文書。頭の鍵を全部持つ。無ければ nullptr
        [[nodiscard]] const nlohmann::json* Find(std::string_view name);

        //! name の .actor の中の class。.actor が無ければ空
        //! クラス名を比べる所はここで引き、.actor の名前とクラス名を同じと見なさない
        [[nodiscard]] std::string ClassNameOf(std::string_view name);

        //! @brief name の .actor の文書を差し替える。ファイルへは書かない。参照の欄は落とす
        //! @details Reload が読まない文書 (知らない頭の鍵・class が無いか登録されていない) は ERROR を出して入れず、false を返す
        bool Set(std::string_view name, nlohmann::json actorFile);

        //! name の .actor の文書を消す。ファイルは消さない
        void Erase(std::string_view name);

        //! name の .actor の文書を消し、Directory() のファイルも消す。ファイルを消せなければ false
        [[nodiscard]] bool Delete(std::string_view name);

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

    //! @brief 配置物の JSON から、配置物が値を持てない欄を落とした写し
    //! @details 持てるのは根の部品の欄・参照の欄・上書きの印の付いた欄。落とした欄ごとに WARN を出す。
    //! actor のクラスに無い部品と欄は残し、値を当てる所に知らせを任せる
    [[nodiscard]] nlohmann::json KeepPlacementFields(const SceneActor& actor, const nlohmann::json& object);

    //! 個体の差分を全欄へ展開する。部品構成は Baseline に揃え、個体だけの部品は除く
    //! 配置物が値を持てない欄は KeepPlacementFields で落とし、.actor の値のままにする
    [[nodiscard]] nlohmann::json ExpandObjectJson(const nlohmann::json& object);

    //! @brief 全欄を持つ配置物の JSON から、配置物の指す .actor の値と同じ欄を除く
    //! @details 欄が全部既定と同じ部品も、空の件として残す。位置・回転・拡縮の部品 Transform は個体の物なので除かない。
    //! 配置物が値を持てない欄は、値が違っても除く
    [[nodiscard]] nlohmann::json DiffObjectJson(const nlohmann::json& object);

    //! 欄の値を持つ所。.actor だけが持つか、配置物が持つか
    enum class FieldDestination
    {
        ActorFile, // 同じ .actor を指すアクターの全部が同じ値を使う
        Placement, // 配置物ごとに値を持つ
    };

    //! @brief actor の部品 part の欄 field の値を持つ所
    //! @details 根の部品の欄・参照の欄・上書きの印の付いた欄は配置物、ほかは .actor。
    //! 配置物の読み書き・エディタの書く先・行の地の色は、どれもこの答えで分ける
    [[nodiscard]] FieldDestination FieldDestinationOf(const SceneActor& actor,
                                                      const Object& part,
                                                      const FieldDesc& field) noexcept;

    //! actor の行 row の部品に対応する、actor の .actor の Baseline の部品。どちらかが作られていないか型が違えば nullptr
    //! row は actor のクラスの表 (NS_CLASSOBJ) の行。.actor を変えると指す先が作り直されるので、そのフレームの間だけ使う
    [[nodiscard]] const Object* FindBaselineClassObj(const SceneActor& actor, const ClassObjInfo& row);
} // namespace NS
