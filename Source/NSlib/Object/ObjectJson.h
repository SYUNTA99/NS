#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/Object/Reflection/ActorRef.h"

#pragma warning(push, 0)
#include "ThirdParty/nlohmann/json.hpp"
#pragma warning(pop)

#include "NSlib/Core/Types.h"
#include <string_view>
#include <unordered_map>

namespace NS::Obj
{
    //! @brief 配置物の "subObjects" を返す
    //! @return 無いか object でなければ空の object
    [[nodiscard]] const nlohmann::json& ObjectJsonSubObjs(const nlohmann::json& object) noexcept;
    //! @brief 配置物の "subObjects" を返す。無いか object でなければ空の object を作って入れる
    [[nodiscard]] nlohmann::json& ObjectJsonSubObjs(nlohmann::json& object);
    //! @brief 部品名 partName の欄の object を返す
    //! @return 無いか object でなければ nullptr
    [[nodiscard]] const nlohmann::json* SubObjFields(const nlohmann::json& object, std::string_view partName) noexcept;
    //! @brief 部品名 partName の欄の object を返す。無い部品は作らない
    //! @return 無いか object でなければ nullptr
    [[nodiscard]] nlohmann::json* SubObjFields(nlohmann::json& object, std::string_view partName) noexcept;
    //! 配置物の保存・凍結・編集履歴で共用する JSON。参照はメモリ上では id、ファイルでは名前
    [[nodiscard]] nlohmann::json MakeObjectJson();

    //! 配置物の永続 id。未採番と壊れた形は 0
    [[nodiscard]] NS::uint32 ObjectJsonId(const nlohmann::json& object) noexcept;
    //! 配置物の永続 id を書く
    void SetObjectJsonId(nlohmann::json& object, NS::uint32 id);

    //! 生成する Actor のクラス名。空は素の Actor
    [[nodiscard]] std::string_view ObjectJsonClass(const nlohmann::json& object) noexcept;
    //! クラス名を書く。空は素の Actor の印なので消す
    void SetObjectJsonClass(nlohmann::json& object, std::string_view className);

    //! 配置物の名前。空は未設定で、読込の一意化が名前を振る
    [[nodiscard]] std::string_view ObjectJsonName(const nlohmann::json& object) noexcept;
    //! 名前を書く。空は未設定の印なので消す
    void SetObjectJsonName(nlohmann::json& object, std::string_view name);

    //! 親の永続識別番号。0 は根。transform は親の座標系での値
    [[nodiscard]] NS::uint32 ObjectJsonParent(const nlohmann::json& object) noexcept;
    //! 親の永続 id を書く。0 は root の印なので消す
    void SetObjectJsonParent(nlohmann::json& object, NS::uint32 parentId);

    //! 配置物自身の active 値。欄が無ければ有効
    [[nodiscard]] bool ObjectJsonActive(const nlohmann::json& object) noexcept;
    //! active 値を書く。有効は既定なので消す
    void SetObjectJsonActive(nlohmann::json& object, bool active);

    //! 参照の欄の値の形 {"ref": …} の場合 true、それ以外の場合は false
    [[nodiscard]] inline bool IsRefValue(const nlohmann::json& value) noexcept
    {
        return value.is_object() && value.contains("ref");
    }

    //! fn はその object を受け取る。値はメモリ上では id、ファイル上では名前
    //! 参照の欄を辿る処理はここだけに置き、欄の形を知る場所を 1 つにする
    template <class Fn> void ForEachRefValue(nlohmann::json& object, Fn&& fn)
    {
        nlohmann::json& subObjects = ObjectJsonSubObjs(object);
        for (nlohmann::json& entry : subObjects)
        {
            if (!entry.is_object())
            {
                continue;
            }
            for (nlohmann::json& value : entry)
            {
                if (IsRefValue(value))
                {
                    fn(value);
                }
            }
        }
    }

    //! @brief object の参照の欄のうち、idMap に載った id を指す物を載った先の id へ付け替える
    //! @details 複製で使う。コピーした範囲の中を指す参照だけをコピー側へ向け、範囲の外を指す参照は元のまま残す
    void RemapObjectRefs(nlohmann::json& object, const std::unordered_map<NS::uint32, NS::uint32>& idMap);

    //! 配置物の JSON の姿勢を読み書きする。
    //! 実体は subObjects 内の k_TransformSubObjName の欄
    //! Set は対象エントリが無ければ EnsureTransformSubObject で 1 つ作る
    [[nodiscard]] NS::Vector3 ObjectPosition(const nlohmann::json& object) noexcept;
    void SetObjectPosition(nlohmann::json& object, const NS::Vector3& position) noexcept;
    [[nodiscard]] NS::Quaternion ObjectRotation(const nlohmann::json& object) noexcept;
    void SetObjectRotation(nlohmann::json& object, const NS::Quaternion& rotation) noexcept;
    [[nodiscard]] NS::Vector3 ObjectScale(const nlohmann::json& object) noexcept;
    void SetObjectScale(nlohmann::json& object, const NS::Vector3& scale) noexcept;

    //! object に Transform のエントリが無ければ既定値(位置0 / 回転なし / スケール1)で 1 つ足して返す
    //! 既にあればそれを返す。シーンの JSON を読む所で通し、全 object が Transform を必ず 1 つ持つようにする
    nlohmann::json& EnsureTransformSubObject(nlohmann::json& object);
} // namespace NS::Obj
