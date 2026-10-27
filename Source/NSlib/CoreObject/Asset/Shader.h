#pragma once

#include "NSlib/CoreObject/Object.h"

#include <memory>
#include <string>

namespace NS
{
    namespace Gfx
    {
        class D3DShader;
    }

    //! HLSL ファイル 1 つのシェーダー。道はここだけが持ち、読み直しはこの道で中の D3DShader を作り直す
    //! 頂点・ピクセルシェーダーが読めない時は、中の D3DShader が代わりのピンクになる。装置が無い時は空のシェーダーになる
    class Shader : public Object
    {
    public:
        explicit Shader(std::string path);
        ~Shader() noexcept override;

        [[nodiscard]] const std::string& Path() const noexcept;
        [[nodiscard]] Gfx::D3DShader* D3D() const noexcept;

        //! 道のファイルを読み直す。同じ D3DShader のまま中身を替えるので、指している物はそのまま使える
        //! @return 成功した場合 true。失敗しても前の中身を保つ
        [[nodiscard]] bool Reload();

        NS_REFLECT_NONE(Shader, Object)

    private:
        std::string m_path;
        std::unique_ptr<Gfx::D3DShader> m_shader;
    };
} // namespace NS
