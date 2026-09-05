import roboslop.core.error;
import roboslop.shell;
import roboslop.vfs;

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace {

auto words(const std::vector<roboslop::Token>& tokens) -> std::vector<std::string> {
    std::vector<std::string> out;
    for (const auto& t : tokens) {
        out.push_back(t.isOperator ? "<" + t.text + ">" : t.text);
    }
    return out;
}

auto fixture() -> roboslop::Vfs {
    roboslop::Vfs fs;
    REQUIRE(fs.mkdir("/home/anna", true).has_value());
    REQUIRE(fs.mkdir("/var/log", true).has_value());
    REQUIRE(fs.writeFile("/var/log/app.log", "one\ntwo\nthree\nfour\n").has_value());
    REQUIRE(fs.writeFile("/home/anna/hello.txt", "Hello World\n").has_value());
    return fs;
}

} // namespace

TEST_CASE("tokenize handles quotes, escapes, operators, and expansion", "[shell]") {
    const auto env = [](std::string_view name) -> std::string {
        if (name == "HOME") {
            return "/home/anna";
        }
        if (name == "X") {
            return "42";
        }
        return {};
    };
    auto t = roboslop::tokenize(
        R"(echo 'a b' "c $X" d\ e $X ~/x > out.txt | grep -i "q" && ls; pwd)", env
    );
    REQUIRE(t.has_value());
    REQUIRE(
        words(*t) == std::vector<std::string>{
                         "echo",
                         "a b",
                         "c 42",
                         "d e",
                         "42",
                         "/home/anna/x",
                         "<>>",
                         "out.txt",
                         "<|>",
                         "grep",
                         "-i",
                         "q",
                         "<&&>",
                         "ls",
                         "<;>",
                         "pwd"
                     }
    );
    REQUIRE_FALSE(roboslop::tokenize("echo 'open").has_value());
    REQUIRE(
        words(*roboslop::tokenize("echo a>>b")) ==
        std::vector<std::string>{"echo", "a", "<>>>", "b"}
    );
    REQUIRE(
        words(*roboslop::tokenize("echo ${X}y", env)) == std::vector<std::string>{"echo", "42y"}
    );
}

TEST_CASE("basic navigation and listing", "[shell]") {
    auto fs = fixture();
    roboslop::Shell sh(fs, {.user = "anna", .host = "gorden", .home = "/home/anna"});

    REQUIRE(sh.cwd() == "/home/anna");
    REQUIRE(sh.prompt() == "anna@gorden:~$ ");
    REQUIRE(sh.execute("pwd").output == "/home/anna\n");

    REQUIRE(sh.execute("cd /var/log").status == 0);
    REQUIRE(sh.execute("pwd").output == "/var/log\n");
    REQUIRE(sh.prompt() == "anna@gorden:/var/log$ ");
    REQUIRE(sh.execute("cd").status == 0);
    REQUIRE(sh.cwd() == "/home/anna");
    REQUIRE(sh.execute("cd /nope").status == 1);
    REQUIRE(sh.execute("cd hello.txt").status == 1);

    REQUIRE(sh.execute("ls /").output == "home/\nvar/\n");
    REQUIRE(sh.execute("ls -l /var/log").output.contains("app.log"));
    REQUIRE(sh.execute("ls /missing").status == 1);
}

TEST_CASE("cat, echo, redirection, append, and pipes", "[shell]") {
    auto fs = fixture();
    roboslop::Shell sh(fs, {.user = "anna", .host = "gorden", .home = "/home/anna"});

    REQUIRE(sh.execute("cat hello.txt").output == "Hello World\n");
    REQUIRE(sh.execute("echo hi there > note.txt").status == 0);
    REQUIRE(fs.readFile("/home/anna/note.txt") == "hi there\n");
    REQUIRE(sh.execute("echo again >> note.txt").status == 0);
    REQUIRE(fs.readFile("/home/anna/note.txt") == "hi there\nagain\n");
    REQUIRE(sh.execute("echo x >").status != 0);

    REQUIRE(sh.execute("cat /var/log/app.log | grep t").output == "two\nthree\n");
    REQUIRE(sh.execute("cat /var/log/app.log | grep -i T | wc").output == "2 2 10\n");
    REQUIRE(sh.execute("grep zzz /var/log/app.log").status == 1);
    REQUIRE(sh.execute("cat /var/log/app.log | head -n 2").output == "one\ntwo\n");
    REQUIRE(sh.execute("tail -n 1 /var/log/app.log").output == "four\n");
}

TEST_CASE("sequencing with ; and &&", "[shell]") {
    auto fs = fixture();
    roboslop::Shell sh(fs, {.user = "anna", .host = "gorden", .home = "/home/anna"});
    REQUIRE(sh.execute("echo a; echo b").output == "a\nb\n");
    REQUIRE(sh.execute("cd /nope && echo never").output.contains("cd:"));
    REQUIRE_FALSE(sh.execute("cd /nope && echo never").output.contains("never"));
    REQUIRE(sh.execute("mkdir -p /tmp/x && echo ok > /tmp/x/f && cat /tmp/x/f").output == "ok\n");
}

TEST_CASE("mkdir, touch, rm, tree", "[shell]") {
    auto fs = fixture();
    roboslop::Shell sh(fs, {.user = "anna", .host = "gorden", .home = "/home/anna"});
    REQUIRE(sh.execute("mkdir a").status == 0);
    REQUIRE(sh.execute("mkdir a").status == 1);
    REQUIRE(sh.execute("mkdir -p a/b/c").status == 0);
    REQUIRE(sh.execute("touch a/b/c/f").status == 0);
    REQUIRE(fs.exists("/home/anna/a/b/c/f"));
    const auto tree = sh.execute("tree a").output;
    REQUIRE(tree.contains("b/"));
    REQUIRE(tree.contains("f"));
    REQUIRE(sh.execute("rm a").status == 1);
    REQUIRE(sh.execute("rm -r a").status == 0);
    REQUIRE_FALSE(fs.exists("/home/anna/a"));
}

TEST_CASE("tail -f reports the path to follow", "[shell]") {
    auto fs = fixture();
    roboslop::Shell sh(fs, {.user = "anna", .host = "gorden", .home = "/home/anna"});
    const auto r = sh.execute("tail -n 2 -f /var/log/app.log");
    REQUIRE(r.status == 0);
    REQUIRE(r.output == "three\nfour\n");
    REQUIRE(r.followPath.has_value());
    REQUIRE(*r.followPath == "/var/log/app.log");
    REQUIRE_FALSE(sh.execute("tail /var/log/app.log").followPath.has_value());
}

TEST_CASE("unknown commands, env, export, history, clear, help", "[shell]") {
    auto fs = fixture();
    roboslop::Shell sh(fs, {.user = "anna", .host = "gorden", .home = "/home/anna"});
    REQUIRE(sh.execute("frobnicate").status == 127);
    REQUIRE(sh.execute("export GREETING=hej").status == 0);
    REQUIRE(sh.execute("echo $GREETING $USER $PWD").output == "hej anna /home/anna\n");
    REQUIRE(sh.execute("env").output.contains("GREETING=hej"));
    REQUIRE(sh.execute("clear").clear);
    REQUIRE(sh.execute("help").output.contains("tail"));
    const auto h = sh.execute("history").output;
    REQUIRE(h.contains("frobnicate"));
    REQUIRE(h.contains("export GREETING=hej"));
}

TEST_CASE("registered app commands and completion", "[shell]") {
    auto fs = fixture();
    roboslop::Shell sh(fs, {.user = "anna", .host = "gorden", .home = "/home/anna"});
    sh.registerCommand("wave", "wave at the player", [](roboslop::CommandContext& c) {
        return roboslop::ShellResult{
            .output = "waving " + (c.args.size() > 1 ? c.args[1] : "") + "\n"
        };
    });
    REQUIRE(sh.execute("wave hi").output == "waving hi\n");

    REQUIRE(sh.complete("wa") == std::vector<std::string>{"wave"});
    REQUIRE(sh.complete("cat /var/l") == std::vector<std::string>{"/var/log/"});
    REQUIRE(sh.complete("cat /var/log/a") == std::vector<std::string>{"/var/log/app.log"});
    REQUIRE(sh.complete("cat hel") == std::vector<std::string>{"hello.txt"});
    REQUIRE(sh.complete("cat /zz").empty());
}
