#pragma once

// 全部の項目を書いた .actor を、クラスのコードの値から作る。エディタの起動とテストの起動が呼び、出荷ビルドには載らない

#include "NSlib/CoreObject/ActorJson.h"

#include <string>
#include <vector>

namespace NS
{
    namespace Editor
    {
        //! @brief actorFile に、そのクラスのコードの値で、書いていない項目を書き足した文書
        //! @details 書いてある項目は値のまま残す。参照の欄は書かない。
        //! 派生の .actor は元から受け継ぐので、そのまま返す。登録の無いクラスもそのまま返す。
        //! 書き足した項目を "部品/欄" の形で added の後ろに足す
        [[nodiscard]] nlohmann::json CompleteActorFile(const nlohmann::json& actorFile, std::vector<std::string>& added);

        //! @brief directory の全部の .actor を CompleteActorFile で書き足し、変わったファイルだけを書く
        //! @details ActorFileLibrary を通さずにファイルを直に読む。書き足した項目はファイルごとに INFO で出す。
        //! 読めないファイルは ERROR を出して飛ばす
        //! @return 書いたファイルの数
        int GenerateActorFiles(const std::string& directory);
    } // namespace Editor
} // namespace NS
