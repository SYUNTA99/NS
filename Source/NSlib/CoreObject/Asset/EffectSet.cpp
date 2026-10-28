#include "NSlib/CoreObject/Asset/EffectSet.h"

#include "NSlib/Core/Logger.h"
#include <utility>

namespace NS
{
    EffectSet::EffectSet(std::string id) noexcept : m_id(std::move(id)) {}

    void EffectSet::Invalidate()
    {
        m_valid = false;
        for (const std::pair<const std::string, std::unique_ptr<Object>>& entry : m_settings)
        {
            ApplyJsonFields(entry.second.get(), m_defaults.at(entry.first));
        }
    }

    void EffectSet::ApplySettings(Object* settings)
    {
        ApplyJsonFields(settings, m_defaults.at(settings->ClassName()));
        const nlohmann::json::const_iterator sections = m_document.find("settings");
        if (sections == m_document.end() || !sections->contains(settings->ClassName()))
        {
            if (!m_id.empty())
            {
                NS_LOG_ERROR(Scene, "演出セット {} に設定 {} が無い", m_id, settings->ClassName());
            }
            return;
        }
        ApplyJsonFields(settings, sections->at(settings->ClassName()));
    }

    bool EffectSet::Replace(std::string path, const nlohmann::json& document)
    {
        if (!document.is_object() || !document.contains("id") || document["id"] != m_id ||
            !document.contains("label") || !document["label"].is_string() || !document.contains("settings") ||
            !document["settings"].is_object())
        {
            NS_LOG_ERROR(Scene, "演出セット {} の定義が不正", m_id);
            return false;
        }
        m_path = std::move(path);
        m_document = document;
        m_label = document["label"].get<std::string>();
        m_valid = true;
        for (const std::pair<const std::string, std::unique_ptr<Object>>& entry : m_settings)
        {
            ApplySettings(entry.second.get());
        }
        return true;
    }

    nlohmann::json EffectSet::Document() const
    {
        nlohmann::json document = m_document;
        for (const std::pair<const std::string, std::unique_ptr<Object>>& entry : m_settings)
        {
            nlohmann::json& fields = document["settings"][entry.first];
            if (!fields.is_object())
            {
                fields = nlohmann::json::object();
            }
            fields.update(SerializeClassObjFields(entry.second.get()));
        }
        return document;
    }
} // namespace NS
