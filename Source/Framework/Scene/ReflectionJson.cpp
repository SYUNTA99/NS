#include "Framework/Scene/ReflectionJson.h"

#include "Framework/Core/LogCategories.h"
#include "Framework/Core/Logger.h"
#include "Framework/Math/Math.h"
#include "Framework/Scene/Reflection.h"

#include <cstddef>
#include <string>

namespace NS::Scene
{
    namespace
    {
        // 1 つの反射 field を get で読み出し JSON 値へ変換する。 Vector3 は [x,y,z] 配列
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
                NS::Math::Vector3 value{};
                field.get(&comp, &value);
                return nlohmann::json{value.x, value.y, value.z};
            }
            case FieldType::String:
            {
                std::string value;
                field.get(&comp, &value);
                return value;
            }
            }
            return nlohmann::json{};
        }

        // JSON 値を field 型に合わせて取り出し set で書き戻す。 型が合わなければ前方互換のため何もしない
        void JsonToField(Component& comp, const FieldDesc& field, const nlohmann::json& value)
        {
            switch (field.type)
            {
            case FieldType::Float:
            {
                if (!value.is_number())
                    return;
                float v = value.get<float>();
                field.set(&comp, &v);
                return;
            }
            case FieldType::Int:
            {
                // 手編集 JSON が 1.0 形式で書いても拾えるよう数値全般を受け、 int へ切り捨てる
                if (!value.is_number())
                    return;
                int v = value.get<int>();
                field.set(&comp, &v);
                return;
            }
            case FieldType::Bool:
            {
                if (!value.is_boolean())
                    return;
                bool v = value.get<bool>();
                field.set(&comp, &v);
                return;
            }
            case FieldType::Vector3:
            {
                if (!value.is_array() || value.size() != 3u)
                    return;
                if (!value[0].is_number() || !value[1].is_number() || !value[2].is_number())
                    return;
                NS::Math::Vector3 v{value[0].get<float>(), value[1].get<float>(), value[2].get<float>()};
                field.set(&comp, &v);
                return;
            }
            case FieldType::String:
            {
                if (!value.is_string())
                    return;
                std::string v = value.get<std::string>();
                field.set(&comp, &v);
                return;
            }
            }
        }
    } // namespace

    nlohmann::json SerializeComponent(const Component& comp)
    {
        nlohmann::json out;
        const ReflectionInfo* info = comp.GetReflection();
        if (info == nullptr)
        {
            // 反射の無いコンポは type を復元できない。 curated 追加漏れを黙って握り潰さず警告する
            NS_LOG_WARN(::NS::Core::LogCat::Game, "反射の無い Component を直列化しようとした (type 復元不可)");
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

    void ApplyJsonFields(Component& comp, const nlohmann::json& fields)
    {
        const ReflectionInfo* info = comp.GetReflection();
        if (info == nullptr || !fields.is_object())
            return;

        for (std::size_t i = 0; i < info->fieldCount; ++i)
        {
            const FieldDesc& field = info->fields[i];
            const auto it = fields.find(field.name);
            if (it == fields.end())
                continue; // 欠損キーは既定値のまま据え置く
            JsonToField(comp, field, *it);
        }
    }
} // namespace NS::Scene
