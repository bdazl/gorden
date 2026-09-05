module;

#include <cerrno>
#include <cstddef>
#include <cstring>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <utility>
#include <vector>

#if !defined(_WIN32)
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

export module roboslop.platform.process;

import roboslop.core.error;

namespace roboslop {

export enum class ProcessError : int {
    Unsupported = 1,
    SpawnFailed = 2,
    ReadFailed = 3,
    WaitFailed = 4,
};

export [[nodiscard]] auto toError(ProcessError e, std::string ctx = {}) -> Error {
    switch (e) {
    case ProcessError::Unsupported:
        return {
            .category = "roboslop.platform.process",
            .code = static_cast<int>(e),
            .message = "child processes are not supported on this platform",
            .context = std::move(ctx)
        };
    case ProcessError::SpawnFailed:
        return {
            .category = "roboslop.platform.process",
            .code = static_cast<int>(e),
            .message = "failed to spawn child process",
            .context = std::move(ctx)
        };
    case ProcessError::ReadFailed:
        return {
            .category = "roboslop.platform.process",
            .code = static_cast<int>(e),
            .message = "failed to read child process output",
            .context = std::move(ctx)
        };
    case ProcessError::WaitFailed:
        return {
            .category = "roboslop.platform.process",
            .code = static_cast<int>(e),
            .message = "failed to wait for child process",
            .context = std::move(ctx)
        };
    }
    return {
        .category = "roboslop.platform.process",
        .code = 0,
        .message = "unknown ProcessError",
        .context = std::move(ctx)
    };
}

// Outcome of a finished child. `output` is stdout and stderr merged in
// arrival order — callers that shell out to a tool (shaderc, ...) want
// the diagnostics as one blob, not two streams to reconcile.
export struct ProcessResult {
    int exitCode = 0;
    std::string output;
};

// Runs `exe` with `args` (argv[1..]) to completion and captures its
// output. Blocking: intended to be called from a worker thread, never
// from the frame loop. A non-zero exit code is not an Error — it is
// reported in ProcessResult so the caller can decide what it means.
//
// POSIX only for now. Other platforms return ProcessError::Unsupported
// rather than a build break, so the module is importable everywhere and
// the gap shows up as a runtime Result.
export [[nodiscard]] auto
runProcess(const std::filesystem::path& exe, std::span<const std::string> args)
    -> Result<ProcessResult> {
#if defined(_WIN32)
    (void)args;
    return std::unexpected(toError(ProcessError::Unsupported, exe.string()));
#else
    int fds[2] = {-1, -1};
    if (::pipe(fds) != 0) {
        return std::unexpected(toError(ProcessError::SpawnFailed, std::strerror(errno)));
    }

    const std::string exeStr = exe.string();
    std::vector<std::string> argvStorage;
    argvStorage.reserve(args.size() + 1);
    argvStorage.push_back(exeStr);
    for (const auto& a : args) {
        argvStorage.push_back(a);
    }
    std::vector<char*> argv;
    argv.reserve(argvStorage.size() + 1);
    for (auto& s : argvStorage) {
        argv.push_back(s.data());
    }
    argv.push_back(nullptr);

    const pid_t pid = ::fork();
    if (pid < 0) {
        ::close(fds[0]);
        ::close(fds[1]);
        return std::unexpected(toError(ProcessError::SpawnFailed, std::strerror(errno)));
    }

    if (pid == 0) {
        // Child. Only async-signal-safe calls from here to exec.
        ::close(fds[0]);
        ::dup2(fds[1], STDOUT_FILENO);
        ::dup2(fds[1], STDERR_FILENO);
        ::close(fds[1]);
        ::execv(exeStr.c_str(), argv.data());
        ::_exit(127);
    }

    ::close(fds[1]);
    ProcessResult result;
    char buf[4096];
    for (;;) {
        const ssize_t n = ::read(fds[0], buf, sizeof(buf));
        if (n > 0) {
            result.output.append(buf, static_cast<std::size_t>(n));
            continue;
        }
        if (n == 0) {
            break;
        }
        if (errno == EINTR) {
            continue;
        }
        ::close(fds[0]);
        ::waitpid(pid, nullptr, 0);
        return std::unexpected(toError(ProcessError::ReadFailed, std::strerror(errno)));
    }
    ::close(fds[0]);

    int status = 0;
    if (::waitpid(pid, &status, 0) < 0) {
        return std::unexpected(toError(ProcessError::WaitFailed, std::strerror(errno)));
    }
    if (WIFEXITED(status)) {
        result.exitCode = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        result.exitCode = 128 + WTERMSIG(status);
    } else {
        result.exitCode = -1;
    }
    return result;
#endif
}

} // namespace roboslop
