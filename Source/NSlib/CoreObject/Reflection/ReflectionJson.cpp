#include "NSlib/CoreObject/Reflection/ReflectionJson.h"
#include "NSlib/Core/Types.h"

#include "NSlib/Core/Logger.h"
#include "NSlib/Core/Math.h"
#include "NSlib/CoreObject/ObjectJson.h"
#include "NSlib/CoreObject/Reflection/ClassObjEntry.h"
#include "NSlib/CoreObject/Reflection/Curve.h"
#include "NSlib/CoreObject/Reflection/Reflection.h"

namespace NS
{
    namespace
    {
        // field.type が T を指している時だけ呼ぶ
        template <class T> T ReadField(const void* owner, const FieldDesc& field)
        {
            T value{};
            field.get(owner, &value);
            return value;
        }

        // 数 4 個の [r,g,b,a] を読む。形式が違えば出力を変更せず false
        bool ReadColor(const nlohmann::json& value, Color& out) noexcept
        {
            if (!value.is_array() || value.size() != 4u)
            {
                return false;
            }
            if (!value[0].is_number() || !value[1].is_number() || !value[2].is_number() || !value[3].is_number())
            {
                return false;
            }
            out = Color{value[0].get<float>(), value[1].get<float>(), value[2].get<float>(), value[3].get<float>()};
            return true;
        }

        // null は空、[r,g,b,a] は色。形式が違えば出力を変更せず false
        bool ReadOptionalColor(const nlohmann::json& value, std::optional<Color>& out) noexcept
        {
            if (value.is_null())
            {
                out.reset();
                return true;
            }
            Color color;
            if (!ReadColor(value, color))
            {
                return false;
            }
            out = color;
            return true;
        }

        nlohmann::json MakeColorJson(const Color& value)
        {
            return nlohmann::json{value.x, value.y, value.z, value.w};
        }

        // ベクトルは成分数の配列、回転と色は4成分の配列で保存する
        nlohmann::json FieldToJson(const void* owner, const FieldDesc& field)
        {
            switch (field.type)
            {
            case FieldType::Float:
                return ReadField<float>(owner, field);
            case FieldType::Int:
                return ReadField<int>(owner, field);
            case FieldType::Bool:
                return ReadField<bool>(owner, field);
            case FieldType::Vector2:
            {
                const Vector2 value = ReadField<Vector2>(owner, field);
                return nlohmann::json{value.x, value.y};
            }
            case FieldType::Vector3:
                return MakeVector3Json(ReadField<Vector3>(owner, field));
            case FieldType::Quaternion:
                // 配列の長さで Vector3 と区別できるため単キー object で包まない
                return MakeQuaternionJson(ReadField<Quaternion>(owner, field));
            case FieldType::Color:
                return MakeColorJson(ReadField<Color>(owner, field));
            case FieldType::OptionalColor:
            {
                const std::optional<Color> value = ReadField<std::optional<Color>>(owner, field);
                if (!value.has_value())
                {
                    return nlohmann::json(nullptr);
                }
                return MakeColorJson(*value);
            }
            case FieldType::String:
            case FieldType::Asset:
                return ReadField<std::string>(owner, field);
            case FieldType::ActorRef:
                return MakeRefValue(ReadField<ActorRef>(owner, field));
            case FieldType::Curve:
            {
                // 素の配列だと読み込み時に Vector3 と区別できないため {"curve": [[x,y], ...]} の単キー object で書く
                const Curve value = ReadField<Curve>(owner, field);
                nlohmann::json points = nlohmann::json::array();
                for (uint32 i = 0; i < value.count; ++i)
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
        void JsonToField(void* owner, const FieldDesc& field, const nlohmann::json& value)
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
                field.set(owner, &v);
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
                field.set(owner, &v);
                return;
            }
            case FieldType::Bool:
            {
                if (!value.is_boolean())
                {
                    return;
                }
                bool v = value.get<bool>();
                field.set(owner, &v);
                return;
            }
            case FieldType::Vector2:
            {
                if (!value.is_array() || value.size() != 2u || !value[0].is_number() || !value[1].is_number())
                {
                    return;
                }
                const Vector2 v{value[0].get<float>(), value[1].get<float>()};
                if (std::isfinite(v.x) && std::isfinite(v.y))
                {
                    field.set(owner, &v);
                }
                return;
            }
            case FieldType::Vector3:
            {
                Vector3 v;
                if (ReadVector3(value, v))
                {
                    field.set(owner, &v);
                }
                return;
            }
            case FieldType::Quaternion:
            {
                Quaternion v;
                if (ReadQuaternion(value, v))
                {
                    field.set(owner, &v);
                }
                return;
            }
            case FieldType::Color:
            {
                // 透明度の無い 3 成分は前の形の色。読めずに今の値のまま残るので、警告で知らせる
                Color v;
                if (!ReadColor(value, v))
                {
                    NS_LOG_WARN(Game, "色の欄 {} は [r,g,b,a] の 4 つの数でないため読まない", field.name);
                    return;
                }
                field.set(owner, &v);
                return;
            }
            case FieldType::OptionalColor:
            {
                std::optional<Color> v;
                if (!ReadOptionalColor(value, v))
                {
                    NS_LOG_WARN(Game, "色の欄 {} は null か [r,g,b,a] の 4 つの数でないため読まない", field.name);
                    return;
                }
                field.set(owner, &v);
                return;
            }
            case FieldType::String:
            case FieldType::Asset:
            {
                if (!value.is_string())
                {
                    return;
                }
                std::string v = value.get<std::string>();
                field.set(owner, &v);
                return;
            }
            case FieldType::ActorRef:
            {
                // 手編集の壊れた値は既定 0 のまま
                ActorRef v{};
                if (ReadRefValue(value, v))
                {
                    field.set(owner, &v);
                }
                return;
            }
            case FieldType::Curve:
            {
                if (!value.is_object())
                {
                    return;
                }
                const nlohmann::json::const_iterator it = value.find("curve");
                if (it == value.end() || !it->is_array())
                {
                    return;
                }
                Curve v{};
                for (const nlohmann::json& point : *it)
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
                field.set(owner, &v);
                return;
            }
            }
        }
    } // namespace

    nlohmann::json SerializeClassObjFields(const Object& comp)
    {
        const ReflectionInfo* info = comp.GetReflection();
        if (info == nullptr)
        {
            NS_LOG_WARN(Game, "リフレクションの無い部品を直列化しようとした");
            return nlohmann::json::object();
        }
        return SerializeReflectedFields(&comp, *info);
    }

    nlohmann::json SerializeReflectedFields(const void* owner, const ReflectionInfo& info)
    {
        nlohmann::json fields = nlohmann::json::object();
        for (std::size_t i = 0; i < info.fieldCount; ++i)
        {
            const FieldDesc& field = info.fields[i];
            fields[field.name] = FieldToJson(owner, field);
        }
        return fields;
    }

    std::size_t ApplyJsonFields(Object& comp, const nlohmann::json& fields)
    {
        const ReflectionInfo* info = comp.GetReflection();
        if (info == nullptr)
        {
            return 0;
        }
        return ApplyReflectedFields(&comp, *info, fields);
    }

    std::size_t ApplyReflectedFields(void* owner, const ReflectionInfo& info, const nlohmann::json& fields)
    {
        if (!fields.is_object())
        {
            return 0;
        }

        for (std::size_t i = 0; i < info.fieldCount; ++i)
        {
            const FieldDesc& field = info.fields[i];
            const nlohmann::json::const_iterator it = fields.find(field.name);
            if (it == fields.end())
            {
                continue; // 欠損キーは既定値のまま据え置く
            }
            JsonToField(owner, field, *it);
        }

        std::size_t unreadCount = 0;
        for (nlohmann::json::const_iterator entry = fields.begin(); entry != fields.end(); ++entry)
        {
            if (FindField(&info, entry.key()) != nullptr)
            {
                continue;
            }
            ++unreadCount;
            NS_LOG_WARN(Game,
                        "{} に読み手のいない欄 {} がある。 欄名を変えたなら保存済みの値は既定へ戻っている",
                        info.typeName,
                        entry.key());
        }
        return unreadCount;
    }
} // namespace NS
