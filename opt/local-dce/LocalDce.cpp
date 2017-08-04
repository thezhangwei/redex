/**
 * Copyright (c) 2016-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#include "LocalDce.h"

#include <iostream>
#include <array>
#include <unordered_set>
#include <vector>

#include <boost/dynamic_bitset.hpp>

#include "ControlFlow.h"
#include "DexClass.h"
#include "IRInstruction.h"
#include "DexUtil.h"
#include "Transform.h"
#include "Walkers.h"

namespace {

constexpr const char* METRIC_DEAD_INSTRUCTIONS = "num_dead_instructions";
constexpr const char* METRIC_UNREACHABLE_INSTRUCTIONS =
    "num_unreachable_instructions";

/*
 * These instructions have observable side effects so must always be considered
 * live, regardless of whether their output is consumed by another instruction.
 */
static bool has_side_effects(DexOpcode opc) {
  switch (opc) {
  case OPCODE_RETURN_VOID:
  case OPCODE_RETURN:
  case OPCODE_RETURN_WIDE:
  case OPCODE_RETURN_OBJECT:
  case OPCODE_MONITOR_ENTER:
  case OPCODE_MONITOR_EXIT:
  case OPCODE_CHECK_CAST:
  case OPCODE_FILL_ARRAY_DATA:
  case OPCODE_THROW:
  case OPCODE_GOTO:
  case OPCODE_GOTO_16:
  case OPCODE_GOTO_32:
  case OPCODE_PACKED_SWITCH:
  case OPCODE_SPARSE_SWITCH:
  case OPCODE_IF_EQ:
  case OPCODE_IF_NE:
  case OPCODE_IF_LT:
  case OPCODE_IF_GE:
  case OPCODE_IF_GT:
  case OPCODE_IF_LE:
  case OPCODE_IF_EQZ:
  case OPCODE_IF_NEZ:
  case OPCODE_IF_LTZ:
  case OPCODE_IF_GEZ:
  case OPCODE_IF_GTZ:
  case OPCODE_IF_LEZ:
  case OPCODE_APUT:
  case OPCODE_APUT_WIDE:
  case OPCODE_APUT_OBJECT:
  case OPCODE_APUT_BOOLEAN:
  case OPCODE_APUT_BYTE:
  case OPCODE_APUT_CHAR:
  case OPCODE_APUT_SHORT:
  case OPCODE_IPUT:
  case OPCODE_IPUT_WIDE:
  case OPCODE_IPUT_OBJECT:
  case OPCODE_IPUT_BOOLEAN:
  case OPCODE_IPUT_BYTE:
  case OPCODE_IPUT_CHAR:
  case OPCODE_IPUT_SHORT:
  case OPCODE_SPUT:
  case OPCODE_SPUT_WIDE:
  case OPCODE_SPUT_OBJECT:
  case OPCODE_SPUT_BOOLEAN:
  case OPCODE_SPUT_BYTE:
  case OPCODE_SPUT_CHAR:
  case OPCODE_SPUT_SHORT:
  case OPCODE_INVOKE_VIRTUAL:
  case OPCODE_INVOKE_SUPER:
  case OPCODE_INVOKE_DIRECT:
  case OPCODE_INVOKE_STATIC:
  case OPCODE_INVOKE_INTERFACE:
  case OPCODE_INVOKE_VIRTUAL_RANGE:
  case OPCODE_INVOKE_SUPER_RANGE:
  case OPCODE_INVOKE_DIRECT_RANGE:
  case OPCODE_INVOKE_STATIC_RANGE:
  case OPCODE_INVOKE_INTERFACE_RANGE:
  case FOPCODE_PACKED_SWITCH:
  case FOPCODE_SPARSE_SWITCH:
  case FOPCODE_FILLED_ARRAY:
  case IOPCODE_LOAD_PARAM:
  case IOPCODE_LOAD_PARAM_OBJECT:
  case IOPCODE_LOAD_PARAM_WIDE:
    return true;
  default:
    return false;
  }
  not_reached();
}

template <typename... T>
std::string show(const boost::dynamic_bitset<T...>& bits) {
  std::string ret;
  to_string(bits, ret);
  return ret;
}

////////////////////////////////////////////////////////////////////////////////

class LocalDce {
 public:
  struct Stats {
    size_t dead_instruction_count{0};
    size_t unreachable_instruction_count{0};
  };

  /*
   * Eliminate dead code using a standard backward dataflow analysis for
   * liveness.  The algorithm is as follows:
   *
   * - Maintain a bitvector for each block representing the liveness for each
   *   register.  Function call results are represented by bit #num_regs.
   *
   * - Walk the blocks in postorder. Compute each block's output state by
   *   OR-ing the liveness of its successors
   *
   * - Walk each block's instructions in reverse to determine its input state.
   *   An instruction's input registers are live if (a) it has side effects, or
   *   (b) its output registers are live.
   *
   * - If the liveness of any block changes during a pass, repeat it.  Since
   *   anything live in one pass is guaranteed to be live in the next, this is
   *   guaranteed to reach a fixed point and terminate.  Visiting blocks in
   *   postorder guarantees a minimum number of passes.
   *
   * - Catch blocks are handled slightly differently; since any instruction
   *   inside a `try` region can jump to a catch block, we assume that any
   *   registers that are live-in to a catch block must be kept live throughout
   *   the `try` region.  (This is actually conservative, since only
   *   potentially-excepting instructions can jump to a catch.)
   */
 public:
  LocalDce() {
    /*
     * Pure methods have no observable side effects, so they can be removed
     * if their outputs are not used.
     *
     * TODO: Derive this list with static analysis rather than hard-coding
     * it.
     */
    m_pure_methods.emplace(DexMethod::make_method( "Ljava/lang/Class;",
          "getSimpleName", "Ljava/lang/String;", {}));
  }

  void dce(DexMethod* method) {
    auto code = method->get_code();
    code->build_cfg();
    auto& cfg = code->cfg();
    auto blocks = postorder_sort(cfg.blocks());
    auto regs = method->get_code()->get_registers_size();
    std::vector<boost::dynamic_bitset<>> liveness(
        cfg.blocks().size(), boost::dynamic_bitset<>(regs + 1));
    bool changed;
    std::vector<FatMethod::iterator> dead_instructions;

    TRACE(DCE, 5, "%s\n", SHOW(method));
    TRACE(DCE, 5, "%s", SHOW(cfg));

    // Iterate liveness analysis to a fixed point.
    do {
      changed = false;
      dead_instructions.clear();
      for (auto& b : blocks) {
        auto prev_liveness = liveness.at(b->id());
        auto& bliveness = liveness.at(b->id());
        bliveness.reset();
        TRACE(DCE, 5, "B%lu: %s\n", b->id(), show(bliveness).c_str());

        // Compute live-out for this block from its successors.
        for (auto& s : b->succs()) {
          if(s->id() == b->id()) {
            bliveness |= prev_liveness;
          }
          TRACE(DCE,
                5,
                "  S%lu: %s\n",
                s->id(),
                show(liveness[s->id()]).c_str());
          bliveness |= liveness[s->id()];
        }

        // Compute live-in for this block by walking its instruction list in
        // reverse and applying the liveness rules.
        for (auto it = b->rbegin(); it != b->rend(); ++it) {
          if (it->type != MFLOW_OPCODE) {
            continue;
          }
          bool required = is_required(it->insn, bliveness);
          if (required) {
            update_liveness(it->insn, bliveness);
          } else {
            dead_instructions.push_back(std::prev(it.base()));
          }
          TRACE(CFG,
                5,
                "%s\n%s\n",
                show(it->insn).c_str(),
                show(bliveness).c_str());
        }
        if (bliveness != prev_liveness) {
          changed = true;
        }
      }
    } while (changed);

    // Remove dead instructions.
    TRACE(DCE, 2, "%s\n", SHOW(method));
    for (auto dead : dead_instructions) {
      TRACE(DCE, 2, "DEAD: %s\n", SHOW(dead->insn));
      code->remove_opcode(dead);
    }
    m_stats.dead_instruction_count += dead_instructions.size();

    // if we deleted an instruction that may throw, we'll need to remove any
    // EDGE_THROW edges in the CFG... ideally we would just prune that edge,
    // but we can do a conservative and inefficient hack for now and just
    // rebuild the entire graph
    if (dead_instructions.size() > 0) {
      code->build_cfg();
    }

    m_stats.unreachable_instruction_count +=
        transform::remove_unreachable_blocks(code);
    remove_empty_try_regions(code);

    TRACE(DCE, 5, "=== Post-DCE CFG ===\n");
    TRACE(DCE, 5, "%s", SHOW(code->cfg()));
  }

  const Stats& get_stats() const {
    return m_stats;
  }

 private:
  void remove_empty_try_regions(IRCode* code) {
    // comb the method looking for superfluous try sections that do not enclose
    // throwing opcodes; remove them. note that try sections should never be
    // nested, otherwise this won't produce the right result.
    bool encloses_throw {false};
    MethodItemEntry* try_start {nullptr};
    for (auto& mie : *code) {
      if (mie.type == MFLOW_TRY) {
        auto tentry = mie.tentry;
        if (tentry->type == TRY_START) {
          encloses_throw = false;
          try_start = &mie;
        } else if (!encloses_throw /* && tentry->type == TRY_END */) {
          try_start->type = MFLOW_FALLTHROUGH;
          try_start->throwing_mie = nullptr;
          try_start = nullptr;
          mie.type = MFLOW_FALLTHROUGH;
          mie.throwing_mie = nullptr;
        }
      } else if (mie.type == MFLOW_OPCODE) {
        auto op = mie.insn->opcode();
        encloses_throw =
            encloses_throw || opcode::may_throw(op) || op == OPCODE_THROW;
      }
    }
  }

  /*
   * An instruction is required (i.e., live) if it has side effects or if its
   * destination register is live.
   */
  bool is_required(IRInstruction* inst, const boost::dynamic_bitset<>& bliveness) {
    if (has_side_effects(inst->opcode())) {
      if (is_invoke(inst->opcode())) {
        if (!is_pure(inst->get_method())) {
          return true;
        }
        return bliveness.test(bliveness.size() - 1);
      }
      return true;
    } else if (inst->dests_size()) {
      bool result = bliveness.test(inst->dest());
      if (inst->dest_is_wide()) {
        result |= bliveness.test(inst->dest() + 1);
      }
      return result;
    } else if (is_filled_new_array(inst->opcode())) {
      // filled-new-array passes its dest via the return-value slot, but isn't
      // inherently live like the invoke-* instructions.
      return bliveness.test(bliveness.size() - 1);
    }
    return false;
  }

  bool is_pure(DexMethod* method) {
    if (assumenosideeffects(method)) {
      return true;
    }
    return m_pure_methods.find(method) != m_pure_methods.end();
  }


  /*
   * Update the liveness vector given that `inst` is live.
   */
  void update_liveness(const IRInstruction* inst,
                       boost::dynamic_bitset<>& bliveness) {
    // The destination register is killed, so it isn't live before this.
    if (inst->dests_size()) {
      bliveness.reset(inst->dest());
      if (inst->dest_is_wide()) {
        bliveness.reset(inst->dest() + 1);
      }
    }
    // The destination of an `invoke` is its return value, which is encoded as
    // the max position in the bitvector.
    if (is_invoke(inst->opcode()) || is_filled_new_array(inst->opcode())) {
      bliveness.reset(bliveness.size() - 1);
    }
    // Source registers are live.
    for (size_t i = 0; i < inst->srcs_size(); i++) {
      bliveness.set(inst->src(i));
    }
    // `invoke-range` instructions need special handling since their sources
    // are encoded as a range.
    if (opcode::has_range(inst->opcode())) {
      for (size_t i = 0; i < inst->range_size(); i++) {
        bliveness.set(inst->range_base() + i);
      }
    }
    // The source of a `move-result` is the return value of the prior call,
    // which is encoded as the max position in the bitvector.
    if (is_move_result(inst->opcode())) {
      bliveness.set(bliveness.size() - 1);
    }
  }

 public:
  void run(const Scope& scope) {
    walk_methods(scope,
                 [&](DexMethod* m) {
                   if (!m->get_code()) {
                     return;
                   }
                   dce(m);
                 });
    TRACE(DCE, 1, "Dead instructions: %lu\n", m_stats.dead_instruction_count);
    TRACE(DCE, 1, "Unreachable instructions: %lu\n",
          m_stats.unreachable_instruction_count);
  }

 private:
  std::unordered_set<DexMethod*> m_pure_methods;
  Stats m_stats;
};

} // namespace

void LocalDcePass::run(DexMethod* m) {
  LocalDce().dce(m);
}

void LocalDcePass::run_pass(DexStoresVector& stores,
                            ConfigFiles& cfg,
                            PassManager& mgr) {
  if (mgr.no_proguard_rules()) {
    TRACE(DCE, 1,
        "LocalDcePass not run because no ProGuard configuration was provided.");
    return;
  }
  auto scope = build_class_scope(stores);
  LocalDce ldce;
  ldce.run(scope);
  const auto& stats = ldce.get_stats();
  mgr.incr_metric(METRIC_DEAD_INSTRUCTIONS, stats.dead_instruction_count);
  mgr.incr_metric(METRIC_UNREACHABLE_INSTRUCTIONS,
                  stats.unreachable_instruction_count);
}

static LocalDcePass s_pass;
