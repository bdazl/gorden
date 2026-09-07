module;

#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module roboslop.assets.shader_compiler;

import roboslop.core.error;
import roboslop.core.file;
import roboslop.platform.process;

namespace roboslop {

export enum class ShaderCompileError : int {
    CompileFailed = 1,
    OutputMissing = 2,
};

export [[nodiscard]] auto toError(ShaderCompileError e, std::string ctx = {}) -> Error {
    switch (e) {
    case ShaderCompileError::CompileFailed:
        return {
            .category = "roboslop.assets.shader_compiler",
            .code = static_cast<int>(e),
            .message = "shaderc reported errors",
            .context = std::move(ctx)
        };
    case ShaderCompileError::OutputMissing:
        return {
            .category = "roboslop.assets.shader_compiler",
            .code = static_cast<int>(e),
            .message = "shaderc succeeded but produced no output file",
            .context = std::move(ctx)
        };
    }
    return {
        .category = "roboslop.assets.shader_compiler",
        .code = 0,
        .message = "unknown ShaderCompileError",
        .context = std::move(ctx)
    };
}

export enum class ShaderStage : int {
    Vertex,
    Fragment,
    Compute,
};

// One shaderc invocation. `profile` is the shaderc target profile for
// the active backend (spirv, 430, metal, s_5_0, ...); see
// shaderProfileFor in roboslop.render.shader. `outputFile` is where
// shaderc writes the .bin — shaderc has no in-memory output, so the
// caller picks a scratch location and the compiler reads it back.
export struct ShaderCompileRequest {
    std::filesystem::path source;
    std::filesystem::path varyingDef;
    ShaderStage stage = ShaderStage::Vertex;
    std::string profile;
    std::vector<std::filesystem::path> includeDirs;
    std::filesystem::path outputFile;
    bool debug = false;
};

// The --platform value shaderc expects for the OS this binary was built
// for. Mirrors the PLATFORM branch in bgfx.cmake's bgfxToolUtils.
export [[nodiscard]] constexpr auto shadercPlatform() noexcept -> std::string_view {
#ifdef _WIN32
    return "windows";
#elifdef __APPLE__
    return "osx";
#else
    return "linux";
#endif
}

export [[nodiscard]] constexpr auto shadercStageName(ShaderStage stage) noexcept
    -> std::string_view {
    switch (stage) {
    case ShaderStage::Vertex:
        return "vertex";
    case ShaderStage::Fragment:
        return "fragment";
    case ShaderStage::Compute:
        return "compute";
    }
    return "vertex";
}

// Builds the argv tail for shaderc. Kept pure and separate from the
// process spawn so the exact command line is unit-testable and matches
// what the build-time CMake path (_bgfx_shaderc_parse) generates.
export [[nodiscard]] auto shadercArguments(const ShaderCompileRequest& req)
    -> std::vector<std::string> {
    std::vector<std::string> args;
    args.emplace_back("-f");
    args.emplace_back(req.source.string());
    for (const auto& dir : req.includeDirs) {
        args.emplace_back("-i");
        args.emplace_back(dir.string());
    }
    args.emplace_back("-o");
    args.emplace_back(req.outputFile.string());
    args.emplace_back("--platform");
    args.emplace_back(shadercPlatform());
    args.emplace_back("--type");
    args.emplace_back(shadercStageName(req.stage));
    args.emplace_back("--varyingdef");
    args.emplace_back(req.varyingDef.string());
    args.emplace_back("--profile");
    args.emplace_back(req.profile);
    if (req.debug) {
        args.emplace_back("--debug");
    }
    return args;
}

// Thin wrapper over the shaderc executable that bgfx.cmake builds.
// compile() blocks for the duration of the child process (tens to a
// few hundred milliseconds), so call it from a worker thread. It does
// no bgfx work: the returned bytes are handed to makeProgram on the
// render thread by the caller.
export class ShaderCompiler {
  public:
    explicit ShaderCompiler(std::filesystem::path shadercExe) noexcept
        : exe(std::move(shadercExe)) {}

    [[nodiscard]] auto executable() const noexcept -> const std::filesystem::path& {
        return exe;
    }

    // On success: the compiled shader blob. On failure: CompileFailed
    // with shaderc's full diagnostics text in Error::context, so the
    // caller can show it verbatim.
    [[nodiscard]] auto compile(const ShaderCompileRequest& req) const -> Result<std::vector<char>> {
        std::error_code ec;
        std::filesystem::create_directories(req.outputFile.parent_path(), ec);

        const auto args = shadercArguments(req);
        auto run = runProcess(exe, std::span<const std::string>{args});
        if (!run) {
            return std::unexpected(run.error());
        }
        if (run->exitCode != 0) {
            std::string ctx = req.source.string() + "\n" + run->output;
            return std::unexpected(toError(ShaderCompileError::CompileFailed, std::move(ctx)));
        }
        auto bytes = readFileBytes(req.outputFile);
        if (!bytes) {
            return std::unexpected(
                toError(ShaderCompileError::OutputMissing, req.outputFile.string())
            );
        }
        return std::move(*bytes);
    }

  private:
    std::filesystem::path exe;
};

} // namespace roboslop
