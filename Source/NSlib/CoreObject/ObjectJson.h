#pragma once

#include "NSlib/CoreObject/Reflection/ActorRef.h"

#pragma warning(push, 0)
#include "ThirdParty/nlohmann/json.hpp"
#pragma warning(pop)

#include "NSlib/Core/Types.h"
#include <string>
#include <string_view>
#include <unordered_map>

namespace NS
{
    //! @brief 配置物の "classObjs" を返す
    //! @return 無いか object でなければ空の object
    [[nodiscard]] const nlohmann::json& ObjectJsonClassObjs(const nlohmann::json& object) noexcept;
    //! @brief 配置物の "classObjs" を返す。無いか object でなければ空の object を作って入れる
    [[nodiscard]] nlohmann::json& ObjectJsonClassObjs(nlohmann::json& object);
    //! @brief 部品名 partName の欄の object を返す
    //! @return 無いか object でなければ nullptr
    [[nodiscard]] const nlohmann::json* ClassObjFields(const nlohmann::json& object,
                                                       std::string_view partName) noexcept;
    //! @brief 部品名 partName の欄の object を返す。無い部品は作らない
    //! @return 無いか object でなければ nullptr
    [[nodiscard]] nlohmann::json* ClassObjFields(nlohmann::json& object, std::string_view partName) noexcept;
    //! 配置物の保存・凍結・編集履歴で共用する JSON。参照はメモリ上では id、ファイルでは名前
    [[nodiscard]] nlohmann::json MakeObjectJson();

    //! 配置物の永続 id。未採番と壊れた形は 0
    [[nodiscard]] uint32 ObjectJsonId(const nlohmann::json& object) noexcept;
    //! 配置物の永続 id を書く
    void SetObjectJsonId(nlohmann::json& object, uint32 id);

    //! 生成する SceneActor のクラス名。空は素の SceneActor
    [[nodiscard]] std::string_view ObjectJsonClass(const nlohmann::json& object) noexcept;
    //! クラス名を書く。空は素の SceneActor の印なので消す
    void SetObjectJsonClass(nlohmann::json& object, std::string_view className);

    //! 配置物の名前。空は未設定で、読込の一意化が名前を振る
    [[nodiscard]] std::string_view ObjectJsonName(const nlohmann::json& object) noexcept;
    //! 名前を書く。空は未設定の印なので消す
    void SetObjectJsonName(nlohmann::json& object, std::string_view name);

    //! 配置物自身の active 値。欄が無ければ有効
    [[nodiscard]] bool ObjectJsonActive(const nlohmann::json& object) noexcept;
    //! active 値を書く。有効は既定なので消す
    void SetObjectJsonActive(nlohmann::json& object, bool active);

    //! 配置物の頭の欄。保存の形は欄ごとの読み書きの関数と同じ
    struct ObjectHeader
    {
        uint32 id = 0;
        std::string className;
        std::string name;
        bool active = true;
    };

    //! 頭の欄をまとめて読む。無い欄と壊れた欄は各欄の読む関数と同じ既定になる
    [[nodiscard]] ObjectHeader ReadObjectHeader(const nlohmann::json& object);
    //! 頭の欄をまとめて書く。既定の欄は各欄の書く関数と同じく消す
    void WriteObjectHeader(nlohmann::json& object, const ObjectHeader& header);

    // 参照の欄の値は鍵 ref を 1 つ持つ object で書く。素の数値だと読む時に Int と区別できない。
    // 値はメモリ上では id、ファイル上では名前。この形を知るのは ObjectJson.cpp だけにする

    //! 参照の欄の値の形の場合 true、それ以外の場合は false
    [[nodiscard]] bool IsRefValue(const nlohmann::json& value) noexcept;
    //! id を指す参照の欄の値を作る
    [[nodiscard]] nlohmann::json MakeRefValue(ActorRef ref);
    //! @brief 参照の欄の値から id を読む
    //! @return id が負でない整数で書かれている場合 true。名前で書かれた値と壊れた形は false で、out を変えない
    [[nodiscard]] bool ReadRefValue(const nlohmann::json& value, ActorRef& out) noexcept;
    //! 参照の欄の値の中の、id か名前の入る所。参照の形でなければ nullptr
    [[nodiscard]] nlohmann::json* FindRefTarget(nlohmann::json& value) noexcept;

    //! fn は参照の欄ごとに、id か名前の入る所を受け取る
    //! 参照の欄を辿る処理はここだけに置き、欄の形を知る場所を 1 つにする
    template <class Fn> void ForEachRefValue(nlohmann::json& object, Fn&& fn)
    {
        nlohmann::json& classObjs = ObjectJsonClassObjs(object);
        for (nlohmann::json& entry : classObjs)
        {
            if (!entry.is_object())
            {
                continue;
            }
            for (nlohmann::json& value : entry)
            {
                nlohmann::json* target = FindRefTarget(value);
                if (target != nullptr)
                {
                    fn(*target);
                }
            }
        }
    }

    //! @brief object の参照の欄のうち、idMap に載った id を指す物を載った先の id へ付け替える
    //! @details 複製で使う。コピーした範囲の中を指す参照だけをコピー側へ向け、範囲の外を指す参照は元のまま残す
    void RemapObjectRefs(nlohmann::json& object, const std::unordered_map<uint32, uint32>& idMap);
} // namespace NS
