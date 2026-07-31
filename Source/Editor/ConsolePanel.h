#pragma once

#include "Runtime/Core/NonCopyable.h"

namespace NS::Editor
{
    //! @brief フレーム統計を出すコンソールパネル
    class ConsolePanel : public NS::Core::NonCopyable
    {
    public:
        //! FPS と delta / fixed step を 1 枚描く
        void Render() noexcept;
    };
} // namespace NS::Editor
