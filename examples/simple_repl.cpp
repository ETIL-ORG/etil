// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/core/interpreter.hpp"
#include "etil/core/primitives.hpp"
#include "etil/core/version.hpp"
#include "etil/selection/selection_engine.hpp"
#include "etil/evolution/evolution_engine.hpp"

#include <replxx.hxx>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <getopt.h>
#include <iostream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>
#include <unistd.h>

using namespace etil::core;

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

struct ReplConfig {
    std::string source;           // path of loaded config file ("" = defaults)
    int version = 1;
    std::string theme = "dark";
    int max_history_lines = 500;
    std::string history_file;     // optional override from JSON config
    std::vector<std::string> startup_files;  // .til files loaded on startup
};

static std::string find_config_path(const std::string& cli_path) {
    // 1. Explicit -c flag — return as-is (let load_config report errors)
    if (!cli_path.empty()) {
        return cli_path;
    }

    // 2. Environment variable
    const char* env = std::getenv("ETIL_REPL_CONFIG");
    if (env && env[0] != '\0') {
        return env;
    }

    // 3. Local .etil/repl/repl.json
    const std::filesystem::path local{".etil/repl/repl.json"};
    if (std::filesystem::exists(local)) {
        return local.string();
    }

    // 4. $HOME/.etil/repl/repl.json
    const char* home = std::getenv("HOME");
    if (home && home[0] != '\0') {
        std::filesystem::path global =
            std::filesystem::path(home) / ".etil" / "repl" / "repl.json";
        if (std::filesystem::exists(global)) {
            return global.string();
        }
    }

    // 5. No config found
    return "";
}

static ReplConfig load_config(const std::string& path) {
    ReplConfig cfg;
    cfg.source = path;

    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "Warning: cannot open config file '" << path
                  << "' — using defaults\n";
        cfg.source.clear();
        return cfg;
    }

    try {
        auto j = nlohmann::json::parse(file);
        cfg.version = j.value("version", 1);
        cfg.theme = j.value("theme", std::string("dark"));
        cfg.max_history_lines = j.value("max-history-lines", 500);
        cfg.history_file = j.value("history-file", std::string(""));
        if (j.contains("startup-files") && j["startup-files"].is_array()) {
            for (const auto& f : j["startup-files"]) {
                if (f.is_string()) {
                    cfg.startup_files.push_back(f.get<std::string>());
                }
            }
        }
    } catch (const nlohmann::json::exception& e) {
        std::cerr << "Warning: malformed config '" << path << "': " << e.what()
                  << " — using defaults\n";
        cfg = ReplConfig{};
    }

    return cfg;
}

// ---------------------------------------------------------------------------
// Color themes (ANSI escape sequences)
// ---------------------------------------------------------------------------

struct ColorTheme {
    std::string name;
    std::string prompt;   // prompt + user input
    std::string output;   // normal interpreter output
    std::string error;    // error messages
    std::string reset;    // reset all attributes
};

static ColorTheme dark_theme() {
    return {"dark", "\033[1;36;40m", "\033[0;37;40m", "\033[1;31;40m", "\033[0m"};
}

static ColorTheme light_theme() {
    return {"light", "\033[0;34;107m", "\033[0;30;107m", "\033[1;31;107m", "\033[0m"};
}

static ColorTheme no_theme() {
    return {"none", "", "", "", ""};
}

static ColorTheme theme_for_name(const std::string& name) {
    if (name == "light") return light_theme();
    return dark_theme();
}

/// Flush captured interpreter output to a target stream with a single color.
/// Since errors are now separated at the source (err_ vs out_), the caller
/// chooses which color to apply.
static void flush_output(std::ostringstream& oss, std::ostream& target,
                         const std::string& color, const std::string& reset) {
    std::string text = oss.str();
    oss.str("");
    oss.clear();
    if (text.empty()) return;

    if (color.empty()) {
        target << text;
        return;
    }

    target << color << text << reset;
}

// ---------------------------------------------------------------------------
// replxx helpers
// ---------------------------------------------------------------------------

/// Build the prompt string. replxx parses ANSI escape sequences natively
/// to compute visible width, so no special wrapping is needed.
static std::string build_prompt(const ColorTheme& theme, bool compiling) {
    return theme.prompt
         + (compiling ? ":  " : "> ")
         + theme.reset;
}

/// Resolve the history file path.
/// Priority: config override → $HOME/.etil/repl/history.txt → local fallback.
static std::string find_history_path(const ReplConfig& config) {
    if (!config.history_file.empty()) {
        return config.history_file;
    }

    const char* home = std::getenv("HOME");
    if (home && home[0] != '\0') {
        auto dir = std::filesystem::path(home) / ".etil" / "repl";
        std::filesystem::create_directories(dir);
        return (dir / "history.txt").string();
    }

    // Last resort: local file
    return "etil_history.txt";
}

/// Tab-completion callback for replxx.
static replxx::Replxx::completions_t completion_hook(
    Interpreter& interp,
    std::string const& context, int& contextLen) {
    replxx::Replxx::completions_t completions;

    // Extract the prefix being completed
    std::string prefix = context.substr(
        context.size() - static_cast<size_t>(contextLen),
        static_cast<size_t>(contextLen));

    auto words = interp.completable_words();

    // Add REPL meta commands
    for (auto& mc : {"/help", "/quit", "/exit", "/clear", "/words",
                     "/history", "/dark", "/light"})
        words.push_back(mc);

    for (const auto& w : words) {
        if (w.compare(0, prefix.size(), prefix) == 0) {
            completions.emplace_back(w);
        }
    }
    return completions;
}

// ---------------------------------------------------------------------------
// Usage / version
// ---------------------------------------------------------------------------

static void print_usage(const char* prog) {
    std::cout
        << "Usage: " << prog << " [OPTIONS] [--] [ARGS...]\n"
        << "\n"
        << "Options:\n"
        << "  -h, --help           Print this help and exit\n"
        << "  -v, --version        Print version and exit\n"
        << "  -q, --quiet          Pipe-friendly mode: suppress banner, prompt,\n"
        << "                       stack status, and goodbye; infer --color=never\n"
        << "  -c, --config <path>  Load REPL config from <path>\n"
        << "  --noconfig           Skip configuration file loading\n"
        << "  --color[=WHEN]       Enable color output: always, never, auto (default: auto)\n"
        << "\n"
        << "Arguments:\n"
        << "  Each non-option argument is interpreted as a line of TIL code,\n"
        << "  evaluated in order before the interactive REPL starts.\n"
        << "\n"
        << "  If an argument is the literal \"-\", all subsequent input is read\n"
        << "  from stdin until EOF, then processing continues.\n";
}

static void print_version() {
    std::cout << "etil_repl " << etil::core::ETIL_VERSION
              << " (built " << etil::core::ETIL_BUILD_TIMESTAMP << ")\n";
}

// ---------------------------------------------------------------------------
// Meta commands
// ---------------------------------------------------------------------------

// Check if line starts with a meta command (first non-whitespace is '/')
static bool is_meta_command(const std::string& line) {
    auto pos = line.find_first_not_of(" \t");
    return pos != std::string::npos && line[pos] == '/';
}

// Extract the meta command token (e.g. "/quit" from "  /quit  ")
static std::string get_meta_command(const std::string& line) {
    std::istringstream iss(line);
    std::string cmd;
    iss >> cmd;
    std::transform(cmd.begin(), cmd.end(), cmd.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return cmd;
}

// Handle a meta command. Returns true to continue REPL, false to exit.
// /words output goes through interp_out and must be flushed by the caller.
static bool handle_meta(const std::string& line, Interpreter& interp,
                        std::ostringstream& interp_out,
                        std::ostringstream& interp_err,
                        ColorTheme& theme, bool use_color) {
    auto cmd = get_meta_command(line);

    if (cmd == "/quit" || cmd == "/exit") {
        return false;
    }
    if (cmd == "/help") {
        // Check for optional word argument: /help <word>
        std::istringstream iss(line);
        std::string skip_cmd, word_arg;
        iss >> skip_cmd;  // consume "/help"
        if (iss >> word_arg) {
            // Delegate to the help primitive via the interpreter
            interp.interpret_line("help " + word_arg);
            flush_output(interp_out, std::cout, theme.output, theme.reset);
            flush_output(interp_err, std::cerr, theme.error, theme.reset);
            return true;
        }
        std::cout << theme.output
                  << "Meta commands:\n"
                  << "  /help [word]  Show help for a word, or list meta commands\n"
                  << "  /quit        Exit the REPL\n"
                  << "  /exit        Exit the REPL\n"
                  << "  /clear       Clear the data stack\n"
                  << "  /words       List all dictionary words\n"
                  << "  /history     Show command history\n"
                  << "  /dark        Switch to dark color theme\n"
                  << "  /light       Switch to light color theme\n"
                  << theme.reset;
        return true;
    }
    if (cmd == "/clear") {
        interp.context().data_stack().clear();
        std::cout << theme.output << "Stack cleared." << theme.reset << "\n";
        return true;
    }
    if (cmd == "/words") {
        interp.print_all_words();  // writes to interp_out
        flush_output(interp_out, std::cout, theme.output, theme.reset);
        flush_output(interp_err, std::cerr, theme.error, theme.reset);
        return true;
    }
    if (cmd == "/dark") {
        if (use_color) theme = dark_theme();
        std::cout << theme.output << "Switched to dark theme."
                  << theme.reset << "\n";
        return true;
    }
    if (cmd == "/light") {
        if (use_color) theme = light_theme();
        std::cout << theme.output << "Switched to light theme."
                  << theme.reset << "\n";
        return true;
    }
    // /history is handled in the main loop (needs access to Replxx instance)
    // so if we get here, it's an unknown command
    std::cout << theme.error << "Unknown command: " << cmd << " (try /help)"
              << theme.reset << "\n";
    return true;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char* argv[]) {
    enum { OPT_NOCONFIG = 256, OPT_COLOR = 257 };

    static struct option long_options[] = {
        {"help",     no_argument,       nullptr, 'h'},
        {"version",  no_argument,       nullptr, 'v'},
        {"quiet",    no_argument,       nullptr, 'q'},
        {"noconfig", no_argument,       nullptr, OPT_NOCONFIG},
        {"config",   required_argument, nullptr, 'c'},
        {"color",    optional_argument, nullptr, OPT_COLOR},
        {nullptr,    0,                 nullptr,  0 }
    };

    bool noconfig = false;
    bool quiet = false;
    bool color_explicit = false;
    std::string config_path;
    std::string color_mode = "auto";

    int opt;
    while ((opt = getopt_long(argc, argv, "hvqc:", long_options, nullptr)) != -1) {
        switch (opt) {
        case 'h': print_usage(argv[0]); return 0;
        case 'v': print_version(); return 0;
        case 'q': quiet = true; break;
        case OPT_NOCONFIG: noconfig = true; break;
        case 'c': config_path = optarg; break;
        case OPT_COLOR: color_mode = optarg ? optarg : "always"; color_explicit = true; break;
        default:  print_usage(argv[0]); return 1;
        }
    }

    // --quiet infers --color=never unless user explicitly passed --color
    if (quiet && !color_explicit) color_mode = "never";

    // Load configuration
    ReplConfig config;
    if (!noconfig) {
        std::string path = find_config_path(config_path);
        if (!path.empty()) {
            config = load_config(path);
        }
    }

    // Capture interpreter output so we can colorize it
    std::ostringstream interp_out;
    std::ostringstream interp_err;
    Dictionary dict;
    register_primitives(dict);
    Interpreter interp(dict, interp_out, interp_err);

    // Wire selection and evolution engines for select-*/evolve-* primitives
    etil::selection::SelectionEngine selection_engine;
    etil::evolution::EvolutionConfig evo_config;
    etil::evolution::EvolutionEngine evolution_engine(evo_config, dict);
    interp.context().set_selection_engine(&selection_engine);
    interp.context().set_evolution_engine(&evolution_engine);

    // Register handler words as concepts so help.til can attach metadata
    interp.register_handler_words();

    // Default startup files when no config specifies them
    if (config.startup_files.empty()) {
        config.startup_files = {"data/builtins.til", "data/help.til"};
    }

    // Load startup .til files (before CLI args and interactive loop)
    if (!config.startup_files.empty()) {
        interp.load_startup_files(config.startup_files);
        flush_output(interp_out, std::cout, "", "");
        flush_output(interp_err, std::cerr, "", "");
    }

    bool interactive = isatty(STDIN_FILENO);

    bool use_color;
    if (color_mode == "always")      use_color = true;
    else if (color_mode == "never")  use_color = false;
    else                             use_color = interactive;

    ColorTheme theme = use_color ? theme_for_name(config.theme) : no_theme();

    // Process non-option arguments as TIL code (before interactive REPL)
    for (int i = optind; i < argc; ++i) {
        if (std::string(argv[i]) == "-") {
            std::string line;
            while (std::getline(std::cin, line)) {
                interp.interpret_line(line);
                if (interp.context().abort_requested()) {
                    interp.context().clear_abort();
                    interp.context().reset_limits();
                }
                flush_output(interp_out, std::cout, theme.output, theme.reset);
                flush_output(interp_err, std::cerr, theme.error, theme.reset);
            }
            if (isatty(STDIN_FILENO)) {
                std::cin.clear();
            }
        } else {
            interp.interpret_line(std::string(argv[i]));
            if (interp.context().abort_requested()) {
                interp.context().clear_abort();
                interp.context().reset_limits();
            }
            flush_output(interp_out, std::cout, theme.output, theme.reset);
            flush_output(interp_err, std::cerr, theme.error, theme.reset);
        }
    }

    // Print banner only on interactive TTY (unless --quiet)
    if (interactive && !quiet) {
        std::cout << theme.prompt
                  << "Evolutionary TIL REPL v" << etil::core::ETIL_VERSION
                  << " (built " << etil::core::ETIL_BUILD_TIMESTAMP << ")"
                  << theme.reset << "\n";
        std::cout << "Type '/help' for commands, '/quit' to exit\n\n";
    }

    // Decide whether to use replxx (interactive, non-quiet) or plain getline
    bool use_replxx = interactive && !quiet;

    if (use_replxx) {
        // ---------------------------------------------------------------
        // replxx-based interactive loop
        // ---------------------------------------------------------------
        replxx::Replxx rx;

        // Configure replxx
        rx.set_max_history_size(config.max_history_lines);
        // FORTH tokens can contain /, +, ., !, @, etc. — only break on whitespace
        rx.set_word_break_characters(" \t\n");
        rx.set_no_color(!use_color);
        rx.install_window_change_handler();
        rx.set_unique_history(true);

        // Install tab-completion callback
        rx.set_completion_callback(
            [&interp](std::string const& context, int& contextLen) {
                return completion_hook(interp, context, contextLen);
            });

        // Load command history
        std::string history_path = find_history_path(config);
        rx.history_load(history_path);

        while (true) {
            std::string prompt = build_prompt(theme, interp.compiling());
            char const* cinput = rx.input(prompt);

            if (cinput == nullptr) {
                // EOF (Ctrl-D)
                break;
            }

            std::string line(cinput);

            // Skip empty lines
            if (line.find_first_not_of(" \t") == std::string::npos) {
                continue;
            }

            // Add non-empty lines to history
            rx.history_add(line);

            // Meta commands only in interpret mode
            if (!interp.compiling() && is_meta_command(line)) {
                auto cmd = get_meta_command(line);

                // /history needs access to the Replxx instance
                if (cmd == "/history") {
                    auto scan = rx.history_scan();
                    int idx = 0;
                    while (scan.next()) {
                        std::cout << theme.output
                                  << "  " << idx++ << "  " << scan.get().text()
                                  << theme.reset << "\n";
                    }
                    continue;
                }

                if (!handle_meta(line, interp, interp_out, interp_err, theme, use_color)) {
                    break;
                }
                continue;
            }

            interp.interpret_line(line);

            // Recover from program-level abort
            if (interp.context().abort_requested()) {
                interp.context().clear_abort();
                interp.context().reset_limits();  // clears cancelled_
            }

            flush_output(interp_out, std::cout, theme.output, theme.reset);
            flush_output(interp_err, std::cerr, theme.error, theme.reset);

            // Show stack status after interpret-mode lines
            if (!interp.compiling()) {
                std::cout << "\n" << theme.output << interp.stack_status()
                          << theme.reset << "\n";
            }
        }

        // Save history on exit
        rx.history_save(history_path);

    } else {
        // ---------------------------------------------------------------
        // Plain std::getline loop (piped input, --quiet)
        // ---------------------------------------------------------------
        std::string line;
        while (true) {
            if (interactive && !quiet) {
                std::cout << theme.prompt
                          << (interp.compiling() ? ":  " : "> ");
            }
            if (!std::getline(std::cin, line)) {
                if (interactive && !quiet) std::cout << theme.reset;
                break;
            }
            if (interactive && !quiet) std::cout << theme.reset;

            // Meta commands only in interpret mode
            if (!interp.compiling() && is_meta_command(line)) {
                if (!handle_meta(line, interp, interp_out, interp_err, theme, use_color)) {
                    break;
                }
                continue;
            }

            interp.interpret_line(line);

            // Recover from program-level abort
            if (interp.context().abort_requested()) {
                interp.context().clear_abort();
                interp.context().reset_limits();  // clears cancelled_
            }

            flush_output(interp_out, std::cout, theme.output, theme.reset);
            flush_output(interp_err, std::cerr, theme.error, theme.reset);

            // Show stack status after interpret-mode lines
            if (!interp.compiling() && !quiet) {
                std::cout << "\n" << theme.output << interp.stack_status()
                          << theme.reset << "\n";
            }
        }
    }

    interp.shutdown();
    if (interactive && !quiet) {
        std::cout << theme.prompt << "Goodbye!" << theme.reset << "\n";
    }
    return 0;
}
