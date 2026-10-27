#pragma once

namespace NS
{
    namespace Phys
    {
        namespace detail
        {
            /// Jolt の allocator・Factory・型を登録する。2 度目からは何もしない。登録の解除はプロセス終了時
            void InitJoltRuntime();
        } // namespace detail
    } // namespace Phys
} // namespace NS
