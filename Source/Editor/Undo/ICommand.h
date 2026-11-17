#pragma once

#include "NSlib/Core/NonCopyable.h"

#include <cstddef>

namespace NS
{
    namespace Ed
    {
        //! スナップショットを適用・取消する編集コマンド。描画資源は保持しない
        //! 履歴のメモリ制限のため EstimatedBytes で推定バイト数を返す
        class ICommand : public NonCopyable
        {
        public:
            virtual ~ICommand() = default;

            virtual void Do() noexcept = 0;
            virtual void Undo() noexcept = 0;

            [[nodiscard]] virtual std::size_t EstimatedBytes() const noexcept = 0;

            //! シーンから外して持っているアクターを、OnEndPlay を呼んでから解放する。前後の JSON は残す
            //! シーンを全部作り直す前に呼ぶ。作り直しは管理クラスを捨てるので、持ったままだと捨てた物を指す
            virtual void ReleaseHeldActors() noexcept {}

            //! 実行と取り消しでシーン (置いたアクター・シーン設定) が変わるか。偽ならシーンを未保存にしない
            //! .actor・.mat はすぐファイルへ書くので、それだけを変えるコマンドは偽を返す
            [[nodiscard]] virtual bool ChangesScene() const noexcept { return true; }

        protected:
            ICommand() = default;
        };
    } // namespace Ed
} // namespace NS
