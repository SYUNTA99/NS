#include "Runtime/Object/Reflection/ReflectionJson.h"

#include "Runtime/Core/LogCategories.h"
#include "Runtime/Core/Logger.h"
#include "Runtime/Core/Math.h"
#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/Reflection/Curve.h"
#include "Runtime/Object/Reflection/Reflection.h"

namespace NS::Object
{
    namespace
    {
        // field を JSON 値へ変換する。素の配列で書くのは Vector3=[x,y,z] と Quaternion=[x,y,z,w]
        nlohmann::json FieldToJson(const Component& comp, const FieldDesc& field)
        {
            switch (field.type)
            {
            case FieldType::Float:
            {
                float value = 0.0f;
                field.get(&comp, &value);
                return value;
            }
            case FieldType::Int:
            {
                int value = 0;
                field.get(&comp, &value);
                return value;
            }
            case FieldType::Bool:
            {
                bool value = false;
                field.get(&comp, &value);
                return value;
            }
            case FieldType::Vector3:
            {
                NS::Core::Vector3 value{};
                field.get(&comp, &value);
                return nlohmann::json{value.x, value.y, value.z};
            }
            case FieldType::Quaternion:
            {
                // 配列の長さで Vector3 と区別できるため単キー object で包まない
                NS::Core::Quaternion value{};
                field.get(&comp, &value);
                return nlohmann::json{value.x, value.y, value.z, value.w};
            }
            case FieldType::String:
            {
                std::string value;
                field.get(&comp, &value);
                return value;
            }
            case FieldType::ObjectRef:
            {
                // 素の数値だと読み込み時に Int と区別できないため {"ref": id} の単キー object で書く
                ObjectRef value{};
                field.get(&comp, &value);
                nlohmann::json out;
                out["ref"] = value.id;
                return out;
            }
            case FieldType::Curve:
            {
                // 素の配列だと読み込み時に Vector3 と区別できないため {"curve": [[x,y], ...]} の単キー object で書く
                Curve value{};
                field.get(&comp, &value);
                nlohmann::json points = nlohmann::json::array();
                for (std::uint32_t i = 0; i < value.count; ++i)
                {
                    const Curve::Key& key = value.keys[i];
                    // 直線の点は従来の 2 要素のまま書く。要素を足すと古い記述との互換が切れるため
                    if (key.mode == Curve::InterpMode::Linear)
                    {
                        points.push_back(nlohmann::json{key.x, key.y});
                        continue;
                    }
                    if (key.mode == Curve::InterpMode::AutoSmooth)
                    {
                        points.push_back(nlohmann::json{key.x, key.y, static_cast<int>(Curve::InterpMode::AutoSmooth)});
                        continue;
                    }
                    points.push_back(nlohmann::json{
                        key.x, key.y, static_cast<int>(Curve::InterpMode::Manual), key.inTangent, key.outTangent});
                }
                nlohmann::json out;
                out["curve"] = std::move(points);
                return out;
            }
            }
            return nlohmann::json{};
        }

        // 型が合わなければ前方互換のため何もしない
        void JsonToField(Component& comp, const FieldDesc& field, const nlohmann::json& value)
        {
            switch (field.type)
            {
            case FieldType::Float:
            {
                if (!value.is_number())
                {
                    return;
                }
                float v = value.get<float>();
                field.set(&comp, &v);
                return;
            }
            case FieldType::Int:
            {
                // 手編集 JSON が 1.0 形式で書いても拾えるよう数値全般を受け、int へ切り捨てる
                if (!value.is_number())
                {
                    return;
                }
                int v = value.get<int>();
                field.set(&comp, &v);
                return;
            }
            case FieldType::Bool:
            {
                if (!value.is_boolean())
                {
                    return;
                }
                bool v = value.get<bool>();
                field.set(&comp, &v);
                return;
            }
            case FieldType::Vector3:
            {
                if (!value.is_array() || value.size() != 3u)
                {
                    return;
                }
                if (!value[0].is_number() || !value[1].is_number() || !value[2].is_number())
                {
                    return;
                }
                NS::Core::Vector3 v{value[0].get<float>(), value[1].get<float>(), value[2].get<float>()};
                field.set(&comp, &v);
                return;
            }
            case FieldType::Quaternion:
            {
                if (!value.is_array() || value.size() != 4u)
                {
                    return;
                }
                if (!value[0].is_number() || !value[1].is_number() || !value[2].is_number() || !value[3].is_number())
                {
                    return;
                }
                NS::Core::Quaternion v{
                    value[0].get<float>(), value[1].get<float>(), value[2].get<float>(), value[3].get<float>()};
                field.set(&comp, &v);
                return;
            }
            case FieldType::String:
            {
                if (!value.is_string())
                {
                    return;
                }
                std::string v = value.get<std::string>();
                field.set(&comp, &v);
                return;
            }
            case FieldType::ObjectRef:
            {
                if (!value.is_object())
                {
                    return;
                }
                const auto it = value.find("ref");
                // 負数は id として不正なので unsigned のみ受ける。手編集の壊れた値は既定 0 のまま
                if (it == value.end() || !it->is_number_unsigned())
                {
                    return;
                }
                ObjectRef v{it->get<std::uint32_t>()};
                field.set(&comp, &v);
                return;
            }
            case FieldType::Curve:
            {
                if (!value.is_object())
                {
                    return;
                }
                const auto it = value.find("curve");
                if (it == value.end() || !it->is_array())
                {
                    return;
                }
                Curve v{};
                for (const auto& point : *it)
                {
                    if (v.count >= Curve::k_MaxKeys)
                    {
                        NS_LOG_WARN(Game, "Curve の点が上限を超えているため捨てる");
                        break;
                    }
                    // 点が 1 個壊れただけで全部を捨てると手編集の損害が広がるので、形の違う点だけ飛ばして残りを読む
                    if (!point.is_array())
                    {
                        continue;
                    }
                    // 2 は直線、3 は自動なめらか、5 は手動接線。他の要素数は形が壊れた点として飛ばす
                    const std::size_t pointSize = point.size();
                    if (pointSize != 2u && pointSize != 3u && pointSize != 5u)
                    {
                        NS_LOG_WARN(Game, "Curve の点の要素数が不正なため捨てる");
                        continue;
                    }
                    if (!point[0].is_number() || !point[1].is_number())
                    {
                        NS_LOG_WARN(Game, "Curve の点の座標が不正なため捨てる");
                        continue;
                    }

                    Curve::Key key{point[0].get<float>(), point[1].get<float>()};
                    if (pointSize >= 3u)
                    {
                        if (!point[2].is_number())
                        {
                            NS_LOG_WARN(Game, "Curve の点のモード番号が不正なため捨てる");
                            continue;
                        }

                        // 要素数とモード番号が食い違う点は手編集で壊れた点なので飛ばす
                        const int mode = point[2].get<int>();
                        if (pointSize == 3u)
                        {
                            if (mode != static_cast<int>(Curve::InterpMode::AutoSmooth))
                            {
                                continue;
                            }
                            key.mode = Curve::InterpMode::AutoSmooth;
                        }
                        else
                        {
                            if (mode != static_cast<int>(Curve::InterpMode::Manual))
                            {
                                NS_LOG_WARN(Game, "Curve の点のモード番号が不正なため捨てる");
                                continue;
                            }
                            if (!point[3].is_number() || !point[4].is_number())
                            {
                                NS_LOG_WARN(Game, "Curve の点の接線が不正なため捨てる");
                                continue;
                            }
                            key.mode = Curve::InterpMode::Manual;
                            key.inTangent = point[3].get<float>();
                            key.outTangent = point[4].get<float>();
                        }
                    }
                    v.keys[v.count] = key;
                    ++v.count;
                }
                // 降順に書かれた記述だと Evaluate の昇順前提が崩れるため、読み込み直後に並べ直す
                v.SortKeys();
                field.set(&comp, &v);
                return;
            }
            }
        }

        bool IsReflectedFieldName(const ReflectionInfo& info, std::string_view name) noexcept
        {
            for (std::size_t i = 0; i < info.fieldCount; ++i)
            {
                if (name == info.fields[i].name)
                {
                    return true;
                }
            }
            return false;
        }
    } // namespace

    nlohmann::json SerializeComponent(const Component& comp)
    {
        nlohmann::json out;
        const ReflectionInfo* info = comp.GetReflection();
        if (info == nullptr)
        {
            // リフレクションの無い component は type を復元できない。宣言の書き忘れに気付けるよう警告する
            NS_LOG_WARN(Game, "リフレクションの無い Component を直列化しようとした (type 復元不可)");
            out["type"] = "";
            out["fields"] = nlohmann::json::object();
            return out;
        }

        out["type"] = info->typeName;
        nlohmann::json fields = nlohmann::json::object();
        for (std::size_t i = 0; i < info->fieldCount; ++i)
        {
            const FieldDesc& field = info->fields[i];
            fields[field.name] = FieldToJson(comp, field);
        }
        out["fields"] = std::move(fields);
        return out;
    }

    nlohmann::json SerializeGameObjectComponents(const GameObject& obj)
    {
        nlohmann::json components = nlohmann::json::array();
        for (const Component* comp : obj.Components())
        {
            if (comp == nullptr)
            {
                NS_LOG_WARN(Game,
                            "GameObject に nullptr Component が混ざっている。AddComponent で nullptr "
                            "を返す派生型があるか、 AddComponent 後に手動 delete したか");
                continue;
            }
            components.push_back(SerializeComponent(*comp));
        }
        return components;
    }

    std::size_t ApplyJsonFields(Component& comp, const nlohmann::json& fields)
    {
        const ReflectionInfo* info = comp.GetReflection();
        if (info == nullptr || !fields.is_object())
        {
            return 0;
        }

        for (std::size_t i = 0; i < info->fieldCount; ++i)
        {
            const FieldDesc& field = info->fields[i];
            const auto it = fields.find(field.name);
            if (it == fields.end())
            {
                continue; // 欠損キーは既定値のまま据え置く
            }
            JsonToField(comp, field, *it);
        }

        std::size_t unreadCount = 0;
        for (const auto& entry : fields.items())
        {
            if (IsReflectedFieldName(*info, entry.key()))
            {
                continue;
            }
            ++unreadCount;
            NS_LOG_WARN(Game,
                        "{} に読み手のいない欄 {} がある。 欄名を変えたなら保存済みの値は既定へ戻っている",
                        info->typeName,
                        entry.key());
        }
        return unreadCount;
    }
} // namespace NS::Object
