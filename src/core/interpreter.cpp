// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/core/interpreter.hpp"
#include "etil/core/heap_map.hpp"
#include "etil/core/heap_matrix.hpp"
#include "etil/core/heap_observable.hpp"
#include "etil/core/heap_object.hpp"
#include "etil/core/heap_string.hpp"
#include "etil/core/primitives.hpp"
#include "etil/lvfs/lvfs.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <ranges>

namespace etil::core {

Interpreter::Interpreter(Dictionary& dict, std::ostream& out, std::ostream& err)
    : dict_(dict), ctx_(0), out_(out), err_(err),
      interpret_handlers_(dict_, ctx_, out_, err_,
                          [this]() { print_all_words(); }),
      compile_handlers_(err_, compiling_word_name_, current_bytecode_,
                        control_stack_,
                        [this]() { finalize_definition(); },
                        [this]() { abandon_definition(); },
                        dict_),
      control_flow_handlers_(err_, current_bytecode_, control_stack_) {
    ctx_.set_dictionary(&dict_);
    ctx_.set_interpreter(this);
    ctx_.set_out(&out_);
    ctx_.set_err(&err_);
}

void Interpreter::shutdown() {
    auto& stack = ctx_.data_stack();
    while (auto val = stack.pop()) {
        val->release();
    }
}

// --- Path mapping ---

void Interpreter::set_lvfs(etil::lvfs::Lvfs* lvfs) {
    lvfs_ = lvfs;
    ctx_.set_lvfs(lvfs);
}

void Interpreter::set_home_dir(const std::string& dir) {
    home_dir_ = dir;
    // Ensure trailing separator for prefix matching
    if (!home_dir_.empty() && home_dir_.back() != '/') {
        home_dir_ += '/';
    }
}

void Interpreter::set_library_dir(const std::string& dir) {
    library_dir_ = dir;
    if (!library_dir_.empty() && library_dir_.back() != '/') {
        library_dir_ += '/';
    }
}

std::string Interpreter::resolve_under(const std::string& base,
                                        const std::string& relative) {
    if (base.empty() || relative.empty()) return {};

    // Reject absolute paths in relative arg
    if (relative[0] == '/') return {};

    // Reject explicit traversal components
    // Check for ".." as a path component
    namespace fs = std::filesystem;
    fs::path rel(relative);
    for (const auto& component : rel) {
        if (component == "..") return {};
    }

    // Join and normalize (lexically, no filesystem access)
    fs::path joined = fs::path(base) / rel;
    fs::path canonical = joined.lexically_normal();

    // Verify the result stays under base
    std::string result = canonical.string();
    if (result.compare(0, base.size(), base) != 0) {
        return {};
    }

    return result;
}

std::string Interpreter::resolve_home_path(const std::string& relative_path) const {
    if (lvfs_) return lvfs_->resolve_home_path(relative_path);
    if (home_dir_.empty()) {
        // Backward compat: no home dir configured, use path as-is
        return relative_path;
    }
    return resolve_under(home_dir_, relative_path);
}

std::string Interpreter::resolve_library_path(const std::string& relative_path) const {
    if (lvfs_) return lvfs_->resolve_library_path(relative_path);
    if (library_dir_.empty()) {
        return {};  // Library not configured
    }
    return resolve_under(library_dir_, relative_path);
}

std::string Interpreter::resolve_logical_path(const std::string& path) const {
    if (lvfs_) return lvfs_->resolve_logical_path(path);

    static const std::string home_prefix = "/home/";
    static const std::string library_prefix = "/library/";

    if (path.compare(0, home_prefix.size(), home_prefix) == 0) {
        return resolve_home_path(path.substr(home_prefix.size()));
    }
    if (path.compare(0, library_prefix.size(), library_prefix) == 0) {
        return resolve_library_path(path.substr(library_prefix.size()));
    }

    // No logical prefix — return as-is (startup files, etc.)
    return path;
}

// --- Public ---

void Interpreter::interpret_line(const std::string& raw_line) {
    std::istringstream iss(raw_line);
    ctx_.set_input_stream(&iss);
    std::string word;
    line_had_error_ = false;

    while (iss >> word) {
        if (!ctx_.tick()) {
            if (!ctx_.abort_requested())
                err_ << "Error: execution limit reached\n";
            line_had_error_ = true;
            break;
        }
        // '#' starts a line comment — skip rest of line
        if (word[0] == '#') break;

        // ':' starts a colon definition
        if (word == ":") {
            if (compiling_) {
                err_ << "Error: nested colon definitions not allowed\n";
                line_had_error_ = true;
                continue;
            }
            if (!(iss >> compiling_word_name_)) {
                err_ << "Error: missing word name after ':'\n";
                line_had_error_ = true;
                continue;
            }
            compiling_ = true;
            compiling_start_line_ = source_line_;
            current_bytecode_ = std::make_shared<ByteCode>();
            control_stack_.clear();
            continue;
        }

        if (compiling_) {
            if (!compile_token(word, iss))
                line_had_error_ = true;
        } else {
            if (!interpret_token(word, iss))
                line_had_error_ = true;
        }
    }

    ctx_.set_input_stream(nullptr);
}

bool Interpreter::evaluate_string(const std::string& code) {
    // Save the current input stream (may be non-null if called mid-line)
    auto* saved_stream = ctx_.input_stream();
    SourceContext saved_ctx = source_context_;
    source_context_ = SourceContext::Evaluate;

    interpret_line(code);

    // Restore the outer input stream and source context
    ctx_.set_input_stream(saved_stream);
    source_context_ = saved_ctx;

    // Detect unterminated definition (same logic as load_file)
    if (compiling_) {
        err_ << "Error: unterminated definition '" << compiling_word_name_
             << "' in evaluate\n";
        abandon_definition();
        return false;
    }

    return true;
}

bool Interpreter::load_file(const std::string& path) {
    // Resolve logical paths (/home/..., /library/...)
    std::string resolved = resolve_logical_path(path);
    if (resolved.empty()) {
        err_ << "Error: path traversal rejected for '" << path << "'\n";
        return false;
    }

    std::ifstream file(resolved);
    if (!file.is_open()) {
        err_ << "Error: cannot open file '" << resolved << "'\n";
        return false;
    }

    // Save/restore source context for nested includes
    std::string saved_file = source_file_;
    size_t saved_line = source_line_;
    SourceContext saved_ctx = source_context_;

    // Extract just the filename for display (strip directory path)
    auto slash = resolved.rfind('/');
    source_file_ = (slash != std::string::npos)
                    ? resolved.substr(slash + 1)
                    : resolved;
    source_line_ = 0;
    source_context_ = SourceContext::Include;

    std::string line;
    while (std::getline(file, line)) {
        ++source_line_;
        size_t depth_before = ctx_.data_stack().size();
        interpret_line(line);
        // If any token on this line failed and orphaned heap values on the
        // stack (e.g., s" pushed strings for a meta! that never executed),
        // release them so they don't leak or corrupt subsequent lines.
        if (line_had_error_ && ctx_.data_stack().size() > depth_before) {
            while (ctx_.data_stack().size() > depth_before) {
                auto v = ctx_.data_stack().pop();
                if (v) v->release();
            }
        }
    }

    if (compiling_) {
        err_ << "Error: unterminated definition '" << compiling_word_name_
             << "' at " << source_file_ << ":" << source_line_ << "\n";
        abandon_definition();
        source_file_ = saved_file;
        source_line_ = saved_line;
        source_context_ = saved_ctx;
        return false;
    }

    source_file_ = saved_file;
    source_line_ = saved_line;
    source_context_ = saved_ctx;
    return true;
}

void Interpreter::register_handler_words() {
    dict_.register_handler_word(":");
    for (const auto& w : interpret_handlers_.words())
        dict_.register_handler_word(w);
    for (const auto& w : compile_handlers_.words())
        dict_.register_handler_word(w);
    for (const auto& w : control_flow_handlers_.words())
        dict_.register_handler_word(w);
}

bool Interpreter::load_startup_files(const std::vector<std::string>& paths) {
    for (const auto& raw_path : paths) {
        std::string path = raw_path;
        // Expand leading ~ to $HOME
        if (!path.empty() && path[0] == '~') {
            const char* home = std::getenv("HOME");
            if (home && home[0] != '\0') {
                path = std::string(home) + path.substr(1);
            }
        }
        if (!load_file(path)) {
            return false;
        }
    }
    return true;
}

std::string Interpreter::format_value(const Value& v) {
    if (v.type == Value::Type::Integer) {
        return std::to_string(v.as_int);
    } else if (v.type == Value::Type::Float) {
        std::ostringstream oss;
        oss << v.as_float;
        return oss.str();
    } else if (v.type == Value::Type::Boolean) {
        return v.as_bool() ? "true" : "false";
    } else if (v.type == Value::Type::String) {
        if (v.as_ptr) {
            auto* hs = v.as_string();
            return std::string("\"") + std::string(hs->view()) + "\"";
        }
        return "\"\"";
    } else if (v.type == Value::Type::Array) {
        return "[array]";
    } else if (v.type == Value::Type::ByteArray) {
        return "<bytes>";
    } else if (v.type == Value::Type::Map) {
        if (v.as_ptr) {
            auto* m = v.as_map();
            return "{map:" + std::to_string(m->size()) + "}";
        }
        return "{map:0}";
    } else if (v.type == Value::Type::Matrix) {
        if (v.as_ptr) {
            auto* mat = v.as_matrix();
            return "{matrix:" + std::to_string(mat->rows()) + "x" + std::to_string(mat->cols()) + "}";
        }
        return "{matrix:0x0}";
    } else if (v.type == Value::Type::Json) {
        return "<json>";
    } else if (v.type == Value::Type::Observable) {
        if (v.as_ptr) {
            return "<observable:" + std::string(v.as_observable()->kind_name()) + ">";
        }
        return "<observable>";
    } else if (v.type == Value::Type::Xt) {
        if (v.as_ptr) {
            auto* impl = v.as_xt_impl();
            return "<xt:" + impl->name() + ">";
        }
        return "<xt:?>";
    } else if (v.type == Value::Type::DataRef) {
        return "dataref";
    }
    return "?";
}

std::vector<std::string> Interpreter::completable_words() const {
    auto names = dict_.word_names();
    names.emplace_back(":");  // mode-switching, not in any handler set
    for (const auto& w : interpret_handlers_.words()) names.push_back(w);
    for (const auto& w : compile_handlers_.words()) names.push_back(w);
    for (const auto& w : control_flow_handlers_.words()) names.push_back(w);
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    return names;
}

void Interpreter::print_all_words() const {
    auto names = completable_words();
    for (const auto& name : names) {
        out_ << name << " ";
    }
    out_ << "\n";
}

std::string Interpreter::stack_status(size_t max_show) const {
    const auto& stack = ctx_.data_stack();
    size_t depth = stack.size();

    std::ostringstream oss;
    oss << "(" << depth << ")";

    // Show top elements (stack[size-1] is TOS)
    size_t show = std::min(depth, max_show);
    // Display from TOS downward: elements[depth-1], [depth-2], ...
    for (size_t i = 0; i < show; ++i) {
        oss << " " << format_value(stack[depth - 1 - i]);
    }
    if (depth > max_show) {
        oss << " ...";
    }

    return oss.str();
}

// --- Private: dispatch ---

bool Interpreter::interpret_token(const std::string& token,
                                   std::istringstream& iss) {
    auto result = interpret_handlers_.dispatch(token, iss);
    if (result) return *result;

    // Try number
    if (try_push_number(token)) {
        return true;
    }

    // Dictionary lookup
    auto lookup = dict_.lookup(token);
    if (!lookup) {
        err_ << "Unknown word: " << token;
        if (!source_file_.empty())
            err_ << " at " << source_file_ << ":" << source_line_;
        err_ << "\n";
        return false;
    }
    auto& impl = *lookup;
    if (!execute_word(impl.get())) {
        err_ << "Error executing '" << token << "'";
        if (!source_file_.empty())
            err_ << " at " << source_file_ << ":" << source_line_;
        err_ << "\n";
        return false;
    }
    return true;
}

bool Interpreter::compile_token(const std::string& token,
                                 std::istringstream& iss) {
    // Compile-mode words (;, does>, .", s")
    if (auto c_result = compile_handlers_.dispatch(token, iss)) return *c_result;

    // Control flow words (if, else, then, do, loop, etc.)
    if (auto cf_result = control_flow_handlers_.dispatch(token)) {
        if (!*cf_result) {
            abandon_definition();
            return false;
        }
        return true;
    }

    // Number literal
    Instruction num_instr;
    if (try_compile_number(token, num_instr)) {
        current_bytecode_->append(std::move(num_instr));
        return true;
    }

    // Word call — verify the word exists before compiling.
    // Allow self-reference (recursion) for the word currently being defined.
    auto lookup = dict_.lookup(token);
    if (token != compiling_word_name_ && !lookup) {
        err_ << "Error: unknown word '" << token << "' in definition of '"
             << compiling_word_name_ << "'";
        if (!source_file_.empty())
            err_ << " at " << source_file_ << ":" << source_line_;
        err_ << "\n";
        abandon_definition();
        return false;
    }

    // Immediate words execute during compilation instead of being compiled
    if (lookup && (*lookup)->immediate()) {
        return execute_word(lookup->get());
    }

    Instruction call;
    call.op = Instruction::Op::Call;
    call.word_name = token;
    current_bytecode_->append(std::move(call));
    return true;
}

// --- Private: definition lifecycle ---

void Interpreter::finalize_definition() {
    auto id = Dictionary::next_id();
    auto* impl = new WordImpl(compiling_word_name_, id);
    impl->set_bytecode(current_bytecode_);
    impl->set_weight(1.0);
    impl->set_generation(0);
    dict_.register_word(compiling_word_name_, WordImplPtr(impl));
    ctx_.set_last_created(impl);  // for immediate, does>, etc.

    // Store definition source metadata on the implementation (not concept)
    // so different impls of the same word track their own source locations.
    switch (source_context_) {
    case SourceContext::Include:
        impl->mark_as_include(source_file_, compiling_start_line_);
        break;
    case SourceContext::Evaluate:
        impl->mark_as_evaluate();
        break;
    case SourceContext::Interactive:
        impl->mark_as_interpret();
        break;
    }

    compiling_ = false;
    current_bytecode_.reset();
    control_stack_.clear();
}

void Interpreter::abandon_definition() {
    compiling_ = false;
    compiling_start_line_ = 0;
    current_bytecode_.reset();
    control_stack_.clear();
}

// --- Private: execution ---

bool Interpreter::execute_word(WordImpl* impl) {
    if (impl->native_code()) {
        return impl->native_code()(ctx_);
    }
    if (impl->bytecode()) {
        if (!ctx_.enter_call()) {
            err_ << "Error: maximum call depth exceeded\n";
            return false;
        }
        bool ok = execute_compiled(*impl->bytecode(), ctx_);
        ctx_.exit_call();
        return ok;
    }
    return false;
}

// --- Private: utilities ---

bool Interpreter::try_push_number(const std::string& token) {
    // Try integer
    try {
        size_t pos = 0;
        int64_t num = std::stoll(token, &pos);
        if (pos == token.size()) {
            ctx_.data_stack().push(Value(num));
            return true;
        }
    } catch (...) {}

    // Try float
    try {
        size_t pos = 0;
        double num = std::stod(token, &pos);
        if (pos == token.size()) {
            ctx_.data_stack().push(Value(num));
            return true;
        }
    } catch (...) {}

    return false;
}

bool Interpreter::try_compile_number(const std::string& word,
                                      Instruction& instr) {
    // Try integer
    try {
        size_t pos = 0;
        int64_t num = std::stoll(word, &pos);
        if (pos == word.size()) {
            instr.op = Instruction::Op::PushInt;
            instr.int_val = num;
            return true;
        }
    } catch (...) {}

    // Try float
    try {
        size_t pos = 0;
        double num = std::stod(word, &pos);
        if (pos == word.size()) {
            instr.op = Instruction::Op::PushFloat;
            instr.float_val = num;
            return true;
        }
    } catch (...) {}

    return false;
}

} // namespace etil::core
