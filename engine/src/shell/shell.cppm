module;

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <ctime>
#include <expected>
#include <format>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module roboslop.shell;

import roboslop.core.error;
import roboslop.vfs;

namespace roboslop {

export struct ShellConfig {
    std::string user = "user";
    std::string host = "roboslop";
    std::string home = "/";
};

// What one execute() produced. `followPath` is set by `tail -f`: the
// terminal keeps appending new bytes of that file until interrupted.
export struct ShellResult {
    std::string output{};
    int status = 0;
    std::optional<std::string> followPath{};
    bool clear = false;
};

export enum class ShellError : int {
    UnterminatedQuote = 1,
    BadRedirect = 2,
};

export [[nodiscard]] auto toError(ShellError e, std::string ctx = {}) -> Error {
    switch (e) {
    case ShellError::UnterminatedQuote:
        return {
            .category = "roboslop.shell",
            .code = static_cast<int>(e),
            .message = "unterminated quote",
            .context = std::move(ctx)
        };
    case ShellError::BadRedirect:
        return {
            .category = "roboslop.shell",
            .code = static_cast<int>(e),
            .message = "redirection needs a file name",
            .context = std::move(ctx)
        };
    }
    return {
        .category = "roboslop.shell",
        .code = 0,
        .message = "unknown ShellError",
        .context = std::move(ctx)
    };
}

// A token is a word or one of the operators | > >> ; &&. Operators are
// only recognised unquoted; `isOperator` tells them apart from a
// quoted word with the same spelling.
export struct Token {
    std::string text{};
    bool isOperator = false;
};

export using EnvLookup = std::function<std::string(std::string_view)>;

// POSIX-ish word splitting: whitespace separates words; single quotes
// are literal; double quotes allow \" \\ and $VAR; backslash escapes
// the next character; `$NAME`, `${NAME}` and a leading `~` expand
// through `env` (empty when absent). Exported for tests and for the
// terminal's completion.
export [[nodiscard]] auto tokenize(std::string_view line, const EnvLookup& env = {})
    -> Result<std::vector<Token>> {
    std::vector<Token> out;
    std::string cur;
    bool inWord = false;
    const auto lookup = [&](std::string_view name) -> std::string {
        return env ? env(name) : std::string{};
    };
    const auto flush = [&] {
        if (inWord) {
            out.push_back({.text = cur, .isOperator = false});
            cur.clear();
            inWord = false;
        }
    };
    const auto expandVar = [&](std::size_t& i) {
        // i points at '$'
        std::size_t j = i + 1;
        if (j < line.size() && line[j] == '{') {
            const auto close = line.find('}', j);
            if (close == std::string_view::npos) {
                cur += '$';
                return;
            }
            cur += lookup(line.substr(j + 1, close - j - 1));
            i = close;
            return;
        }
        while (j < line.size() &&
               (std::isalnum(static_cast<unsigned char>(line[j])) != 0 || line[j] == '_')) {
            ++j;
        }
        if (j == i + 1) {
            cur += '$';
            return;
        }
        cur += lookup(line.substr(i + 1, j - i - 1));
        i = j - 1;
    };

    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (c == ' ' || c == '\t') {
            flush();
            continue;
        }
        if (c == '\'') {
            const auto close = line.find('\'', i + 1);
            if (close == std::string_view::npos) {
                return std::unexpected(toError(ShellError::UnterminatedQuote, std::string{line}));
            }
            cur += line.substr(i + 1, close - i - 1);
            inWord = true;
            i = close;
            continue;
        }
        if (c == '"') {
            std::size_t j = i + 1;
            bool closed = false;
            while (j < line.size()) {
                if (line[j] == '\\' && j + 1 < line.size()) {
                    cur += line[j + 1];
                    j += 2;
                    continue;
                }
                if (line[j] == '"') {
                    closed = true;
                    break;
                }
                if (line[j] == '$') {
                    expandVar(j);
                    ++j;
                    continue;
                }
                cur += line[j++];
            }
            if (!closed) {
                return std::unexpected(toError(ShellError::UnterminatedQuote, std::string{line}));
            }
            inWord = true;
            i = j;
            continue;
        }
        if (c == '\\' && i + 1 < line.size()) {
            cur += line[i + 1];
            inWord = true;
            ++i;
            continue;
        }
        if (c == '|' || c == ';' || c == '>' || c == '&') {
            flush();
            std::string op{c};
            if (c == '>' && i + 1 < line.size() && line[i + 1] == '>') {
                op = ">>";
                ++i;
            } else if (c == '&' && i + 1 < line.size() && line[i + 1] == '&') {
                op = "&&";
                ++i;
            } else if (c == '&') {
                cur += c; // a lone & is just a character here
                inWord = true;
                continue;
            }
            out.push_back({.text = op, .isOperator = true});
            continue;
        }
        if (c == '$') {
            expandVar(i);
            inWord = true;
            continue;
        }
        if (c == '~' && !inWord && (i + 1 == line.size() || line[i + 1] == '/')) {
            cur += lookup("HOME");
            inWord = true;
            continue;
        }
        cur += c;
        inWord = true;
    }
    flush();
    return out;
}

export class Shell;

// Passed to every command. `args[0]` is the command name. `stdinText`
// is the previous pipeline stage's output (empty otherwise).
export struct CommandContext {
    Shell& shell;
    Vfs& vfs;
    std::vector<std::string> args{};
    std::string stdinText{};
};

export using CommandFn = std::function<ShellResult(CommandContext&)>;

namespace detail {

[[nodiscard]] auto errorResult(std::string_view cmd, const Error& e) -> ShellResult {
    std::string text = std::format("{}: {}", cmd, e.message);
    if (!e.context.empty()) {
        text += std::format(": {}", e.context);
    }
    return {.output = text + "\n", .status = 1};
}

[[nodiscard]] auto usage(std::string_view text) -> ShellResult {
    return {.output = std::format("usage: {}\n", text), .status = 2};
}

[[nodiscard]] auto splitLines(std::string_view text) -> std::vector<std::string> {
    std::vector<std::string> lines;
    std::size_t i = 0;
    while (i < text.size()) {
        const auto j = text.find('\n', i);
        lines.emplace_back(
            text.substr(i, j == std::string_view::npos ? std::string_view::npos : j - i)
        );
        if (j == std::string_view::npos) {
            break;
        }
        i = j + 1;
    }
    return lines;
}

// Flags and positional args, POSIX-short style (-l, -la, -n 5).
struct Parsed {
    std::vector<std::string> positional;
    std::map<char, std::string> flags; // value for flags that take one, "" otherwise
};

[[nodiscard]] auto parseArgs(const std::vector<std::string>& args, std::string_view withValue)
    -> Parsed {
    Parsed p;
    for (std::size_t i = 1; i < args.size(); ++i) {
        const auto& a = args[i];
        if (a.size() > 1 && a[0] == '-' && a != "--" &&
            (std::isdigit(static_cast<unsigned char>(a[1])) == 0)) {
            for (std::size_t k = 1; k < a.size(); ++k) {
                const char f = a[k];
                if (withValue.contains(f)) {
                    if (k + 1 < a.size()) {
                        p.flags[f] = a.substr(k + 1);
                    } else if (i + 1 < args.size()) {
                        p.flags[f] = args[++i];
                    } else {
                        p.flags[f] = "";
                    }
                    break;
                }
                p.flags[f] = "";
            }
        } else {
            p.positional.push_back(a);
        }
    }
    return p;
}

} // namespace detail

// A small zsh-flavoured shell over a Vfs: builtins, `|` pipes between
// them, `>` / `>>` redirection into the VFS, `;` and `&&` sequencing,
// history, and path/command completion. No job control, no scripting.
// Apps add commands with registerCommand().
export class Shell {
  public:
    Shell(Vfs& vfs, ShellConfig config) : fs(&vfs), cfg(std::move(config)), started(Clock::now()) {
        cwdPath = normalizePath("/", cfg.home);
        if (!fs->exists(cwdPath)) {
            cwdPath = "/";
        }
        env["HOME"] = cfg.home;
        env["USER"] = cfg.user;
        env["HOST"] = cfg.host;
        env["SHELL"] = "roboslop-sh";
        registerBuiltins();
    }

    Shell(const Shell&) = delete;
    auto operator=(const Shell&) -> Shell& = delete;
    Shell(Shell&&) noexcept = default;
    auto operator=(Shell&&) noexcept -> Shell& = default;
    ~Shell() = default;

    auto registerCommand(const std::string& name, std::string help, CommandFn fn) -> void {
        commands[name] = Entry{.help = std::move(help), .fn = std::move(fn)};
    }

    [[nodiscard]] auto execute(std::string_view line) -> ShellResult {
        if (!line.empty() && !std::ranges::all_of(line, [](char c) { return c == ' '; })) {
            historyLines.emplace_back(line);
        }
        auto tokens = tokenize(line, [this](std::string_view name) { return envValue(name); });
        if (!tokens) {
            return detail::errorResult("sh", tokens.error());
        }
        ShellResult total;
        // Split on ; and && at the top level.
        std::vector<Token> current;
        bool skipNext = false;
        const auto runPipeline = [&](std::vector<Token>& pipeline) {
            if (pipeline.empty()) {
                return;
            }
            if (skipNext) {
                skipNext = false;
                return;
            }
            const ShellResult r = executePipeline(pipeline);
            total.output += r.output;
            total.status = r.status;
            if (r.followPath) {
                total.followPath = r.followPath;
            }
            total.clear = total.clear || r.clear;
        };
        for (auto& t : *tokens) {
            if (t.isOperator && (t.text == ";" || t.text == "&&")) {
                const bool andThen = t.text == "&&";
                runPipeline(current);
                current.clear();
                if (andThen && total.status != 0) {
                    skipNext = true;
                }
                continue;
            }
            current.push_back(std::move(t));
        }
        runPipeline(current);
        return total;
    }

    // Candidate completions for the last word of `line`: command names
    // when it is the first word, VFS paths otherwise. Directories get a
    // trailing slash.
    [[nodiscard]] auto complete(std::string_view line) const -> std::vector<std::string> {
        std::vector<std::string> out;
        const auto lastSpace = line.rfind(' ');
        const std::string_view word =
            lastSpace == std::string_view::npos ? line : line.substr(lastSpace + 1);
        const bool firstWord =
            lastSpace == std::string_view::npos ||
            line.substr(0, lastSpace).find_first_not_of(' ') == std::string_view::npos;
        if (firstWord && !word.contains('/')) {
            for (const auto& [name, _] : commands) {
                if (name.starts_with(word)) {
                    out.push_back(name);
                }
            }
            return out;
        }
        const auto slash = word.rfind('/');
        const std::string dirPart{slash == std::string_view::npos ? "" : word.substr(0, slash + 1)};
        const std::string_view prefix =
            slash == std::string_view::npos ? word : word.substr(slash + 1);
        const auto entries = fs->list(dirPart.empty() ? "." : dirPart, cwdPath);
        if (!entries) {
            return out;
        }
        for (const auto& e : *entries) {
            if (e.name.starts_with(prefix)) {
                out.push_back(dirPart + e.name + (e.isDirectory ? "/" : ""));
            }
        }
        return out;
    }

    [[nodiscard]] auto prompt() const -> std::string {
        std::string shown = cwdPath;
        if (cwdPath == cfg.home) {
            shown = "~";
        } else if (cfg.home != "/" && cwdPath.starts_with(cfg.home + "/")) {
            shown = "~" + cwdPath.substr(cfg.home.size());
        }
        return std::format("{}@{}:{}$ ", cfg.user, cfg.host, shown);
    }

    [[nodiscard]] auto cwd() const noexcept -> const std::string& {
        return cwdPath;
    }

    [[nodiscard]] auto history() const noexcept -> const std::vector<std::string>& {
        return historyLines;
    }

    [[nodiscard]] auto config() const noexcept -> const ShellConfig& {
        return cfg;
    }

    [[nodiscard]] auto envValue(std::string_view name) const -> std::string {
        if (name == "PWD") {
            return cwdPath;
        }
        const auto it = env.find(std::string{name});
        return it == env.end() ? std::string{} : it->second;
    }

    [[nodiscard]] auto vfs() noexcept -> Vfs& {
        return *fs;
    }

  private:
    using Clock = std::chrono::steady_clock;

    struct Entry {
        std::string help;
        CommandFn fn;
    };

    [[nodiscard]] auto executePipeline(std::vector<Token>& tokens) -> ShellResult {
        // Split on | into stages; pull > / >> out of the last stage.
        std::vector<std::vector<std::string>> stages(1);
        std::optional<std::pair<std::string, bool>> redirect; // path, append
        for (std::size_t i = 0; i < tokens.size(); ++i) {
            const auto& t = tokens[i];
            if (t.isOperator && t.text == "|") {
                stages.emplace_back();
                continue;
            }
            if (t.isOperator && (t.text == ">" || t.text == ">>")) {
                if (i + 1 >= tokens.size() || tokens[i + 1].isOperator) {
                    return detail::errorResult("sh", toError(ShellError::BadRedirect));
                }
                redirect = {tokens[i + 1].text, t.text == ">>"};
                ++i;
                continue;
            }
            stages.back().push_back(t.text);
        }

        ShellResult result;
        std::string carry;
        for (auto& args : stages) {
            if (args.empty()) {
                return {.output = "sh: empty command in pipeline\n", .status = 2};
            }
            const auto it = commands.find(args[0]);
            if (it == commands.end()) {
                return {.output = std::format("{}: command not found\n", args[0]), .status = 127};
            }
            CommandContext ctx{.shell = *this, .vfs = *fs, .args = args, .stdinText = carry};
            result = it->second.fn(ctx);
            carry = result.output;
            if (result.status != 0) {
                break;
            }
        }
        if (redirect && result.status == 0) {
            auto w = fs->writeFile(redirect->first, result.output, redirect->second, cwdPath);
            if (!w) {
                return detail::errorResult("sh", w.error());
            }
            result.output.clear();
        }
        return result;
    }

    auto registerBuiltins() -> void {
        registerCommand("help", "list commands", [this](CommandContext&) {
            std::string out;
            for (const auto& [name, e] : commands) {
                out += std::format("{:<10} {}\n", name, e.help);
            }
            return ShellResult{.output = out};
        });
        registerCommand("pwd", "print the working directory", [this](CommandContext&) {
            return ShellResult{.output = cwdPath + "\n"};
        });
        registerCommand("cd", "change directory", [this](CommandContext& c) {
            const std::string target = c.args.size() > 1 ? c.args[1] : cfg.home;
            const auto abs = normalizePath(cwdPath, target);
            const auto st = fs->stat(abs);
            if (!st) {
                return detail::errorResult("cd", st.error());
            }
            if (!st->isDirectory) {
                return detail::errorResult("cd", toError(VfsError::NotADirectory, abs));
            }
            cwdPath = abs;
            return ShellResult{};
        });
        registerCommand("ls", "list a directory (-l long, -a all)", [this](CommandContext& c) {
            const auto p = detail::parseArgs(c.args, "");
            std::vector<std::string> targets = p.positional;
            if (targets.empty()) {
                targets.emplace_back(".");
            }
            std::string out;
            int status = 0;
            for (const auto& t : targets) {
                const auto st = fs->stat(t, cwdPath);
                if (!st) {
                    out += detail::errorResult("ls", st.error()).output;
                    status = 1;
                    continue;
                }
                if (!st->isDirectory) {
                    out += t + "\n";
                    continue;
                }
                const auto entries = fs->list(t, cwdPath);
                if (!entries) {
                    out += detail::errorResult("ls", entries.error()).output;
                    status = 1;
                    continue;
                }
                if (targets.size() > 1) {
                    out += t + ":\n";
                }
                for (const auto& e : *entries) {
                    if (p.flags.contains('l')) {
                        out += std::format(
                            "{} {:>8} {}{}\n",
                            e.isDirectory ? 'd' : '-',
                            e.size,
                            e.name,
                            e.isDirectory ? "/" : ""
                        );
                    } else {
                        out += e.name + (e.isDirectory ? "/" : "") + "\n";
                    }
                }
            }
            return ShellResult{.output = out, .status = status};
        });
        registerCommand("cat", "print files (or stdin)", [this](CommandContext& c) {
            if (c.args.size() == 1) {
                return ShellResult{.output = c.stdinText};
            }
            std::string out;
            for (std::size_t i = 1; i < c.args.size(); ++i) {
                auto r = fs->readFile(c.args[i], cwdPath);
                if (!r) {
                    return detail::errorResult("cat", r.error());
                }
                out += *r;
                if (!out.empty() && out.back() != '\n') {
                    out += '\n';
                }
            }
            return ShellResult{.output = out};
        });
        registerCommand("echo", "print arguments", [](CommandContext& c) {
            std::string out;
            for (std::size_t i = 1; i < c.args.size(); ++i) {
                if (i > 1) {
                    out += ' ';
                }
                out += c.args[i];
            }
            return ShellResult{.output = out + "\n"};
        });
        registerCommand("mkdir", "create directories (-p parents)", [this](CommandContext& c) {
            const auto p = detail::parseArgs(c.args, "");
            if (p.positional.empty()) {
                return detail::usage("mkdir [-p] DIR...");
            }
            for (const auto& d : p.positional) {
                if (auto r = fs->mkdir(d, p.flags.contains('p'), cwdPath); !r) {
                    return detail::errorResult("mkdir", r.error());
                }
            }
            return ShellResult{};
        });
        registerCommand("rm", "remove files (-r recursive)", [this](CommandContext& c) {
            const auto p = detail::parseArgs(c.args, "");
            if (p.positional.empty()) {
                return detail::usage("rm [-r] PATH...");
            }
            for (const auto& d : p.positional) {
                if (auto r = fs->remove(d, p.flags.contains('r'), cwdPath); !r) {
                    return detail::errorResult("rm", r.error());
                }
            }
            return ShellResult{};
        });
        registerCommand("touch", "create empty files", [this](CommandContext& c) {
            if (c.args.size() < 2) {
                return detail::usage("touch FILE...");
            }
            for (std::size_t i = 1; i < c.args.size(); ++i) {
                if (fs->exists(c.args[i], cwdPath)) {
                    continue;
                }
                if (auto r = fs->writeFile(c.args[i], "", false, cwdPath); !r) {
                    return detail::errorResult("touch", r.error());
                }
            }
            return ShellResult{};
        });
        registerCommand("head", "first lines (-n N, default 10)", [this](CommandContext& c) {
            return headTail(c, /*fromEnd=*/false);
        });
        registerCommand(
            "tail", "last lines (-n N, default 10; -f follow)", [this](CommandContext& c) {
                return headTail(c, /*fromEnd=*/true);
            }
        );
        registerCommand(
            "grep", "lines matching PATTERN (-i ignore case)", [this](CommandContext& c) {
                const auto p = detail::parseArgs(c.args, "");
                if (p.positional.empty()) {
                    return detail::usage("grep [-i] PATTERN [FILE...]");
                }
                const bool icase = p.flags.contains('i');
                auto lower = [](std::string s) {
                    std::ranges::transform(s, s.begin(), [](unsigned char ch) {
                        return static_cast<char>(std::tolower(ch));
                    });
                    return s;
                };
                const std::string pattern = icase ? lower(p.positional[0]) : p.positional[0];
                std::string input;
                if (p.positional.size() == 1) {
                    input = c.stdinText;
                } else {
                    for (std::size_t i = 1; i < p.positional.size(); ++i) {
                        auto r = fs->readFile(p.positional[i], cwdPath);
                        if (!r) {
                            return detail::errorResult("grep", r.error());
                        }
                        input += *r;
                        if (!input.empty() && input.back() != '\n') {
                            input += '\n';
                        }
                    }
                }
                std::string out;
                for (const auto& line : detail::splitLines(input)) {
                    const std::string hay = icase ? lower(line) : line;
                    if (hay.contains(pattern)) {
                        out += line + "\n";
                    }
                }
                return ShellResult{.output = out, .status = out.empty() ? 1 : 0};
            }
        );
        registerCommand("wc", "count lines, words, bytes", [this](CommandContext& c) {
            std::string input = c.stdinText;
            if (c.args.size() > 1) {
                auto r = fs->readFile(c.args[1], cwdPath);
                if (!r) {
                    return detail::errorResult("wc", r.error());
                }
                input = *r;
            }
            const auto lines = std::ranges::count(input, '\n');
            std::size_t words = 0;
            bool inWord = false;
            for (const char ch : input) {
                const bool space = ch == ' ' || ch == '\n' || ch == '\t';
                if (!space && !inWord) {
                    ++words;
                }
                inWord = !space;
            }
            return ShellResult{.output = std::format("{} {} {}\n", lines, words, input.size())};
        });
        registerCommand("tree", "recursive listing", [this](CommandContext& c) {
            const std::string start = c.args.size() > 1 ? c.args[1] : ".";
            const auto abs = normalizePath(cwdPath, start);
            std::string out = abs + "\n";
            int status = 0;
            std::function<void(const std::string&, const std::string&, int)> walk =
                [&](const std::string& dir, const std::string& indent, int depth) {
                    if (depth > 8) {
                        return;
                    }
                    const auto entries = fs->list(dir);
                    if (!entries) {
                        status = 1;
                        return;
                    }
                    for (std::size_t i = 0; i < entries->size(); ++i) {
                        const auto& e = (*entries)[i];
                        const bool last = i + 1 == entries->size();
                        out += indent + (last ? "└── " : "├── ") + e.name +
                               (e.isDirectory ? "/" : "") + "\n";
                        if (e.isDirectory) {
                            walk(
                                dir == "/" ? "/" + e.name : dir + "/" + e.name,
                                indent + (last ? "    " : "│   "),
                                depth + 1
                            );
                        }
                    }
                };
            walk(abs, "", 0);
            return ShellResult{.output = out, .status = status};
        });
        registerCommand("clear", "clear the screen", [](CommandContext&) {
            return ShellResult{.clear = true};
        });
        registerCommand("env", "print environment", [this](CommandContext&) {
            std::string out;
            for (const auto& [k, v] : env) {
                out += std::format("{}={}\n", k, v);
            }
            out += std::format("PWD={}\n", cwdPath);
            return ShellResult{.output = out};
        });
        registerCommand("export", "set NAME=value", [this](CommandContext& c) {
            if (c.args.size() < 2) {
                return detail::usage("export NAME=value");
            }
            for (std::size_t i = 1; i < c.args.size(); ++i) {
                const auto eq = c.args[i].find('=');
                if (eq == std::string::npos || eq == 0) {
                    return detail::usage("export NAME=value");
                }
                env[c.args[i].substr(0, eq)] = c.args[i].substr(eq + 1);
            }
            return ShellResult{};
        });
        registerCommand("history", "show command history", [this](CommandContext&) {
            std::string out;
            for (std::size_t i = 0; i < historyLines.size(); ++i) {
                out += std::format("{:>4}  {}\n", i + 1, historyLines[i]);
            }
            return ShellResult{.output = out};
        });
        registerCommand("date", "current date and time", [](CommandContext&) {
            const auto now = std::chrono::system_clock::now();
            const std::time_t t = std::chrono::system_clock::to_time_t(now);
            char buf[64];
            std::tm tm{};
            localtime_r(&t, &tm);
            std::strftime(buf, sizeof(buf), "%a %b %e %H:%M:%S %Z %Y", &tm);
            return ShellResult{.output = std::string{buf} + "\n"};
        });
        registerCommand("uptime", "time since this shell started", [this](CommandContext&) {
            const auto s = std::chrono::duration_cast<std::chrono::seconds>(Clock::now() - started);
            return ShellResult{.output = std::format("up {}s\n", s.count())};
        });
    }

    [[nodiscard]] auto headTail(CommandContext& c, bool fromEnd) -> ShellResult {
        const auto p = detail::parseArgs(c.args, "n");
        std::size_t n = 10;
        if (const auto it = p.flags.find('n'); it != p.flags.end()) {
            try {
                n = static_cast<std::size_t>(std::stoul(it->second));
            } catch (...) { // std::stoul is the only throwing API here; keep it local
                return detail::usage(fromEnd ? "tail [-n N] [-f] [FILE]" : "head [-n N] [FILE]");
            }
        }
        const bool follow = fromEnd && p.flags.contains('f');
        std::string input = c.stdinText;
        std::optional<std::string> followPath;
        if (!p.positional.empty()) {
            auto r = fs->readFile(p.positional[0], cwdPath);
            if (!r) {
                return detail::errorResult(fromEnd ? "tail" : "head", r.error());
            }
            input = *r;
            if (follow) {
                followPath = normalizePath(cwdPath, p.positional[0]);
            }
        }
        auto lines = detail::splitLines(input);
        if (lines.size() > n) {
            if (fromEnd) {
                lines.erase(lines.begin(), lines.end() - static_cast<long>(n));
            } else {
                lines.resize(n);
            }
        }
        std::string out;
        for (const auto& l : lines) {
            out += l + "\n";
        }
        return ShellResult{.output = out, .status = 0, .followPath = followPath, .clear = false};
    }

    Vfs* fs;
    ShellConfig cfg;
    std::string cwdPath;
    std::map<std::string, std::string> env;
    std::map<std::string, Entry> commands;
    std::vector<std::string> historyLines;
    Clock::time_point started;
};

} // namespace roboslop
