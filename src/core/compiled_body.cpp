// Copyright (c) 2026 Mark Deazley. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#include "etil/core/compiled_body.hpp"
#include "etil/core/execution_context.hpp"
#include "etil/core/dictionary.hpp"
#include "etil/core/heap_json.hpp"
#include "etil/core/heap_object.hpp"
#include "etil/core/heap_string.hpp"

#include <nlohmann/json.hpp>

#include <iostream>
#include <vector>

namespace etil::core {

bool execute_compiled(ByteCode& code, ExecutionContext& ctx) {
    auto& instrs = code.instructions();
    size_t ip = 0;
    std::vector<Value> local_rstack;  // Local return stack for DO/LOOP params

    while (ip < instrs.size()) {
        if (!ctx.tick()) {
            if (!ctx.abort_requested())
                ctx.err() << "Error: execution limit reached\n";
            return false;
        }
        auto& instr = instrs[ip];

        switch (instr.op) {
        case Instruction::Op::PushInt:
            ctx.data_stack().push(Value(instr.int_val));
            ++ip;
            break;

        case Instruction::Op::PushFloat:
            ctx.data_stack().push(Value(instr.float_val));
            ++ip;
            break;

        case Instruction::Op::Call: {
            // Use cached impl if available and generation matches,
            // otherwise look up and cache.  The generation check
            // detects invalidation caused by forget_word/forget_all.
            auto* dict = ctx.dictionary();
            if (!dict) return false;
            if (!instr.cached_impl ||
                dict->generation() != instr.cached_generation) {
                auto result = dict->lookup(instr.word_name);
                if (!result) {
                    ctx.err() << "Unknown word: " << instr.word_name << "\n";
                    return false;
                }
                instr.cached_impl = result->get();
                instr.cached_generation = dict->generation();
            }
            auto* impl = instr.cached_impl;
            if (impl->native_code()) {
                if (!impl->native_code()(ctx)) {
                    ctx.err() << "Error in '" << instr.word_name
                              << "' (stack depth: "
                              << ctx.data_stack().size() << ")\n";
                    return false;
                }
            } else if (impl->bytecode()) {
                if (!ctx.enter_call()) {
                    ctx.err() << "Error: maximum call depth exceeded\n";
                    return false;
                }
                bool ok = execute_compiled(*impl->bytecode(), ctx);
                ctx.exit_call();
                if (!ok) {
                    if (!ctx.abort_requested()) {
                        ctx.err() << "  in '" << instr.word_name << "'";
                        if (impl) {
                            auto sf = impl->source_file();
                            auto sl = impl->source_line();
                            if (sf && sl) {
                                ctx.err() << " (defined at " << *sf << ":" << *sl << ")";
                            }
                        }
                        ctx.err() << "\n";
                    }
                    return false;
                }
            } else {
                ctx.err() << "Word '" << instr.word_name
                          << "' has no executable code\n";
                return false;
            }
            ++ip;
            break;
        }

        case Instruction::Op::Branch:
            ip = static_cast<size_t>(instr.int_val);
            break;

        case Instruction::Op::PushBool:
            ctx.data_stack().push(Value(instr.int_val != 0));
            ++ip;
            break;

        case Instruction::Op::BranchIfFalse: {
            auto flag = ctx.data_stack().pop();
            if (!flag) return false;
            if (flag->type != Value::Type::Boolean) {
                ctx.err() << "Error: if/while/until requires boolean, got "
                          << (flag->type == Value::Type::Integer ? "integer" :
                              flag->type == Value::Type::Float ? "float" : "non-boolean")
                          << "\n";
                return false;
            }
            if (!flag->as_bool()) {
                ip = static_cast<size_t>(instr.int_val);
            } else {
                ++ip;
            }
            break;
        }

        case Instruction::Op::DoSetup: {
            // ( limit index -- )  push to local return stack
            auto opt_index = ctx.data_stack().pop();
            if (!opt_index) return false;
            auto opt_limit = ctx.data_stack().pop();
            if (!opt_limit) {
                ctx.data_stack().push(*opt_index);
                return false;
            }
            local_rstack.push_back(*opt_limit);  // limit deeper
            local_rstack.push_back(*opt_index);   // index on top
            ++ip;
            break;
        }

        case Instruction::Op::DoLoop: {
            if (local_rstack.size() < 2) return false;
            int64_t index = local_rstack.back().as_int + 1;
            int64_t limit = local_rstack[local_rstack.size() - 2].as_int;
            if (index >= limit) {
                // Loop done
                local_rstack.pop_back();
                local_rstack.pop_back();
                ++ip;
            } else {
                local_rstack.back() = Value(index);
                ip = static_cast<size_t>(instr.int_val);
            }
            break;
        }

        case Instruction::Op::DoPlusLoop: {
            auto opt_inc = ctx.data_stack().pop();
            if (!opt_inc) return false;
            if (local_rstack.size() < 2) return false;
            int64_t increment = opt_inc->as_int;
            if (increment == 0) {
                ctx.err() << "Error: +LOOP increment is zero\n";
                return false;
            }
            int64_t index = local_rstack.back().as_int;
            int64_t limit = local_rstack[local_rstack.size() - 2].as_int;
            int64_t new_index = index + increment;
            // Standard FORTH +LOOP: terminates when index crosses the boundary
            bool crossed = (increment > 0)
                           ? (index < limit && new_index >= limit)
                           : (index >= limit && new_index < limit);
            if (crossed) {
                local_rstack.pop_back();
                local_rstack.pop_back();
                ++ip;
            } else {
                local_rstack.back() = Value(new_index);
                ip = static_cast<size_t>(instr.int_val);
            }
            break;
        }

        case Instruction::Op::DoI: {
            if (local_rstack.empty()) return false;
            ctx.data_stack().push(local_rstack.back());
            ++ip;
            break;
        }

        case Instruction::Op::ToR: {
            auto opt = ctx.data_stack().pop();
            if (!opt) return false;
            local_rstack.push_back(*opt);
            ++ip;
            break;
        }

        case Instruction::Op::FromR: {
            if (local_rstack.empty()) {
                ctx.err() << "Error: return stack underflow\n";
                return false;
            }
            ctx.data_stack().push(local_rstack.back());
            local_rstack.pop_back();
            ++ip;
            break;
        }

        case Instruction::Op::FetchR: {
            if (local_rstack.empty()) {
                ctx.err() << "Error: return stack underflow\n";
                return false;
            }
            ctx.data_stack().push(local_rstack.back());
            ++ip;
            break;
        }

        case Instruction::Op::DoJ: {
            // Outer loop index: local_rstack layout is
            // [..., outer_limit, outer_index, inner_limit, inner_index]
            // So outer_index is at size()-3
            if (local_rstack.size() < 4) {
                ctx.err() << "Error: j requires nested DO loop\n";
                return false;
            }
            ctx.data_stack().push(local_rstack[local_rstack.size() - 3]);
            ++ip;
            break;
        }

        case Instruction::Op::DoLeave: {
            // Pop current loop frame (limit + index) and jump past loop
            if (local_rstack.size() < 2) return false;
            local_rstack.pop_back();  // index
            local_rstack.pop_back();  // limit
            ip = static_cast<size_t>(instr.int_val);
            break;
        }

        case Instruction::Op::DoExit: {
            return true;
        }

        case Instruction::Op::PrintString:
            ctx.out() << instr.word_name;
            ++ip;
            break;

        case Instruction::Op::PushString: {
            auto* hs = HeapString::create(instr.word_name);
            ctx.data_stack().push(Value::from(hs));
            ++ip;
            break;
        }

        case Instruction::Op::PushJson: {
            try {
                auto j = nlohmann::json::parse(instr.word_name);
                auto* hj = new HeapJson(std::move(j));
                ctx.data_stack().push(Value::from(hj));
            } catch (const nlohmann::json::parse_error& e) {
                ctx.err() << "Error: invalid JSON in PushJson: " << e.what() << "\n";
                return false;
            }
            ++ip;
            break;
        }

        case Instruction::Op::PushDataPtr: {
            // Lazy-register data field on first execution
            if (code.registry_index() < 0) {
                auto reg_ptr = ctx.data_field_registry_ptr();
                auto idx = reg_ptr->register_field(&code.data_field());
                code.set_registry_index(static_cast<int64_t>(idx));
                code.set_registry(reg_ptr);
            }
            ctx.data_stack().push(make_dataref(
                static_cast<uint32_t>(code.registry_index()), 0));
            ++ip;
            break;
        }

        case Instruction::Op::PushXt: {
            // Use cached impl if available and generation matches,
            // otherwise look up and cache.
            auto* dict = ctx.dictionary();
            if (!dict) return false;
            if (!instr.cached_impl ||
                dict->generation() != instr.cached_generation) {
                auto result = dict->lookup(instr.word_name);
                if (!result) {
                    ctx.err() << "Unknown word: " << instr.word_name << "\n";
                    return false;
                }
                instr.cached_impl = result->get();
                instr.cached_generation = dict->generation();
            }
            auto* impl = instr.cached_impl;
            impl->add_ref();
            ctx.data_stack().push(Value::from_xt(impl));
            ++ip;
            break;
        }

        case Instruction::Op::SetDoes: {
            // Get the last CREATE'd word and attach does>-body to it
            auto* last = ctx.last_created();
            if (!last) {
                ctx.err() << "does> error: no CREATE'd word\n";
                return false;
            }

            // Build new bytecode for the CREATE'd word:
            // PushDataPtr + instructions from int_val to end of current code
            auto does_code = std::make_shared<ByteCode>();

            // First instruction: push the data field pointer
            Instruction push_ptr;
            push_ptr.op = Instruction::Op::PushDataPtr;
            does_code->append(std::move(push_ptr));

            // Copy remaining instructions (from offset to end)
            size_t does_start = static_cast<size_t>(instr.int_val);
            for (size_t j = does_start; j < instrs.size(); ++j) {
                Instruction copy = instrs[j];
                // Adjust branch targets relative to the new code
                if (copy.op == Instruction::Op::Branch ||
                    copy.op == Instruction::Op::BranchIfFalse ||
                    copy.op == Instruction::Op::DoLoop ||
                    copy.op == Instruction::Op::DoPlusLoop ||
                    copy.op == Instruction::Op::DoLeave) {
                    // Offset: original target - does_start + 1 (for PushDataPtr)
                    copy.int_val = copy.int_val - static_cast<int64_t>(does_start) + 1;
                }
                does_code->append(std::move(copy));
            }

            // Transfer the data_field from last's existing bytecode
            auto* old_code = last->bytecode().get();
            if (old_code) {
                does_code->data_field() = std::move(old_code->data_field());

                // Transfer registry info: update pointer to new data field,
                // move index/backpointer to new ByteCode, clear old
                if (old_code->registry() && old_code->registry_index() >= 0) {
                    auto reg_ptr = old_code->registry_ptr();
                    auto idx = old_code->registry_index();
                    reg_ptr->update(static_cast<uint32_t>(idx),
                                    &does_code->data_field());
                    does_code->set_registry_index(idx);
                    does_code->set_registry(reg_ptr);
                    old_code->set_registry_index(-1);
                    old_code->set_registry({});
                }
            }

            last->set_bytecode(does_code);
            // Stop executing this word — everything after SetDoes is the does>-body
            return true;
        }

        default:
            return false;
        }
    }
    return true;
}

} // namespace etil::core
