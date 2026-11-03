#include "NSlib/CoreObject/Asset/MaterialFile.h"

#include "NSlib/Core/Logger.h"
#include "NSlib/Core/Math.h"
#include "NSlib/CoreObject/Asset/Material.h"
#include "NSlib/CoreObject/Reflection/ClassObjEntry.h"

#include <cmath>
#include <cstddef>
#include <format>
#include <utility>

namespace NS
{
    namespace
    {
        // .mat に書いて読み直せる値の決まり。読む側と書く側の両方がこれで断り、決まりを写さない
        [[nodiscard]] bool IsValidParamValue(const MaterialParamValue& value) noexcept
        {
            if (value.numbers.empty() || value.numbers.size() > 4u)
            {
                return false;
            }
            if (value.isBool && value.numbers.size() != 1u)
            {
                return false;
            }
            for (const double number : value.numbers)
            {
                if (!std::isfinite(number))
                {
                    return false;
                }
            }
            return true;
        }

        // .mat の params の値 1 つ。ここで見るのは JSON の型だけで、数と個数の範囲は IsValidParamValue が断る
        [[nodiscard]] std::optional<MaterialParamValue> ParseParamValue(const nlohmann::json& j)
        {
            if (j.is_boolean())
            {
                MaterialParamValue value{.isBool = true};
                if (j.get<bool>())
                {
                    value.numbers.push_back(1.0);
                }
                else
                {
                    value.numbers.push_back(0.0);
                }
                return value;
            }
            MaterialParamValue value;
            if (j.is_number())
            {
                value.numbers.push_back(j.get<double>());
            }
            else if (j.is_array())
            {
                for (const nlohmann::json& element : j)
                {
                    if (!element.is_number())
                    {
                        return std::nullopt;
                    }
                    value.numbers.push_back(element.get<double>());
                }
            }
            else
            {
                return std::nullopt;
            }
            if (!IsValidParamValue(value))
            {
                return std::nullopt;
            }
            return value;
        }

        // 壊れた値は名前を出して 1 つずつ捨てる。1 つのために .mat 全体をピンクにしない
        [[nodiscard]] MaterialParamValues ParseParams(const nlohmann::json& root)
        {
            MaterialParamValues params;
            if (!root.contains("params"))
            {
                return params;
            }
            const nlohmann::json& j = root["params"];
            if (!j.is_object())
            {
                NS_LOG_WARN(Scene, "マテリアルの params が object でないので、材質の値を読まない");
                return params;
            }
            for (nlohmann::json::const_iterator it = j.begin(); it != j.end(); ++it)
            {
                std::optional<MaterialParamValue> value = ParseParamValue(it.value());
                if (!value)
                {
                    NS_LOG_WARN(Scene,
                                "マテリアルの値 {} は bool・有限の数・有限の数 1〜4 個の配列のどれでもないので捨てる",
                                it.key());
                    continue;
                }
                params.emplace(it.key(), std::move(*value));
            }
            return params;
        }
    } // namespace

    bool ParseMaterialJson(std::string_view jsonText, MaterialFileDesc& out, std::string& outError)
    {
        // 例外を投げない parse。不正 JSON は is_discarded() で検知する
        const nlohmann::json j = nlohmann::json::parse(jsonText, nullptr, false);
        if (j.is_discarded())
        {
            outError = "JSON parse に失敗";
            return false;
        }
        if (!j.is_object())
        {
            outError = "ルートが object でない";
            return false;
        }

        if (!j.contains("vs") || !j["vs"].is_string() || !j.contains("ps") || !j["ps"].is_string())
        {
            outError = "vs / ps (string) が必要";
            return false;
        }
        out.vertexShader = j["vs"].get<std::string>();
        out.pixelShader = j["ps"].get<std::string>();

        out.textures.clear();
        if (j.contains("textures") && j["textures"].is_array())
        {
            for (const nlohmann::json& tex : j["textures"])
            {
                if (tex.is_string())
                {
                    out.textures.emplace_back(tex.get<std::string>());
                }
            }
        }

        out.baseColor = FieldVector3(j, "baseColor", Vector3{1.0f, 1.0f, 1.0f});

        out.blend = Gfx::BlendMode::Opaque;
        if (j.contains("blend") && j["blend"].is_string())
        {
            out.blend = ParseBlend(j["blend"].get<std::string>()).value_or(Gfx::BlendMode::Opaque);
        }

        out.params = ParseParams(j);

        outError.clear();
        return true;
    }

    namespace
    {
        // 読み直すと同じ浮動小数点数になる一番短い綴り。整数の値は今の .mat と同じく .0 を付ける
        template <typename T> [[nodiscard]] std::string NumberJson(T value)
        {
            std::string text = std::format("{}", value);
            if (text.find_first_of(".e") == std::string::npos)
            {
                text += ".0";
            }
            return text;
        }

        [[nodiscard]] std::string ParamJson(const MaterialParamValue& value)
        {
            if (value.isBool)
            {
                if (value.numbers[0] != 0.0)
                {
                    return "true";
                }
                return "false";
            }
            if (value.numbers.size() == 1u)
            {
                return NumberJson(value.numbers[0]);
            }
            std::string text = "[";
            for (std::size_t i = 0; i < value.numbers.size(); ++i)
            {
                if (i > 0)
                {
                    text += ", ";
                }
                text += NumberJson(value.numbers[i]);
            }
            return text + "]";
        }

        // nlohmann は UTF-8 でない文字列で例外を投げるので、置き換え文字で書く
        [[nodiscard]] std::string StringJson(std::string_view value)
        {
            return nlohmann::json(value).dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        }
    } // namespace

    std::optional<std::string> MakeMaterialJson(const MaterialFileDesc& file)
    {
        if (file.textures.size() > k_MaterialTextureSlots)
        {
            return std::nullopt;
        }
        const Vector3& color = file.baseColor;
        if (!std::isfinite(color.x) || !std::isfinite(color.y) || !std::isfinite(color.z))
        {
            return std::nullopt;
        }
        for (const std::pair<const std::string, MaterialParamValue>& param : file.params)
        {
            if (!IsValidParamValue(param.second))
            {
                return std::nullopt;
            }
        }

        std::string text = "{\n";
        text += "  \"vs\": " + StringJson(file.vertexShader) + ",\n";
        text += "  \"ps\": " + StringJson(file.pixelShader) + ",\n";
        if (!file.textures.empty())
        {
            text += "  \"textures\": [";
            for (std::size_t i = 0; i < file.textures.size(); ++i)
            {
                if (i > 0)
                {
                    text += ", ";
                }
                text += StringJson(file.textures[i]);
            }
            text += "],\n";
        }
        text += std::format(
            "  \"baseColor\": [{}, {}, {}],\n", NumberJson(color.x), NumberJson(color.y), NumberJson(color.z));
        text += "  \"blend\": " + StringJson(BlendName(file.blend));
        // params の無い .mat は、params を持つ前と同じ字面に書く
        if (!file.params.empty())
        {
            text += ",\n  \"params\": {\n";
            std::size_t written = 0;
            for (const std::pair<const std::string, MaterialParamValue>& param : file.params)
            {
                if (written > 0)
                {
                    text += ",\n";
                }
                text += "    " + StringJson(param.first) + ": " + ParamJson(param.second);
                ++written;
            }
            text += "\n  }";
        }
        text += "\n}\n";
        return text;
    }
} // namespace NS
