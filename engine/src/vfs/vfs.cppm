module;

#include <algorithm>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

export module roboslop.vfs;

import roboslop.core.error;
import roboslop.core.file;

namespace roboslop {

export enum class VfsError : int {
    NotFound = 1,
    NotADirectory = 2,
    IsADirectory = 3,
    ReadOnly = 4,
    Exists = 5,
    InvalidPath = 6,
    HostIo = 7,
};

export [[nodiscard]] auto toError(VfsError e, std::string ctx = {}) -> Error {
    const auto make = [&](std::string_view msg) {
        return Error{
            .category = "roboslop.vfs",
            .code = static_cast<int>(e),
            .message = msg,
            .context = std::move(ctx)
        };
    };
    switch (e) {
    case VfsError::NotFound:
        return make("no such file or directory");
    case VfsError::NotADirectory:
        return make("not a directory");
    case VfsError::IsADirectory:
        return make("is a directory");
    case VfsError::ReadOnly:
        return make("read-only file");
    case VfsError::Exists:
        return make("file exists");
    case VfsError::InvalidPath:
        return make("invalid path");
    case VfsError::HostIo:
        return make("host filesystem error");
    }
    return make("unknown VfsError");
}

// A file whose contents are computed on read (e.g. a log, an
// observation). A missing `write` makes it read-only.
export struct LiveFile {
    std::function<std::string()> read;
    std::function<Result<void>(std::string_view)> write;
};

export struct DirEntry {
    std::string name;
    bool isDirectory = false;
    std::size_t size = 0;
};

export struct Stat {
    bool isDirectory = false;
    std::size_t size = 0;
};

// Turns `path` (absolute or relative to `cwd`) into a canonical
// absolute path: single slashes, no `.`/`..`, no trailing slash except
// for "/". `..` above the root stays at the root, like a real shell.
export [[nodiscard]] auto normalizePath(std::string_view cwd, std::string_view path)
    -> std::string {
    std::vector<std::string> parts;
    const auto push = [&](std::string_view p) {
        std::size_t i = 0;
        while (i < p.size()) {
            const auto j = p.find('/', i);
            const auto seg =
                p.substr(i, j == std::string_view::npos ? std::string_view::npos : j - i);
            if (seg == "..") {
                if (!parts.empty()) {
                    parts.pop_back();
                }
            } else if (!seg.empty() && seg != ".") {
                parts.emplace_back(seg);
            }
            if (j == std::string_view::npos) {
                break;
            }
            i = j + 1;
        }
    };
    if (path.empty() || path.front() != '/') {
        push(cwd);
    }
    push(path);
    std::string out;
    for (const auto& p : parts) {
        out += '/';
        out += p;
    }
    return out.empty() ? "/" : out;
}

export [[nodiscard]] auto splitPath(std::string_view absolute) -> std::vector<std::string> {
    std::vector<std::string> parts;
    std::size_t i = 0;
    while (i < absolute.size()) {
        const auto j = absolute.find('/', i);
        const auto seg =
            absolute.substr(i, j == std::string_view::npos ? std::string_view::npos : j - i);
        if (!seg.empty()) {
            parts.emplace_back(seg);
        }
        if (j == std::string_view::npos) {
            break;
        }
        i = j + 1;
    }
    return parts;
}

export [[nodiscard]] auto parentPath(std::string_view absolute) -> std::string {
    const auto slash = absolute.rfind('/');
    if (slash == std::string_view::npos || slash == 0) {
        return "/";
    }
    return std::string{absolute.substr(0, slash)};
}

export [[nodiscard]] auto baseName(std::string_view absolute) -> std::string {
    const auto slash = absolute.rfind('/');
    return std::string{slash == std::string_view::npos ? absolute : absolute.substr(slash + 1)};
}

// In-memory filesystem with three node kinds plus host mounts:
//   directory  — children by name
//   file       — a std::string, writable
//   live file  — read/write callbacks (see LiveFile)
//   host mount — a directory whose subtree is a real directory on
//                disk; everything under it persists
// Paths given to the methods may be relative to `cwd` (default "/").
// Single-threaded: use from the thread that owns it.
export class Vfs {
  public:
    Vfs() : root(std::make_unique<Node>(Kind::Directory)) {}

    Vfs(const Vfs&) = delete;
    auto operator=(const Vfs&) -> Vfs& = delete;
    Vfs(Vfs&&) noexcept = default;
    auto operator=(Vfs&&) noexcept -> Vfs& = default;
    ~Vfs() = default;

    [[nodiscard]] auto exists(std::string_view path, std::string_view cwd = "/") const -> bool {
        return stat(path, cwd).has_value();
    }

    [[nodiscard]] auto stat(std::string_view path, std::string_view cwd = "/") const
        -> Result<Stat> {
        const auto abs = normalizePath(cwd, path);
        auto r = resolve(abs);
        if (!r) {
            return std::unexpected(r.error());
        }
        if (r->node->kind == Kind::HostMount) {
            std::error_code ec;
            const auto hp = hostPath(*r);
            const auto st = std::filesystem::status(hp, ec);
            if (ec || !std::filesystem::exists(st)) {
                return std::unexpected(toError(VfsError::NotFound, abs));
            }
            if (std::filesystem::is_directory(st)) {
                return Stat{.isDirectory = true, .size = 0};
            }
            return Stat{
                .isDirectory = false,
                .size = static_cast<std::size_t>(std::filesystem::file_size(hp, ec))
            };
        }
        if (!r->rest.empty()) {
            return std::unexpected(toError(VfsError::NotFound, abs));
        }
        switch (r->node->kind) {
        case Kind::Directory:
            return Stat{.isDirectory = true, .size = 0};
        case Kind::File:
            return Stat{.isDirectory = false, .size = r->node->content.size()};
        case Kind::Live:
            return Stat{
                .isDirectory = false, .size = r->node->live.read ? r->node->live.read().size() : 0
            };
        case Kind::HostMount:
            break;
        }
        return std::unexpected(toError(VfsError::NotFound, abs));
    }

    [[nodiscard]] auto list(std::string_view path, std::string_view cwd = "/") const
        -> Result<std::vector<DirEntry>> {
        const auto abs = normalizePath(cwd, path);
        auto r = resolve(abs);
        if (!r) {
            return std::unexpected(r.error());
        }
        std::vector<DirEntry> out;
        if (r->node->kind == Kind::HostMount) {
            std::error_code ec;
            const auto hp = hostPath(*r);
            if (!std::filesystem::is_directory(hp, ec)) {
                return std::unexpected(toError(
                    std::filesystem::exists(hp, ec) ? VfsError::NotADirectory : VfsError::NotFound,
                    abs
                ));
            }
            for (const auto& entry : std::filesystem::directory_iterator(hp, ec)) {
                const bool dir = entry.is_directory(ec);
                out.push_back({
                    .name = entry.path().filename().string(),
                    .isDirectory = dir,
                    .size = dir ? 0 : static_cast<std::size_t>(entry.file_size(ec)),
                });
            }
            std::ranges::sort(out, {}, &DirEntry::name);
            return out;
        }
        if (!r->rest.empty()) {
            return std::unexpected(toError(VfsError::NotFound, abs));
        }
        if (r->node->kind != Kind::Directory) {
            return std::unexpected(toError(VfsError::NotADirectory, abs));
        }
        for (const auto& [name, child] : r->node->children) {
            const bool dir = child->kind == Kind::Directory || child->kind == Kind::HostMount;
            std::size_t size = 0;
            if (child->kind == Kind::File) {
                size = child->content.size();
            } else if (child->kind == Kind::Live && child->live.read) {
                size = child->live.read().size();
            }
            out.push_back({.name = name, .isDirectory = dir, .size = size});
        }
        return out;
    }

    [[nodiscard]] auto readFile(std::string_view path, std::string_view cwd = "/") const
        -> Result<std::string> {
        const auto abs = normalizePath(cwd, path);
        auto r = resolve(abs);
        if (!r) {
            return std::unexpected(r.error());
        }
        if (r->node->kind == Kind::HostMount) {
            const auto hp = hostPath(*r);
            std::error_code ec;
            if (std::filesystem::is_directory(hp, ec)) {
                return std::unexpected(toError(VfsError::IsADirectory, abs));
            }
            auto bytes = readFileBytes(hp);
            if (!bytes) {
                return std::unexpected(toError(VfsError::NotFound, abs));
            }
            return std::string{bytes->begin(), bytes->end()};
        }
        if (!r->rest.empty()) {
            return std::unexpected(toError(VfsError::NotFound, abs));
        }
        switch (r->node->kind) {
        case Kind::Directory:
            return std::unexpected(toError(VfsError::IsADirectory, abs));
        case Kind::File:
            return r->node->content;
        case Kind::Live:
            return r->node->live.read ? r->node->live.read() : std::string{};
        case Kind::HostMount:
            break;
        }
        return std::unexpected(toError(VfsError::NotFound, abs));
    }

    // Creates the file when missing; parent directory must exist.
    [[nodiscard]] auto writeFile(
        std::string_view path,
        std::string_view text,
        bool append = false,
        std::string_view cwd = "/"
    ) -> Result<void> {
        const auto abs = normalizePath(cwd, path);
        if (abs == "/") {
            return std::unexpected(toError(VfsError::IsADirectory, abs));
        }
        auto r = resolve(abs);
        if (r && r->node->kind == Kind::HostMount) {
            const auto hp = hostPath(*r);
            std::error_code ec;
            if (std::filesystem::is_directory(hp, ec)) {
                return std::unexpected(toError(VfsError::IsADirectory, abs));
            }
            std::ofstream out(hp, std::ios::binary | (append ? std::ios::app : std::ios::trunc));
            if (!out || !(out << text)) {
                return std::unexpected(toError(VfsError::HostIo, hp.string()));
            }
            return {};
        }
        if (r && r->rest.empty()) {
            Node& n = *r->node;
            switch (n.kind) {
            case Kind::Directory:
                return std::unexpected(toError(VfsError::IsADirectory, abs));
            case Kind::File:
                if (append) {
                    n.content += text;
                } else {
                    n.content = std::string{text};
                }
                return {};
            case Kind::Live:
                if (!n.live.write) {
                    return std::unexpected(toError(VfsError::ReadOnly, abs));
                }
                return n.live.write(text);
            case Kind::HostMount:
                break;
            }
        }
        // New file: parent must be an in-memory directory.
        auto parent = resolve(parentPath(abs));
        if (!parent || !parent->rest.empty() || parent->node->kind != Kind::Directory) {
            return std::unexpected(toError(VfsError::NotFound, parentPath(abs)));
        }
        auto file = std::make_unique<Node>(Kind::File);
        file->content = std::string{text};
        parent->node->children[baseName(abs)] = std::move(file);
        return {};
    }

    [[nodiscard]] auto
    mkdir(std::string_view path, bool parents = false, std::string_view cwd = "/") -> Result<void> {
        const auto abs = normalizePath(cwd, path);
        if (abs == "/") {
            return parents ? Result<void>{} : std::unexpected(toError(VfsError::Exists, abs));
        }
        auto r = resolve(abs);
        if (r && r->node->kind == Kind::HostMount) {
            const auto hp = hostPath(*r);
            std::error_code ec;
            if (std::filesystem::exists(hp, ec)) {
                return parents && std::filesystem::is_directory(hp, ec)
                           ? Result<void>{}
                           : std::unexpected(toError(VfsError::Exists, abs));
            }
            if (parents) {
                std::filesystem::create_directories(hp, ec);
            } else {
                std::filesystem::create_directory(hp, ec);
            }
            if (ec) {
                return std::unexpected(
                    toError(VfsError::HostIo, hp.string() + ": " + ec.message())
                );
            }
            return {};
        }
        if (r && r->rest.empty()) {
            return parents && r->node->kind == Kind::Directory
                       ? Result<void>{}
                       : std::unexpected(toError(VfsError::Exists, abs));
        }
        Node* dir = root.get();
        std::string walked;
        const auto parts = splitPath(abs);
        for (std::size_t i = 0; i < parts.size(); ++i) {
            walked += '/' + parts[i];
            auto it = dir->children.find(parts[i]);
            if (it == dir->children.end()) {
                if (!parents && i + 1 != parts.size()) {
                    return std::unexpected(toError(VfsError::NotFound, walked));
                }
                it = dir->children.emplace(parts[i], std::make_unique<Node>(Kind::Directory)).first;
            } else if (it->second->kind != Kind::Directory) {
                return std::unexpected(toError(VfsError::NotADirectory, walked));
            }
            dir = it->second.get();
        }
        return {};
    }

    [[nodiscard]] auto
    remove(std::string_view path, bool recursive = false, std::string_view cwd = "/")
        -> Result<void> {
        const auto abs = normalizePath(cwd, path);
        if (abs == "/") {
            return std::unexpected(toError(VfsError::InvalidPath, "cannot remove /"));
        }
        auto r = resolve(abs);
        if (!r) {
            return std::unexpected(r.error());
        }
        if (r->node->kind == Kind::HostMount && !r->rest.empty()) {
            const auto hp = hostPath(*r);
            std::error_code ec;
            if (!std::filesystem::exists(hp, ec)) {
                return std::unexpected(toError(VfsError::NotFound, abs));
            }
            if (std::filesystem::is_directory(hp, ec) && !recursive) {
                return std::unexpected(toError(VfsError::IsADirectory, abs));
            }
            std::filesystem::remove_all(hp, ec);
            if (ec) {
                return std::unexpected(
                    toError(VfsError::HostIo, hp.string() + ": " + ec.message())
                );
            }
            return {};
        }
        if (!r->rest.empty()) {
            return std::unexpected(toError(VfsError::NotFound, abs));
        }
        auto parent = resolve(parentPath(abs));
        if (!parent || !parent->rest.empty()) {
            return std::unexpected(toError(VfsError::NotFound, abs));
        }
        auto& children = parent->node->children;
        const auto it = children.find(baseName(abs));
        if (it == children.end()) {
            return std::unexpected(toError(VfsError::NotFound, abs));
        }
        const bool dirLike =
            it->second->kind == Kind::Directory || it->second->kind == Kind::HostMount;
        if (dirLike && !recursive && !it->second->children.empty()) {
            return std::unexpected(toError(VfsError::IsADirectory, abs + " (not empty)"));
        }
        if (it->second->kind == Kind::Directory && !recursive) {
            return std::unexpected(toError(VfsError::IsADirectory, abs));
        }
        children.erase(it);
        return {};
    }

    // Mounts a live file, creating parent directories. Replaces any
    // existing node at that path.
    [[nodiscard]] auto mountLive(std::string_view path, LiveFile file) -> Result<void> {
        const auto abs = normalizePath("/", path);
        if (auto r = mkdir(parentPath(abs), /*parents=*/true); !r) {
            return r;
        }
        auto parent = resolve(parentPath(abs));
        if (!parent || parent->node->kind != Kind::Directory) {
            return std::unexpected(toError(VfsError::NotADirectory, parentPath(abs)));
        }
        auto node = std::make_unique<Node>(Kind::Live);
        node->live = std::move(file);
        parent->node->children[baseName(abs)] = std::move(node);
        return {};
    }

    // Mounts a host directory (created if missing) at `path`, creating
    // parents. Everything under `path` is then read from and written to
    // disk under `hostRoot`.
    [[nodiscard]] auto mountHost(std::string_view path, const std::filesystem::path& hostRoot)
        -> Result<void> {
        const auto abs = normalizePath("/", path);
        if (abs == "/") {
            return std::unexpected(toError(VfsError::InvalidPath, "cannot mount at /"));
        }
        std::error_code ec;
        std::filesystem::create_directories(hostRoot, ec);
        if (ec) {
            return std::unexpected(
                toError(VfsError::HostIo, hostRoot.string() + ": " + ec.message())
            );
        }
        if (auto r = mkdir(parentPath(abs), /*parents=*/true); !r) {
            return r;
        }
        auto parent = resolve(parentPath(abs));
        if (!parent || parent->node->kind != Kind::Directory) {
            return std::unexpected(toError(VfsError::NotADirectory, parentPath(abs)));
        }
        auto node = std::make_unique<Node>(Kind::HostMount);
        node->hostRoot = hostRoot;
        parent->node->children[baseName(abs)] = std::move(node);
        return {};
    }

  private:
    enum class Kind : int { Directory, File, Live, HostMount };

    struct Node {
        explicit Node(Kind k) : kind(k) {}

        Kind kind;
        std::map<std::string, std::unique_ptr<Node>> children;
        std::string content;
        LiveFile live;
        std::filesystem::path hostRoot;
    };

    // Deepest in-memory node on the path plus the components left over.
    // `rest` is non-empty only when the walk stopped at a host mount
    // (the remainder is a path inside it) or at a missing child (an
    // error for every caller except writeFile/mkdir, which create).
    struct Resolved {
        Node* node = nullptr;
        std::vector<std::string> rest;
    };

    [[nodiscard]] auto resolve(std::string_view abs) const -> Result<Resolved> {
        Node* node = root.get();
        const auto parts = splitPath(abs);
        for (std::size_t i = 0; i < parts.size(); ++i) {
            if (node->kind == Kind::HostMount) {
                return Resolved{
                    .node = node, .rest = {parts.begin() + static_cast<long>(i), parts.end()}
                };
            }
            if (node->kind != Kind::Directory) {
                return std::unexpected(toError(VfsError::NotADirectory, std::string{abs}));
            }
            const auto it = node->children.find(parts[i]);
            if (it == node->children.end()) {
                return std::unexpected(toError(VfsError::NotFound, std::string{abs}));
            }
            node = it->second.get();
        }
        return Resolved{.node = node, .rest = {}};
    }

    [[nodiscard]] static auto hostPath(const Resolved& r) -> std::filesystem::path {
        auto p = r.node->hostRoot;
        for (const auto& part : r.rest) {
            p /= part;
        }
        return p;
    }

    std::unique_ptr<Node> root;
};

} // namespace roboslop
