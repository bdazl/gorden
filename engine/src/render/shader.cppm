module;

#include <bgfx/bgfx.h>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module roboslop.render.shader;

import roboslop.core.error;
import roboslop.core.file;

namespace roboslop {

export enum class ShaderError : int {
    UnsupportedRenderer = 1,
    ShaderCreateFailed = 2,
    ProgramCreateFailed = 3,
};

export [[nodiscard]] auto toError(ShaderError e, std::string ctx = {}) -> Error {
    switch (e) {
    case ShaderError::UnsupportedRenderer:
        return {
            .category = "roboslop.render.shader",
            .code = static_cast<int>(e),
            .message = "no shader directory for active bgfx renderer",
            .context = std::move(ctx)
        };
    case ShaderError::ShaderCreateFailed:
        return {
            .category = "roboslop.render.shader",
            .code = static_cast<int>(e),
            .message = "bgfx::createShader returned invalid handle",
            .context = std::move(ctx)
        };
    case ShaderError::ProgramCreateFailed:
        return {
            .category = "roboslop.render.shader",
            .code = static_cast<int>(e),
            .message = "bgfx::createProgram returned invalid handle",
            .context = std::move(ctx)
        };
    }
    return {
        .category = "roboslop.render.shader",
        .code = 0,
        .message = "unknown ShaderError",
        .context = std::move(ctx)
    };
}

namespace {

[[nodiscard]] auto backendDir(bgfx::RendererType::Enum type) noexcept -> std::string_view {
    switch (type) {
    case bgfx::RendererType::Vulkan:
        return "spirv";
    case bgfx::RendererType::OpenGL:
        return "glsl";
    case bgfx::RendererType::OpenGLES:
        return "essl";
    case bgfx::RendererType::Direct3D11:
    case bgfx::RendererType::Direct3D12:
        return "dx11";
    case bgfx::RendererType::Metal:
        return "metal";
    default:
        return {};
    }
}

} // namespace

// The shaderc --profile that produces binaries loadable by the active
// bgfx renderer. Mirrors the desktop profile list bgfxToolUtils uses at
// build time (spirv + 430 on Linux, metal on macOS, s_5_0 on Windows),
// so a runtime compile lands in the same format the build-time path
// produces. Empty for renderers without a known profile.
export [[nodiscard]] auto shaderProfileFor(bgfx::RendererType::Enum type) noexcept
    -> std::string_view {
    switch (type) {
    case bgfx::RendererType::Vulkan:
        return "spirv";
    case bgfx::RendererType::OpenGL:
        return "430";
    case bgfx::RendererType::OpenGLES:
        return "300_es";
    case bgfx::RendererType::Direct3D11:
    case bgfx::RendererType::Direct3D12:
        return "s_5_0";
    case bgfx::RendererType::Metal:
        return "metal";
    default:
        return {};
    }
}

// RAII handle to a bgfx::ProgramHandle. createProgram destroys its shader
// inputs internally (we pass destroyShaders=true), so Program only manages
// the program handle. Moving transfers ownership; destructor destroys.
export class Program {
  public:
    Program() = default;

    Program(const Program&) = delete;
    auto operator=(const Program&) -> Program& = delete;

    Program(Program&& other) noexcept : handle(std::exchange(other.handle, kInvalid)) {}

    auto operator=(Program&& other) noexcept -> Program& {
        if (this != &other) {
            destroy();
            handle = std::exchange(other.handle, kInvalid);
        }
        return *this;
    }

    ~Program() {
        destroy();
    }

    [[nodiscard]] auto bgfxHandle() const noexcept -> bgfx::ProgramHandle {
        return handle;
    }

    [[nodiscard]] auto valid() const noexcept -> bool {
        return bgfx::isValid(handle);
    }

  private:
    friend auto loadProgram(const std::filesystem::path&, std::string_view, std::string_view)
        -> Result<Program>;

    explicit Program(bgfx::ProgramHandle h) noexcept : handle(h) {}

    auto destroy() noexcept -> void {
        if (bgfx::isValid(handle)) {
            bgfx::destroy(handle);
            handle = kInvalid;
        }
    }

    static constexpr bgfx::ProgramHandle kInvalid{bgfx::kInvalidHandle};
    bgfx::ProgramHandle handle{bgfx::kInvalidHandle};
};

// Load a compiled bgfx shader (.bin) and create the bgfx shader object.
// Caller owns the returned handle until passed into createProgram.
export [[nodiscard]] auto loadShader(const std::filesystem::path& path)
    -> Result<bgfx::ShaderHandle> {
    auto bytes = readFileBytes(path);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }

    const bgfx::Memory* mem = bgfx::copy(bytes->data(), static_cast<std::uint32_t>(bytes->size()));
    const auto handle = bgfx::createShader(mem);
    if (!bgfx::isValid(handle)) {
        return std::unexpected(toError(ShaderError::ShaderCreateFailed, path.string()));
    }
    return handle;
}

// Load a vertex+fragment shader pair from assetRoot/shaders/<backend>/<name>.bin
// and link them into a Program. Shader files are produced by
// the app's CMakeLists.txt (e.g. apps/gorden) via roboslop_compile_shader.
export [[nodiscard]] auto loadProgram(
    const std::filesystem::path& assetRoot, std::string_view vsName, std::string_view fsName
) -> Result<Program> {
    const auto backend = backendDir(bgfx::getRendererType());
    if (backend.empty()) {
        return std::unexpected(toError(ShaderError::UnsupportedRenderer));
    }

    // bgfxToolUtils keeps the .sc input extension on its output:
    // vs_basic.sc → vs_basic.sc.bin. The vs/fs name passed in is the
    // basename of the .sc source.
    const auto shaderDir = assetRoot / "shaders" / std::string{backend};
    const auto vsPath = shaderDir / (std::string{vsName} + ".sc.bin");
    const auto fsPath = shaderDir / (std::string{fsName} + ".sc.bin");

    auto vsh = loadShader(vsPath);
    if (!vsh) {
        return std::unexpected(vsh.error());
    }
    auto fsh = loadShader(fsPath);
    if (!fsh) {
        bgfx::destroy(*vsh);
        return std::unexpected(fsh.error());
    }

    const auto program = bgfx::createProgram(*vsh, *fsh, /*destroyShaders=*/true);
    if (!bgfx::isValid(program)) {
        return std::unexpected(
            toError(ShaderError::ProgramCreateFailed, vsPath.string() + " + " + fsPath.string())
        );
    }
    return Program{program};
}

} // namespace roboslop
