#pragma once

#include "NSlib/Object/Reflection/ActorRef.h"

#pragma warning(push, 0)
#include "ThirdParty/nlohmann/json.hpp"
#pragma warning(pop)

#include <cstdint>
#include <string_view>
#include <unordered_map>

namespace NS::Obj
{
    //! @brief 配置物の "parts" を返す
    //! @return 無いか object でなければ空の object
    [[nodiscard]] const nlohmann::json& ObjectJsonParts(const nlohmann::json& object) noexcept;
    //! @brief 配置物の "parts" を返す。無いか object でなければ空の object を作って入れる
    [[nodiscard]] nlohmann::json& ObjectJsonParts(nlohmann::json& object);
    //! @brief 部品名 partName の欄の object を返す
    //! @return 無いか object でなければ nullptr
    [[nodiscard]] const nlohmann::json* PartFields(const nlohmann::json& object, std::string_view partName) noexcept;
    //! @brief 部品名 partName の欄の object を返す。無い部品は作らない
    //! @return 無いか object でなければ nullptr
    [[nodiscard]] nlohmann::json* PartFields(nlohmann::json& object, std::string_view partName) noexcept;
    //! @brief 配置物 1 体の JSON を作る
    //! @details 配置物が自分を書き出した保存形式で、実体でない姿はどれもこの形で持つ
    //! ファイルの 1 配置物・undo の控え・プレイ開始時の凍結・パレットのひな形が同じ形を使う
    //! 参照の欄はメモリ上では id、ファイル上だけ名前で書く
    [[nodiscard]] nlohmann::json MakeObjectJson(nlohmann::json components = nlohmann::json::object());

    //! 配置物の永続 id。未採番と壊れた形は 0
    [[nodiscard]] std::uint32_t ObjectJsonId(const nlohmann::json& object) noexcept;
    //! 配置物の永続 id を書く
    void SetObjectJsonId(nlohmann::json& object, std::uint32_t id);

    //! 生成する Actor のクラス名。空は素の Actor
    [[nodiscard]] std::string_view ObjectJsonClass(const nlohmann::json& object) noexcept;
    //! クラス名を書く。空は素の Actor の印なので消す
    void SetObjectJsonClass(nlohmann::json& object, std::string_view className);

    //! 配置物の名前。空は未設定で、読込の一意化が名前を振る
    [[nodiscard]] std::string_view ObjectJsonName(const nlohmann::json& object) noexcept;
    //! 名前を書く。空は未設定の印なので消す
    void SetObjectJsonName(nlohmann::json& object, std::string_view name);

    //! 親の永続 id。0 は root。transform は親空間の local として解釈される
    [[nodiscard]] std::uint32_t ObjectJsonParent(const nlohmann::json& object) noexcept;
    //! 親の永続 id を書く。0 は root の印なので消す
    void SetObjectJsonParent(nlohmann::json& object, std::uint32_t parentId);

    //! 配置物自身の active 値。欄が無ければ有効
    [[nodiscard]] bool ObjectJsonActive(const nlohmann::json& object) noexcept;
    //! active 値を書く。有効は既定なので消す
    void SetObjectJsonActive(nlohmann::json& object, bool active);

    //! fn はその object を受け取る。値はメモリ上では id、ファイル上では名前
    //! 参照の欄を辿る処理はここだけに置き、欄の形を知る場所を 1 つにする
    template <class Fn> void ForEachRefValue(nlohmann::json& object, Fn&& fn)
    {
        nlohmann::json& components = ObjectJsonParts(object);
        for (nlohmann::json& entry : components)
        {
            if (!entry.is_object())
            {
                continue;
            }
            for (nlohmann::json& value : entry)
            {
                if (value.is_object() && value.contains("ref"))
                {
                    fn(value);
                }
            }
        }
    }

    //! @brief object の参照の欄のうち、idMap に載った id を指す物を載った先の id へ付け替える
    //! @details 複製で使う。コピーした範囲の中を指す参照だけをコピー側へ向け、範囲の外を指す参照は元のまま残す
    void RemapObjectRefs(nlohmann::json& object, const std::unordered_map<std::uint32_t, std::uint32_t>& idMap);
} // namespace NS::Obj
