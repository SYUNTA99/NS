#include "NSlib/Core/Windows/ComInitialization.h"

#include "NSlib/Core/Logger.h"
#include "NSlib/Core/Types.h"

#include <objbase.h>

namespace NS
{
    namespace OS
    {
        ComInitialization::ComInitialization() noexcept
        {
            // 初期化済みのスレッドでは S_FALSE が返る。これも CoUninitialize と対にする
            const HRESULT hr = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            m_initialized = SUCCEEDED(hr);
            if (!m_initialized)
            {
                NS_LOG_ERROR(Core, "ComInitialization: COM を初期化できない (hr=0x{:08X})", static_cast<uint>(hr));
            }
        }

        ComInitialization::~ComInitialization()
        {
            if (m_initialized)
            {
                ::CoUninitialize();
            }
        }
    } // namespace OS
} // namespace NS
