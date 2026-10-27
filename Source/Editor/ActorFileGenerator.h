#pragma once

#include "NSlib/CoreObject/ActorJson.h"

#include <string>
#include <vector>

namespace NS
{
    namespace Editor
    {
        //! actorFile に未記載の項目をクラスのコード既定値で補完した文書を返す（参照欄・派生 .actor・未登録クラスは対象外）。補完した項目は "部品/欄" の形で added に追記する
        [[nodiscard]] nlohmann::json CompleteActorFile(const nlohmann::json& actorFile, std::vector<std::string>& added);

        //! directory 内の全 .actor を CompleteActorFile で補完し、変更があったファイルのみ保存して件数を返す
        int GenerateActorFiles(const std::string& directory);
    } // namespace Editor
} // namespace NS
