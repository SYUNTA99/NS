#pragma once

namespace NS::Physics::detail
{
    //! Jolt の allocator・Factory・型を登録する。2 度目からは何もしない。登録の解除はプロセス終了時
    void InitJoltRuntime();
} // namespace NS::Physics::detail
