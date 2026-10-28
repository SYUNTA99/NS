#pragma once

#include "NSlib/CoreObject/Object.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"

#include <map>
#include <memory>
#include <string>
#include <string_view>

namespace NS
{
    class EffectSet : public Object
    {
    public:
        explicit EffectSet(std::string id) noexcept;
        const std::string& Id() const noexcept { return m_id; }
        const std::string& Path() const noexcept { return m_path; }
        const std::string& Label() const noexcept { return m_label; }
        bool IsValid() const noexcept { return m_valid; }

        // 設定型は呼び出し側が渡し、素材を読む層はゲームの型を知らない。
        template <class T> const T& Settings()
        {
            const std::string key = std::string{T::StaticReflection()->typeName};
            std::map<std::string, std::unique_ptr<Object>>::iterator found = m_settings.find(key);
            if (found == m_settings.end())
            {
                std::unique_ptr<T> settings = std::make_unique<T>();
                m_defaults.emplace(key, SerializeClassObjFields(settings.get()));
                ApplySettings(settings.get());
                found = m_settings.emplace(key, std::move(settings)).first;
            }
            return *static_cast<const T*>(found->second.get());
        }

        const std::map<std::string, std::unique_ptr<Object>>& Sections() const noexcept { return m_settings; }
        nlohmann::json Document() const;
        bool Replace(std::string path, const nlohmann::json& document);
        void Invalidate();

        NS_REFLECT_NONE(EffectSet, Object)

    private:
        void ApplySettings(Object* settings);

        std::string m_id;
        std::string m_path;
        std::string m_label;
        nlohmann::json m_document = nlohmann::json::object();
        std::map<std::string, std::unique_ptr<Object>> m_settings;
        std::map<std::string, nlohmann::json> m_defaults;
        bool m_valid = false;
    };
} // namespace NS
