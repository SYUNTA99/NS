#pragma once

#include "Runtime/Math/Math.h"
#include "Runtime/Object/Reflection/ObjectRef.h"

#pragma warning(push, 0)
#include "ThirdParty/nlohmann/json.hpp"
#pragma warning(pop)

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace NS::Object
{

    /// 配置物の永続表現。 コンポーネント一覧を内包し、 当たりも見た目も transform も components が唯一の出所
    /// 配置物は種別を問わず同じ型で 1 リストに格納する
    /// components は SerializeComponent と同じ {"type": 型名, "fields": {名前: 値}} の JSON 配列で、
    /// 保存形式・undo スナップショット・クリップボードが同じ形を共有する
    /// transform は components 内の TransformComponent エントリが持ち、 EnsureTransformComponent が 1 つを保証する
    /// objectId はシーン内で一意な永続 id で 0 は未割当。並べ替えや改名に耐えるオブジェクト参照のキーになる
    struct ObjectData
    {
        std::uint32_t objectId = 0;

        /// 生成する GameObject のクラス名。 空は素の GameObject。 TypeRegistry の登録名と一致させ、 保存にもこの名で書く
        std::string className;

        /// 編集側が付ける表示名。 空なら型と component から名前を導出する
        std::string name;

        /// 親の永続 id。 0 は root。 transform は親空間の local として解釈される
        std::uint32_t parentId = 0;

        /// 親の中での並び順。 一覧の表示順で、 組み立てもこの順に並べる。 同値は書かれた順のまま残る
        std::uint32_t order = 0;

        /// この配置物自身の active 値。 false なら配下 component が更新も描画も当たりも止まる
        /// 子の値は独立で、 親を戻せば子も一緒に戻る
        bool active = true;

        /// このオブジェクトが持つコンポーネント一覧
        nlohmann::json components = nlohmann::json::array();

        [[nodiscard]] bool operator==(const ObjectData& other) const = default;
    };

    /// シーンが所有する環境値。 保存形式に入る永続データで、 skybox の描画が毎フレームこのパスを読む
    /// 照明は DirectionalLightComponent が供給するので、 ここは skybox だけを持つ
    struct SceneEnvironment
    {
        /// skybox cubemap のディレクトリまたは .dds の ContentRoot 配下相対パス。 空文字なら skybox を描かない
        std::string skyboxCubemapPath{};
    };

    /// @brief シーンの姿を値として持つデータ。配置物一覧とシーン付随プロパティを収める
    /// @details 用途は 3 つあり、ファイル保存はそのうちの 1 つでしかない
    /// - `.scene` の読み書き
    /// - プレイ突入時の凍結 (Scene::BeginPlayBaseline)。やり直しはここから姿を戻す
    /// - エディタの 1 手戻し
    /// live な GameObject は所有と実行時状態を抱えていて複製できないので、
    /// 「後で元へ戻す」には姿を値へ落とすこのデータが要る
    /// 配置物は transform + リフレクションコンポーネント一覧の ObjectData に統一し、
    /// 当たりも見た目も components が唯一の出所になる
    /// プレイ経路には const 参照でしか渡さない
    /// 依存: NS::Math, NS::Object::ObjectRef
    struct SceneData
    {
        /// 全配置物の唯一のリスト
        std::vector<ObjectData> objects;

        /// 次に割り当てる永続 object id。単調増加で欠番は再利用せず、削除済み id が別物を指す事故を防ぐ
        std::uint32_t nextObjectId = 1;

        /// シーンの見た目を確定する環境値。 lighting と skybox
        SceneEnvironment environment{};

        /// field 単位の明示 update で計算。 components は JSON 木を型 tag + 値の順で再帰 hash する
        [[nodiscard]] std::uint32_t ComputeCrc32() const noexcept;
    };

    /// objects 配列で「該当無し」を表す添字
    inline constexpr std::size_t k_NoObjectIndex = static_cast<std::size_t>(-1);

    /// 「object 無し」を表す永続 id の番兵。実 id は採番が 1 始まりで 0 を取らない
    /// ObjectRef フィールドの「0 は未設定」と同じ約束
    inline constexpr std::uint32_t k_NoObjectId = 0;

    /// 永続 id が `id` の object の添字。無ければ k_NoObjectIndex、k_NoObjectId は常に該当無し
    [[nodiscard]] std::size_t FindObjectIndexById(const SceneData& scene, std::uint32_t id) noexcept;

    /// 全 object と全 component の永続 id を「非 0 かつ一意」へ整える。未割当と重複には新 id を振り、
    /// nextObjectId を既存最大 id より先へ進める。手編集のファイルを読込直後に通す整合処理
    /// 番号の空間は object と component で共通なので、id 1 個で世界の誰か 1 人が決まる
    void EnsureUniqueObjectIds(SceneData& scene);

    /// 存在しない object を指す ObjectRef フィールドを未設定 0 へ戻し、直した件数を返す
    /// 手編集や参照先削除で宙に浮いた参照を読込直後に除去し、実行時の照合失敗を入口で断つ
    [[nodiscard]] std::size_t PruneDanglingObjectRefs(SceneData& scene);

    /// 辿れない parentId を root の 0 へ戻し、直した件数を返す。自分自身・不在の親・循環が対象
    /// 循環したまま組むと world 変換の再帰が止まらないので、手編集のファイルを読込直後にここで断つ
    [[nodiscard]] std::size_t PruneInvalidParents(SceneData& scene);

    /// ObjectRef フィールドが指す先を、 参照元 object と component 添字・ フィールド名で特定する
    struct ObjectRefLocation
    {
        std::uint32_t objectId;     // 参照元 object の永続 id
        std::size_t componentIndex; // 参照元 component の添字
        std::string fieldName;      // ObjectRef フィールド名
    };

    /// `targetId` を指す ObjectRef フィールドを全 object から集める。 削除前に何が参照しているかを調べる関数
    /// k_NoObjectId は未設定の印なので空を返す。 自分自身を指す参照も含める
    [[nodiscard]] std::vector<ObjectRefLocation> FindReferencesTo(const SceneData& scene, std::uint32_t targetId);

} // namespace NS::Object
