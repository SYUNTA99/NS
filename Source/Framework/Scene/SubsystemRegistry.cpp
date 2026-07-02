#include "Framework/Scene/SubsystemRegistry.h"

namespace NS::Scene
{
    SubsystemRegistry& SubsystemRegistry::Get() noexcept
    {
        // 関数内 static で初期化順を確定させ、他 TU の静的登録より先に器を用意する
        static SubsystemRegistry instance;
        return instance;
    }

    void SubsystemRegistry::Register(const SubsystemEntry& entry)
    {
        m_entries.push_back(entry);
    }

    const std::vector<SubsystemEntry>& SubsystemRegistry::Entries() const noexcept
    {
        return m_entries;
    }
} // namespace NS::Scene
