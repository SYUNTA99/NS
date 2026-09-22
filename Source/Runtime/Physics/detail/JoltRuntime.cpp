#include "Runtime/Physics/detail/JoltRuntime.h"

#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/RegisterTypes.h>

#include <memory>

namespace NS::Phys::detail
{
    namespace
    {
        class JoltRuntime
        {
        public:
            JoltRuntime()
            {
                JPH::RegisterDefaultAllocator();
                m_factory = std::make_unique<JPH::Factory>();
                JPH::Factory::sInstance = m_factory.get();
                JPH::RegisterTypes();
            }

            ~JoltRuntime()
            {
                JPH::UnregisterTypes();
                JPH::Factory::sInstance = nullptr;
            }

        private:
            std::unique_ptr<JPH::Factory> m_factory;
        };
    } // namespace

    void InitJoltRuntime()
    {
        static JoltRuntime runtime;
    }
} // namespace NS::Phys::detail
