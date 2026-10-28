#pragma once

#include "NSlib/CoreObject/Reflection/ReflectionJson.h"

#include <type_traits>
#include <utility>

template <class Runtime> class TestEffectSettings
{
public:
    using Settings = std::remove_cvref_t<decltype(std::declval<Runtime>().Settings())>;

    explicit TestEffectSettings(Runtime* runtime) : m_runtime(runtime), m_original(&runtime->Settings())
    {
        NS::ApplyJsonFields(&m_settings, NS::SerializeClassObjFields(m_original));
        m_runtime->Configure(&m_settings);
    }
    ~TestEffectSettings() { m_runtime->Configure(m_original); }
    Settings* Get() noexcept { return &m_settings; }

private:
    Runtime* m_runtime;
    const Settings* m_original;
    Settings m_settings;
};
