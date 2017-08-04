/**
 * Copyright (c) 2016-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#include "RedundantMoveEliminationPass.h"

#include <boost/optional.hpp>

#include "AliasedRegisters.h"
#include "ControlFlow.h"
#include "DexUtil.h"
#include "FixpointIterators.h"
#include "IRInstruction.h"
#include "ParallelWalkers.h"
#include "PassManager.h"

/**
 * This pass eliminates writes to registers that already hold the written value.
 *
 * It's more commonly known as Copy Propagation.
 *
 * For example,
 *   move-object/from16 v0, v33
 *   iget-object v2, v0, LX/04b;.a:Landroid/content/Context; // field@05d6
 *   move-object/from16 v0, v33
 *   iget-object v3, v0, LX/04b;.b:Ljava/lang/String; // field@05d7
 *   move-object/from16 v0, v33
 *   iget-object v4, v0, LX/04b;.c:LX/04K; // field@05d8
 *   move-object/from16 v0, v33
 *
 * It keeps moving v33 to v0 even though they hold the same object!
 *
 * This optimization transforms the above code to this:
 *   move-object/from16 v0, v33
 *   iget-object v2, v0, LX/04b;.a:Landroid/content/Context; // field@05d6
 *   iget-object v3, v0, LX/04b;.b:Ljava/lang/String; // field@05d7
 *   iget-object v4, v0, LX/04b;.c:LX/04K; // field@05d8
 *
 * It does so by examinining all the writes to registers in a basic block, if vA
 * is moved into vB, then vA and vB are aliases until one of them is written
 * with a different value. Any move between registers that are already aliased
 * is unneccesary. Eliminate them.
 *
 * It can also do the same thing with constant loads, if enabled by the config.
 *
 * This optimization also will replace source registers with a representative
 * register (a whole alias group has a single representative). The reason is
 * that if we use fewer registers, DCE could clean up some more moves after
 * us.
 *
 * Possible additions: (TODO?)
 *   wide registers
 *     I tried it, with registers only it's a tiny help. With
 *     constants it causes spurious verify errors at install time. --jhendrick
 */

namespace {

struct RedundantStats {
  size_t moves_eliminated{0};
  size_t replaced_sources{0};

  RedundantStats() = default;
  RedundantStats(size_t elim, size_t replaced) : moves_eliminated(elim), replaced_sources(replaced) {}

  RedundantStats operator+(const RedundantStats& other);
};

RedundantStats RedundantStats::operator+(const RedundantStats& other) {
  return RedundantStats{moves_eliminated + other.moves_eliminated,
                        replaced_sources + other.replaced_sources};
}

class AliasFixpointIterator final
    : public MonotonicFixpointIterator<Block*, AliasDomain> {
 public:
  const RedundantMoveEliminationPass::Config& m_config;
  RedundantStats& m_stats;

  using BlockToListFunc = std::function<std::vector<Block*>(Block* const&)>;

  AliasFixpointIterator(Block* start_block,
                        BlockToListFunc succ,
                        BlockToListFunc pred,
                        const RedundantMoveEliminationPass::Config& config,
                        RedundantStats& stats)
      : MonotonicFixpointIterator<Block*, AliasDomain>(start_block, succ, pred),
        m_config(config),
        m_stats(stats) {}

  /*
   * if deletes is not null, this time is for real.
   * fill the `deletes` vector with redundant instructions
   *
   * if deletes is null, analyze only. make no changes.
   *
   * An instruction can be removed if we know the source and destination are
   * aliases
   */
  void run_on_block(Block* const& block,
                    AliasedRegisters& aliases,
                    std::vector<IRInstruction*>* deletes) const {

    for (auto& mei : InstructionIterable(block)) {
      const RegisterValue& src = get_src_value(mei.insn);
      if (src != RegisterValue::none()) {
        // either a move or a constant load into `dst`
        RegisterValue dst{mei.insn->dest()};
        if (aliases.are_aliases(dst, src)) {
          if (deletes != nullptr) {
            deletes->push_back(mei.insn);
          }
        } else {
          aliases.break_alias(dst);
          aliases.make_aliased(dst, src);
        }
      } else {
        if (m_config.replace_with_representative && deletes != nullptr) {
          replace_with_representative(mei.insn, aliases);
        }
        if (mei.insn->dests_size() > 0) {
          // dest is being written to but not by a simple move from another
          // register or a constant load. Break its aliases because we don't
          // know what its value is.
          RegisterValue dst{mei.insn->dest()};
          aliases.break_alias(dst);
          if (mei.insn->dest_is_wide()) {
            Register wide_reg = mei.insn->dest() + 1;
            RegisterValue wide{wide_reg};
            aliases.break_alias(wide);
          }
        }
      }
    }
  }

  // Each group of aliases has one representative register.
  // Try to replace source registers with their representative.
  void replace_with_representative(IRInstruction* insn,
                                   AliasedRegisters& aliases) const {
    if (insn->srcs_size() > 0 &&
        !opcode::has_range(insn->opcode()) && // range has to stay in order
        // we need to make sure the dest and src of check-cast stay identical,
        // because the dest is simply an alias to the src. See the comments in
        // IRInstruction.h for details.
        insn->opcode() != OPCODE_CHECK_CAST && !is_monitor(insn->opcode())) {
      for (size_t i = 0; i < insn->srcs_size(); ++i) {
        RegisterValue val{insn->src(i)};
        boost::optional<Register> rep = aliases.get_representative(val);
        if (rep) {
          // filter out uses of wide registers where the second register isn't
          // also aliased
          if (insn->src_is_wide(i)) {
            RegisterValue orig_wide{(Register)(insn->src(i) + 1)};
            RegisterValue rep_wide{(Register)(*rep + 1)};
            bool wides_are_aliased = aliases.are_aliases(orig_wide, rep_wide);
            if (!wides_are_aliased) {
              continue;
            }
          }

          if (insn->src(i) != *rep) {
            insn->set_src(i, *rep);
            m_stats.replaced_sources++;
          }
        }
      }
    }
  }

  const RegisterValue get_src_value(IRInstruction* insn) const {
    switch (insn->opcode()) {
    case OPCODE_MOVE:
    case OPCODE_MOVE_FROM16:
    case OPCODE_MOVE_16:
    case OPCODE_MOVE_OBJECT:
    case OPCODE_MOVE_OBJECT_FROM16:
    case OPCODE_MOVE_OBJECT_16:
      return RegisterValue{insn->src(0)};
    case OPCODE_CONST:
    case OPCODE_CONST_4:
    case OPCODE_CONST_16:
      if (m_config.eliminate_const_literals) {
        return RegisterValue{insn->literal()};
      } else {
        return RegisterValue::none();
      }
    case OPCODE_CONST_STRING:
    case OPCODE_CONST_STRING_JUMBO: {
      if (m_config.eliminate_const_strings) {
        DexString* str = insn->get_string();
        return RegisterValue{str};
      } else {
        return RegisterValue::none();
      }
    }
    case OPCODE_CONST_CLASS: {
      if (m_config.eliminate_const_classes) {
        DexType* type = insn->get_type();
        return RegisterValue{type};
      } else {
        return RegisterValue::none();
      }
    }
    default:
      return RegisterValue::none();
    }
  }

  void analyze_node(Block* const& node,
                    AliasDomain* current_state) const override {
    current_state->update([&](AliasedRegisters& aliases) {
      run_on_block(node, aliases, nullptr);
    });
  }

  AliasDomain analyze_edge(
      Block* const& /* source */,
      Block* const& /* target */,
      const AliasDomain& exit_state_at_source) const override {
    return exit_state_at_source;
  }
};

class RedundantMoveEliminationImpl final {
 public:
  explicit RedundantMoveEliminationImpl(
      const RedundantMoveEliminationPass::Config& config)
      : m_config(config) {}

  RedundantStats run(Scope scope) {
    using Data = std::nullptr_t;
    using Output = RedundantStats;
    return walk_methods_parallel<Scope, Data, Output>(
        scope,
        [this](Data&, DexMethod* m) {
          if (m->get_code()) {
            return run_on_method(m);
          }
          return RedundantStats();
        },
        [](Output a, Output b) { return a + b; },
        [](unsigned int /*thread_index*/) { return nullptr; });
  }

 private:
  const RedundantMoveEliminationPass::Config& m_config;

  RedundantStats run_on_method(DexMethod* method) {
    std::vector<IRInstruction*> deletes;
    RedundantStats stats;

    auto code = method->get_code();
    code->build_cfg();
    const auto& blocks = code->cfg().blocks();

    AliasFixpointIterator fixpoint(
        code->cfg().entry_block(),
        [](Block* const& block) { return block->succs(); },
        [](Block* const& block) { return block->preds(); },
        m_config,
        stats);

    if (m_config.full_method_analysis) {
      fixpoint.run(AliasDomain());
      for (auto block : blocks) {
        AliasDomain domain = fixpoint.get_entry_state_at(block);
        domain.update([&fixpoint, block, &deletes](AliasedRegisters& aliases) {
          fixpoint.run_on_block(block, aliases, &deletes);
        });
      }
    } else {
      for (auto block : blocks) {
        AliasedRegisters aliases;
        fixpoint.run_on_block(block, aliases, &deletes);
      }
    }

    stats.moves_eliminated += deletes.size();
    for (auto insn : deletes) {
      code->remove_opcode(insn);
    }
    return stats;
  }
};
}

void RedundantMoveEliminationPass::run_pass(DexStoresVector& stores,
                                            ConfigFiles& /* unused */,
                                            PassManager& mgr) {
  auto scope = build_class_scope(stores);
  RedundantMoveEliminationImpl impl(m_config);
  auto stats = impl.run(scope);
  mgr.incr_metric("redundant_moves_eliminated", stats.moves_eliminated);
  mgr.incr_metric("source_regs_replaced_with_representative",
                  stats.replaced_sources);
  TRACE(RME,
        1,
        "%d redundant moves eliminated\n",
        mgr.get_metric("redundant_moves_eliminated"));
  TRACE(RME,
        1,
        "%d source registers replaced with representative\n",
        mgr.get_metric("source_regs_replaced_with_representative"));
}

static RedundantMoveEliminationPass s_pass;
