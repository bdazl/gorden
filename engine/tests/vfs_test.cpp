import roboslop.core.error;
import roboslop.vfs;

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

TEST_CASE("normalizePath canonicalises absolute and relative paths", "[vfs]") {
    REQUIRE(roboslop::normalizePath("/", "/") == "/");
    REQUIRE(roboslop::normalizePath("/", "") == "/");
    REQUIRE(roboslop::normalizePath("/", "/a//b/./c/") == "/a/b/c");
    REQUIRE(roboslop::normalizePath("/home/x", "docs") == "/home/x/docs");
    REQUIRE(roboslop::normalizePath("/home/x", "../y") == "/home/y");
    REQUIRE(roboslop::normalizePath("/home/x", "/../../etc") == "/etc");
    REQUIRE(roboslop::normalizePath("/a/b", "..") == "/a");
    REQUIRE(roboslop::normalizePath("/a", "../../..") == "/");
    REQUIRE(roboslop::parentPath("/a/b/c") == "/a/b");
    REQUIRE(roboslop::parentPath("/a") == "/");
    REQUIRE(roboslop::baseName("/a/b/c") == "c");
}

TEST_CASE("mkdir, writeFile, readFile, list on the in-memory tree", "[vfs]") {
    roboslop::Vfs fs;
    REQUIRE(fs.mkdir("/var/log", true).has_value());
    REQUIRE_FALSE(fs.mkdir("/nope/deep").has_value()); // parent missing, no -p
    REQUIRE_FALSE(fs.mkdir("/var/log").has_value());   // exists, no -p
    REQUIRE(fs.mkdir("/var/log", true).has_value());   // exists, -p ok

    REQUIRE(fs.writeFile("/var/log/a.txt", "hello").has_value());
    REQUIRE(fs.writeFile("/var/log/a.txt", " world", /*append=*/true).has_value());
    REQUIRE(fs.readFile("/var/log/a.txt") == "hello world");
    REQUIRE_FALSE(fs.writeFile("/missing/x", "y").has_value());
    REQUIRE_FALSE(fs.readFile("/var/log").has_value()); // directory

    REQUIRE(fs.writeFile("b.txt", "B", false, "/var/log").has_value()); // relative to cwd
    const auto entries = fs.list("/var/log");
    REQUIRE(entries.has_value());
    REQUIRE(entries->size() == 2);
    REQUIRE((*entries)[0].name == "a.txt");
    REQUIRE((*entries)[0].size == 11);
    REQUIRE_FALSE((*entries)[0].isDirectory);

    const auto root = fs.list("/");
    REQUIRE(root.has_value());
    REQUIRE(root->size() == 1);
    REQUIRE((*root)[0].isDirectory);

    const auto st = fs.stat("/var/log/a.txt");
    REQUIRE(st.has_value());
    REQUIRE(st->size == 11);
    REQUIRE(fs.stat("/var")->isDirectory);
    REQUIRE_FALSE(fs.exists("/var/nope"));
}

TEST_CASE("remove respects directories and recursion", "[vfs]") {
    roboslop::Vfs fs;
    REQUIRE(fs.mkdir("/d/e", true).has_value());
    REQUIRE(fs.writeFile("/d/e/f", "x").has_value());
    REQUIRE_FALSE(fs.remove("/d").has_value());
    REQUIRE(fs.remove("/d/e/f").has_value());
    REQUIRE_FALSE(fs.exists("/d/e/f"));
    REQUIRE(fs.remove("/d", true).has_value());
    REQUIRE_FALSE(fs.exists("/d"));
    REQUIRE_FALSE(fs.remove("/").has_value());
}

TEST_CASE("live files read through callbacks and honour read-only", "[vfs]") {
    roboslop::Vfs fs;
    int reads = 0;
    std::string sink;
    REQUIRE(fs.mountLive(
                  "/proc/counter",
                  roboslop::LiveFile{
                      .read = [&] { return std::to_string(++reads); },
                      .write = {},
                  }
    )
                .has_value());
    REQUIRE(fs.mountLive(
                  "/etc/name",
                  roboslop::LiveFile{
                      .read = [&] { return sink; },
                      .write = [&](std::string_view s) -> roboslop::Result<void> {
                          sink = std::string{s};
                          return {};
                      },
                  }
    )
                .has_value());

    REQUIRE(fs.readFile("/proc/counter") == "1");
    REQUIRE(fs.readFile("/proc/counter") == "2");
    const auto ro = fs.writeFile("/proc/counter", "9");
    REQUIRE_FALSE(ro.has_value());
    REQUIRE(ro.error().code == static_cast<int>(roboslop::VfsError::ReadOnly));

    REQUIRE(fs.writeFile("/etc/name", "Anna").has_value());
    REQUIRE(fs.readFile("/etc/name") == "Anna");
    REQUIRE(fs.list("/proc")->size() == 1);
}

TEST_CASE("host mounts read and write real files and cannot escape", "[vfs]") {
    const auto host = std::filesystem::temp_directory_path() / "roboslop-vfs-host";
    std::filesystem::remove_all(host);

    roboslop::Vfs fs;
    REQUIRE(fs.mountHost("/persist", host).has_value());
    REQUIRE(std::filesystem::is_directory(host));

    REQUIRE(fs.writeFile("/persist/note.txt", "keep").has_value());
    REQUIRE(std::filesystem::exists(host / "note.txt"));
    REQUIRE(fs.readFile("/persist/note.txt") == "keep");
    REQUIRE(fs.writeFile("/persist/note.txt", "!", true).has_value());
    REQUIRE(fs.readFile("/persist/note.txt") == "keep!");

    REQUIRE(fs.mkdir("/persist/sub/deeper", true).has_value());
    REQUIRE(std::filesystem::is_directory(host / "sub" / "deeper"));
    const auto entries = fs.list("/persist");
    REQUIRE(entries.has_value());
    REQUIRE(entries->size() == 2);
    REQUIRE(fs.stat("/persist/sub")->isDirectory);
    REQUIRE(fs.stat("/persist/note.txt")->size == 5);

    // `..` is resolved before the mount is consulted, so this lands
    // back inside the mount rather than in the host's parent.
    REQUIRE(fs.writeFile("/persist/../persist/ok.txt", "x").has_value());
    REQUIRE(std::filesystem::exists(host / "ok.txt"));
    REQUIRE_FALSE(fs.readFile("/persist/../secret").has_value());

    REQUIRE(fs.remove("/persist/sub", true).has_value());
    REQUIRE_FALSE(std::filesystem::exists(host / "sub"));
    REQUIRE_FALSE(fs.readFile("/persist/missing").has_value());

    // Root listing shows the mount as a directory.
    const auto root = fs.list("/");
    REQUIRE(root.has_value());
    REQUIRE((*root)[0].name == "persist");
    REQUIRE((*root)[0].isDirectory);

    std::filesystem::remove_all(host);
}
