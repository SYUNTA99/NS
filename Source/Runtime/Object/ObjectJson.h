#pragma once

#include "Runtime/Object/Reflection/ObjectRef.h"

#pragma warning(push, 0)
#include "ThirdParty/nlohmann/json.hpp"
#pragma warning(pop)

#include <cstdint>
#include <string_view>
#include <unordered_map>

namespace NS::Obj
{
    //! @brief 配置物 1 体の JSON を作る
    //! @details 配置物が自分を書き出した保存形式で、実体でない姿はどれもこの形で持つ
    //! ファイルの 1 配置物・undo の控え・プレイ開始時の凍結・パレットのひな形が同じ形を使う
    //! {"id": 永続 id, "class": クラス名, "name": 名前, "parent": 親の id, "active": 有効か, "components": [...]}
    //! class / name / parent / active は既定値なら書かない。components は {"type", "id", "name", "fields"} の配列
    //! 参照の欄はメモリ上では id、ファイル上だけ名前で書く
    [[nodiscard]] nlohmann::json MakeObjectJson(nlohmann::json components = nlohmann::json::array());

    //! 配置物の永続 id。未採番と壊れた形は 0
    [[nodiscard]] std::uint32_t ObjectJsonId(const nlohmann::json& object) noexcept;
    //! 配置物の永続 id を書く
    void SetObjectJsonId(nlohmann::json& object, std::uint32_t id);

    //! 生成する GameObject のクラス名。空は素の GameObject
    [[nodiscard]] std::string_view ObjectJsonClass(const nlohmann::json& object) noexcept;
    //! クラス名を書く。空は素の GameObject の印なので消す
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

    //! components 配列。無いか壊れていれば空の配列
    [[nodiscard]] const nlohmann::json& ObjectJsonComponents(const nlohmann::json& object) noexcept;
    //! components 配列を返し、無ければ作る
    [[nodiscard]] nlohmann::json& ObjectJsonComponents(nlohmann::json& object);

    //! @brief 配置物の components の fields にある参照の欄を 1 つずつ fn へ渡す
    //! @details ObjectRef は {"ref": 持ち主}、ComponentRef は {"ref": 持ち主, "component": Component} の object で、
    //! fn はその object を受け取る。値はメモリ上では id、ファイル上では名前
    //! 参照の欄を辿る処理はここだけに置き、欄の形を知る場所を 1 つにする
    template <class Fn> void ForEachRefValue(nlohmann::json& object, Fn&& fn)
    {
        nlohmann::json& components = ObjectJsonComponents(object);
        for (nlohmann::json& entry : components)
        {
            if (!entry.is_object())
            {
                continue;
            }
            const nlohmann::json::iterator fieldsIt = entry.find("fields");
            if (fieldsIt == entry.end() || !fieldsIt->is_object())
            {
                continue;
            }
            for (nlohmann::json& value : *fieldsIt)
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
    //! 配置物と Component の id は同じ空間なので、1 つの表で両方を付け替える
    void RemapObjectRefs(nlohmann::json& object, const std::unordered_map<std::uint32_t, std::uint32_t>& idMap);
} // namespace NS::Obj
