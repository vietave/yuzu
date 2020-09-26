// Copyright 2019 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <list>
#include <map>
#include <set>
#include <stack>
#include <unordered_map>
#include <vector>

#include "common/assert.h"
#include "common/common_types.h"
#include "video_core/shader/ast.h"
#include "video_core/shader/control_flow.h"
#include "video_core/shader/memory_util.h"
#include "video_core/shader/registry.h"
#include "video_core/shader/shader_ir.h"

namespace VideoCommon::Shader {

namespace {

using Tegra::Shader::Instruction;
using Tegra::Shader::OpCode;

constexpr s32 unassigned_branch = -2;

struct Query {
    u32 address{};
    std::stack<u32> ssy_stack{};
    std::stack<u32> pbk_stack{};
};

struct BlockStack {
    BlockStack() = default;
    explicit BlockStack(const Query& q) : ssy_stack{q.ssy_stack}, pbk_stack{q.pbk_stack} {}
    std::stack<u32> ssy_stack{};
    std::stack<u32> pbk_stack{};
};

template <typename T, typename... Args>
BlockBranchInfo MakeBranchInfo(Args&&... args) {
    static_assert(std::is_convertible_v<T, BranchData>);
    return std::make_shared<BranchData>(T(std::forward<Args>(args)...));
}

bool BlockBranchIsIgnored(BlockBranchInfo first) {
    bool ignore = false;
    if (std::holds_alternative<SingleBranch>(*first)) {
        const auto branch = std::get_if<SingleBranch>(first.get());
        ignore = branch->ignore;
    }
    return ignore;
}

struct BlockInfo {
    u32 start{};
    u32 end{};
    bool visited{};
    BlockBranchInfo branch{};

    bool IsInside(const u32 address) const {
        return start <= address && address <= end;
    }
};

struct CFGRebuildState {
    explicit CFGRebuildState(const ProgramCode& program_code, u32 start, Registry& registry)
        : program_code{program_code}, registry{registry}, start{start} {}

    const ProgramCode& program_code;
    Registry& registry;
    u32 start{};
    std::vector<BlockInfo> block_info;
    std::list<u32> inspect_queries;
    std::list<Query> queries;
    std::unordered_map<u32, u32> registered;
    std::set<u32> labels;
    std::map<u32, u32> ssy_labels;
    std::map<u32, u32> pbk_labels;
    std::unordered_map<u32, BlockStack> stacks;
    ASTManager* manager{};
};

enum class BlockCollision : u32 { None, Found, Inside };

std::pair<BlockCollision, u32> TryGetBlock(CFGRebuildState& state, u32 address) {
    const auto& blocks = state.block_info;
    for (u32 index = 0; index < blocks.size(); index++) {
        if (blocks[index].start == address) {
            return {BlockCollision::Found, index};
        }
        if (blocks[index].IsInside(address)) {
            return {BlockCollision::Inside, index};
        }
    }
    return {BlockCollision::None, 0xFFFFFFFF};
}

struct ParseInfo {
    BlockBranchInfo branch_info{};
    u32 end_address{};
};

BlockInfo& CreateBlockInfo(CFGRebuildState& state, u32 start, u32 end) {
    auto& it = state.block_info.emplace_back();
    it.start = start;
    it.end = end;
    const u32 index = static_cast<u32>(state.block_info.size() - 1);
    state.registered.insert({start, index});
    return it;
}

Pred GetPredicate(u32 index, bool negated) {
    return static_cast<Pred>(static_cast<u64>(index) + (negated ? 8ULL : 0ULL));
}

enum class ParseResult : u32 {
    ControlCaught,
    BlockEnd,
    AbnormalFlow,
};

struct BranchIndirectInfo {
    u32 buffer{};
    u32 offset{};
    u32 entries{};
    s32 relative_position{};
};

struct BufferInfo {
    u32 index;
    u32 offset;
};

std::optional<std::pair<s32, u64>> GetBRXInfo(const CFGRebuildState& state, u32& pos) {
    const Instruction instr = state.program_code[pos];
    const auto opcode = OpCode::Decode(instr);
    if (opcode->get().GetId() != OpCode::Id::BRX) {
        return std::nullopt;
    }
    if (instr.brx.constant_buffer != 0) {
        return std::nullopt;
    }
    --pos;
    return std::make_pair(instr.brx.GetBranchExtend(), instr.gpr8.Value());
}

template <typename Result, typename TestCallable, typename PackCallable>
// requires std::predicate<TestCallable, Instruction, const OpCode::Matcher&>
// requires std::invocable<PackCallable, Instruction, const OpCode::Matcher&>
std::optional<Result> TrackInstruction(const CFGRebuildState& state, u32& pos, TestCallable test,
                                       PackCallable pack) {
    for (; pos >= state.start; --pos) {
        if (IsSchedInstruction(pos, state.start)) {
            continue;
        }
        const Instruction instr = state.program_code[pos];
        const auto opcode = OpCode::Decode(instr);
        if (!opcode) {
            continue;
        }
        if (test(instr, opcode->get())) {
            --pos;
            return std::make_optional(pack(instr, opcode->get()));
        }
    }
    return std::nullopt;
}

std::optional<std::pair<BufferInfo, u64>> TrackLDC(const CFGRebuildState& state, u32& pos,
                                                   u64 brx_tracked_register) {
    return TrackInstruction<std::pair<BufferInfo, u64>>(
        state, pos,
        [brx_tracked_register](auto instr, const auto& opcode) {
            return opcode.GetId() == OpCode::Id::LD_C &&
                   instr.gpr0.Value() == brx_tracked_register &&
                   instr.ld_c.type.Value() == Tegra::Shader::UniformType::Single;
        },
        [](auto instr, const auto& opcode) {
            const BufferInfo info = {static_cast<u32>(instr.cbuf36.index.Value()),
                                     static_cast<u32>(instr.cbuf36.GetOffset())};
            return std::make_pair(info, instr.gpr8.Value());
        });
}

std::optional<u64> TrackSHLRegister(const CFGRebuildState& state, u32& pos,
                                    u64 ldc_tracked_register) {
    return TrackInstruction<u64>(
        state, pos,
        [ldc_tracked_register](auto instr, const auto& opcode) {
            return opcode.GetId() == OpCode::Id::SHL_IMM &&
                   instr.gpr0.Value() == ldc_tracked_register;
        },
        [](auto instr, const auto&) { return instr.gpr8.Value(); });
}

std::optional<u32> TrackIMNMXValue(const CFGRebuildState& state, u32& pos,
                                   u64 shl_tracked_register) {
    return TrackInstruction<u32>(
        state, pos,
        [shl_tracked_register](auto instr, const auto& opcode) {
            return opcode.GetId() == OpCode::Id::IMNMX_IMM &&
                   instr.gpr0.Value() == shl_tracked_register;
        },
        [](auto instr, const auto&) {
            return static_cast<u32>(instr.alu.GetSignedImm20_20() + 1);
        });
}

std::optional<BranchIndirectInfo> TrackBranchIndirectInfo(const CFGRebuildState& state, u32 pos) {
    const auto brx_info = GetBRXInfo(state, pos);
    if (!brx_info) {
        return std::nullopt;
    }
    const auto [relative_position, brx_tracked_register] = *brx_info;

    const auto ldc_info = TrackLDC(state, pos, brx_tracked_register);
    if (!ldc_info) {
        return std::nullopt;
    }
    const auto [buffer_info, ldc_tracked_register] = *ldc_info;

    const auto shl_tracked_register = TrackSHLRegister(state, pos, ldc_tracked_register);
    if (!shl_tracked_register) {
        return std::nullopt;
    }

    const auto entries = TrackIMNMXValue(state, pos, *shl_tracked_register);
    if (!entries) {
        return std::nullopt;
    }

    return BranchIndirectInfo{buffer_info.index, buffer_info.offset, *entries, relative_position};
}

std::pair<ParseResult, ParseInfo> ParseCode(CFGRebuildState& state, u32 address) {
    u32 offset = static_cast<u32>(address);
    const u32 end_address = static_cast<u32>(state.program_code.size());
    ParseInfo parse_info{};
    SingleBranch single_branch{};

    const auto insert_label = [](CFGRebuildState& state, u32 address) {
        const auto pair = state.labels.emplace(address);
        if (pair.second) {
            state.inspect_queries.push_back(address);
        }
    };

    while (true) {
        if (offset >= end_address) {
            // ASSERT_OR_EXECUTE can't be used, as it ignores the break
            ASSERT_MSG(false, "Shader passed the current limit!");

            single_branch.address = exit_branch;
            single_branch.ignore = false;
            break;
        }
        if (state.registered.count(offset) != 0) {
            single_branch.address = offset;
            single_branch.ignore = true;
            break;
        }
        if (IsSchedInstruction(offset, state.start)) {
            offset++;
            continue;
        }
        const Instruction instr = {state.program_code[offset]};
        const auto opcode = OpCode::Decode(instr);
        if (!opcode || opcode->get().GetType() != OpCode::Type::Flow) {
            offset++;
            continue;
        }

        switch (opcode->get().GetId()) {
        case OpCode::Id::EXIT: {
            const auto pred_index = static_cast<u32>(instr.pred.pred_index);
            single_branch.condition.predicate = GetPredicate(pred_index, instr.negate_pred != 0);
            if (single_branch.condition.predicate == Pred::NeverExecute) {
                offset++;
                continue;
            }
            const ConditionCode cc = instr.flow_condition_code;
            single_branch.condition.cc = cc;
            if (cc == ConditionCode::F) {
                offset++;
                continue;
            }
            single_branch.address = exit_branch;
            single_branch.kill = false;
            single_branch.is_sync = false;
            single_branch.is_brk = false;
            single_branch.ignore = false;
            parse_info.end_address = offset;
            parse_info.branch_info = MakeBranchInfo<SingleBranch>(
                single_branch.condition, single_branch.address, single_branch.kill,
                single_branch.is_sync, single_branch.is_brk, single_branch.ignore);

            return {ParseResult::ControlCaught, parse_info};
        }
        case OpCode::Id::BRA: {
            if (instr.bra.constant_buffer != 0) {
                return {ParseResult::AbnormalFlow, parse_info};
            }
            const auto pred_index = static_cast<u32>(instr.pred.pred_index);
            single_branch.condition.predicate = GetPredicate(pred_index, instr.negate_pred != 0);
            if (single_branch.condition.predicate == Pred::NeverExecute) {
                offset++;
                continue;
            }
            const ConditionCode cc = instr.flow_condition_code;
            single_branch.condition.cc = cc;
            if (cc == ConditionCode::F) {
                offset++;
                continue;
            }
            const u32 branch_offset = offset + instr.bra.GetBranchTarget();
            if (branch_offset == 0) {
                single_branch.address = exit_branch;
            } else {
                single_branch.address = branch_offset;
            }
            insert_label(state, branch_offset);
            single_branch.kill = false;
            single_branch.is_sync = false;
            single_branch.is_brk = false;
            single_branch.ignore = false;
            parse_info.end_address = offset;
            parse_info.branch_info = MakeBranchInfo<SingleBranch>(
                single_branch.condition, single_branch.address, single_branch.kill,
                single_branch.is_sync, single_branch.is_brk, single_branch.ignore);

            return {ParseResult::ControlCaught, parse_info};
        }
        case OpCode::Id::SYNC: {
            const auto pred_index = static_cast<u32>(instr.pred.pred_index);
            single_branch.condition.predicate = GetPredicate(pred_index, instr.negate_pred != 0);
            if (single_branch.condition.predicate == Pred::NeverExecute) {
                offset++;
                continue;
            }
            const ConditionCode cc = instr.flow_condition_code;
            single_branch.condition.cc = cc;
            if (cc == ConditionCode::F) {
                offset++;
                continue;
            }
            single_branch.address = unassigned_branch;
            single_branch.kill = false;
            single_branch.is_sync = true;
            single_branch.is_brk = false;
            single_branch.ignore = false;
            parse_info.end_address = offset;
            parse_info.branch_info = MakeBranchInfo<SingleBranch>(
                single_branch.condition, single_branch.address, single_branch.kill,
                single_branch.is_sync, single_branch.is_brk, single_branch.ignore);

            return {ParseResult::ControlCaught, parse_info};
        }
        case OpCode::Id::BRK: {
            const auto pred_index = static_cast<u32>(instr.pred.pred_index);
            single_branch.condition.predicate = GetPredicate(pred_index, instr.negate_pred != 0);
            if (single_branch.condition.predicate == Pred::NeverExecute) {
                offset++;
                continue;
            }
            const ConditionCode cc = instr.flow_condition_code;
            single_branch.condition.cc = cc;
            if (cc == ConditionCode::F) {
                offset++;
                continue;
            }
            single_branch.address = unassigned_branch;
            single_branch.kill = false;
            single_branch.is_sync = false;
            single_branch.is_brk = true;
            single_branch.ignore = false;
            parse_info.end_address = offset;
            parse_info.branch_info = MakeBranchInfo<SingleBranch>(
                single_branch.condition, single_branch.address, single_branch.kill,
                single_branch.is_sync, single_branch.is_brk, single_branch.ignore);

            return {ParseResult::ControlCaught, parse_info};
        }
        case OpCode::Id::KIL: {
            const auto pred_index = static_cast<u32>(instr.pred.pred_index);
            single_branch.condition.predicate = GetPredicate(pred_index, instr.negate_pred != 0);
            if (single_branch.condition.predicate == Pred::NeverExecute) {
                offset++;
                continue;
            }
            const ConditionCode cc = instr.flow_condition_code;
            single_branch.condition.cc = cc;
            if (cc == ConditionCode::F) {
                offset++;
                continue;
            }
            single_branch.address = exit_branch;
            single_branch.kill = true;
            single_branch.is_sync = false;
            single_branch.is_brk = false;
            single_branch.ignore = false;
            parse_info.end_address = offset;
            parse_info.branch_info = MakeBranchInfo<SingleBranch>(
                single_branch.condition, single_branch.address, single_branch.kill,
                single_branch.is_sync, single_branch.is_brk, single_branch.ignore);

            return {ParseResult::ControlCaught, parse_info};
        }
        case OpCode::Id::SSY: {
            const u32 target = offset + instr.bra.GetBranchTarget();
            insert_label(state, target);
            state.ssy_labels.emplace(offset, target);
            break;
        }
        case OpCode::Id::PBK: {
            const u32 target = offset + instr.bra.GetBranchTarget();
            insert_label(state, target);
            state.pbk_labels.emplace(offset, target);
            break;
        }
        case OpCode::Id::BRX: {
            const auto tmp = TrackBranchIndirectInfo(state, offset);
            if (!tmp) {
                LOG_WARNING(HW_GPU, "BRX Track Unsuccesful");
                return {ParseResult::AbnormalFlow, parse_info};
            }

            const auto result = *tmp;
            const s32 pc_target = offset + result.relative_position;
            std::vector<CaseBranch> branches;
            for (u32 i = 0; i < result.entries; i++) {
                auto key = state.registry.ObtainKey(result.buffer, result.offset + i * 4);
                if (!key) {
                    return {ParseResult::AbnormalFlow, parse_info};
                }
                u32 value = *key;
                u32 target = static_cast<u32>((value >> 3) + pc_target);
                insert_label(state, target);
                branches.emplace_back(value, target);
            }
            parse_info.end_address = offset;
            parse_info.branch_info = MakeBranchInfo<MultiBranch>(
                static_cast<u32>(instr.gpr8.Value()), std::move(branches));

            return {ParseResult::ControlCaught, parse_info};
        }
        default:
            break;
        }

        offset++;
    }
    single_branch.kill = false;
    single_branch.is_sync = false;
    single_branch.is_brk = false;
    parse_info.end_address = offset - 1;
    parse_info.branch_info = MakeBranchInfo<SingleBranch>(
        single_branch.condition, single_branch.address, single_branch.kill, single_branch.is_sync,
        single_branch.is_brk, single_branch.ignore);
    return {ParseResult::BlockEnd, parse_info};
}

bool TryInspectAddress(CFGRebuildState& state) {
    if (state.inspect_queries.empty()) {
        return false;
    }

    const u32 address = state.inspect_queries.front();
    state.inspect_queries.pop_front();
    const auto [result, block_index] = TryGetBlock(state, address);
    switch (result) {
    case BlockCollision::Found: {
        return true;
    }
    case BlockCollision::Inside: {
        // This case is the tricky one:
        // We need to split the block into 2 separate blocks
        const u32 end = state.block_info[block_index].end;
        BlockInfo& new_block = CreateBlockInfo(state, address, end);
        BlockInfo& current_block = state.block_info[block_index];
        current_block.end = address - 1;
        new_block.branch = std::move(current_block.branch);
        BlockBranchInfo forward_branch = MakeBranchInfo<SingleBranch>();
        const auto branch = std::get_if<SingleBranch>(forward_branch.get());
        branch->address = address;
        branch->ignore = true;
        current_block.branch = std::move(forward_branch);
        return true;
    }
    default:
        break;
    }
    const auto [parse_result, parse_info] = ParseCode(state, address);
    if (parse_result == ParseResult::AbnormalFlow) {
        // if it's AbnormalFlow, we end it as false, ending the CFG reconstruction
        return false;
    }

    BlockInfo& block_info = CreateBlockInfo(state, address, parse_info.end_address);
    block_info.branch = parse_info.branch_info;
    if (std::holds_alternative<SingleBranch>(*block_info.branch)) {
        const auto branch = std::get_if<SingleBranch>(block_info.branch.get());
        if (branch->condition.IsUnconditional()) {
            return true;
        }
        const u32 fallthrough_address = parse_info.end_address + 1;
        state.inspect_queries.push_front(fallthrough_address);
        return true;
    }
    return true;
}

bool TryQuery(CFGRebuildState& state) {
    const auto gather_labels = [](std::stack<u32>& cc, std::map<u32, u32>& labels,
                                  BlockInfo& block) {
        auto gather_start = labels.lower_bound(block.start);
        const auto gather_end = labels.upper_bound(block.end);
        while (gather_start != gather_end) {
            cc.push(gather_start->second);
            ++gather_start;
        }
    };
    if (state.queries.empty()) {
        return false;
    }

    Query& q = state.queries.front();
    const u32 block_index = state.registered[q.address];
    BlockInfo& block = state.block_info[block_index];
    // If the block is visited, check if the stacks match, else gather the ssy/pbk
    // labels into the current stack and look if the branch at the end of the block
    // consumes a label. Schedule new queries accordingly
    if (block.visited) {
        BlockStack& stack = state.stacks[q.address];
        const bool all_okay = (stack.ssy_stack.empty() || q.ssy_stack == stack.ssy_stack) &&
                              (stack.pbk_stack.empty() || q.pbk_stack == stack.pbk_stack);
        state.queries.pop_front();
        return all_okay;
    }
    block.visited = true;
    state.stacks.insert_or_assign(q.address, BlockStack{q});

    Query q2(q);
    state.queries.pop_front();
    gather_labels(q2.ssy_stack, state.ssy_labels, block);
    gather_labels(q2.pbk_stack, state.pbk_labels, block);
    if (std::holds_alternative<SingleBranch>(*block.branch)) {
        auto* branch = std::get_if<SingleBranch>(block.branch.get());
        if (!branch->condition.IsUnconditional()) {
            q2.address = block.end + 1;
            state.queries.push_back(q2);
        }

        auto& conditional_query = state.queries.emplace_back(q2);
        if (branch->is_sync) {
            if (branch->address == unassigned_branch) {
                branch->address = conditional_query.ssy_stack.top();
            }
            conditional_query.ssy_stack.pop();
        }
        if (branch->is_brk) {
            if (branch->address == unassigned_branch) {
                branch->address = conditional_query.pbk_stack.top();
            }
            conditional_query.pbk_stack.pop();
        }
        conditional_query.address = branch->address;
        return true;
    }

    const auto* multi_branch = std::get_if<MultiBranch>(block.branch.get());
    for (const auto& branch_case : multi_branch->branches) {
        auto& conditional_query = state.queries.emplace_back(q2);
        conditional_query.address = branch_case.address;
    }

    return true;
}

void InsertBranch(ASTManager& mm, const BlockBranchInfo& branch_info) {
    const auto get_expr = [](const Condition& cond) -> Expr {
        Expr result;
        if (cond.cc != ConditionCode::T) {
            result = MakeExpr<ExprCondCode>(cond.cc);
        }
        if (cond.predicate != Pred::UnusedIndex) {
            u32 pred = static_cast<u32>(cond.predicate);
            bool negate = false;
            if (pred > 7) {
                negate = true;
                pred -= 8;
            }
            Expr extra = MakeExpr<ExprPredicate>(pred);
            if (negate) {
                extra = MakeExpr<ExprNot>(std::move(extra));
            }
            if (result) {
                return MakeExpr<ExprAnd>(std::move(extra), std::move(result));
            }
            return extra;
        }
        if (result) {
            return result;
        }
        return MakeExpr<ExprBoolean>(true);
    };

    if (std::holds_alternative<SingleBranch>(*branch_info)) {
        const auto* branch = std::get_if<SingleBranch>(branch_info.get());
        if (branch->address < 0) {
            if (branch->kill) {
                mm.InsertReturn(get_expr(branch->condition), true);
                return;
            }
            mm.InsertReturn(get_expr(branch->condition), false);
            return;
        }
        mm.InsertGoto(get_expr(branch->condition), branch->address);
        return;
    }
    const auto* multi_branch = std::get_if<MultiBranch>(branch_info.get());
    for (const auto& branch_case : multi_branch->branches) {
        mm.InsertGoto(MakeExpr<ExprGprEqual>(multi_branch->gpr, branch_case.cmp_value),
                      branch_case.address);
    }
}

void DecompileShader(CFGRebuildState& state) {
    state.manager->Init();
    for (auto label : state.labels) {
        state.manager->DeclareLabel(label);
    }
    for (auto& block : state.block_info) {
        if (state.labels.count(block.start) != 0) {
            state.manager->InsertLabel(block.start);
        }
        const bool ignore = BlockBranchIsIgnored(block.branch);
        u32 end = ignore ? block.end + 1 : block.end;
        state.manager->InsertBlock(block.start, end);
        if (!ignore) {
            InsertBranch(*state.manager, block.branch);
        }
    }
    state.manager->Decompile();
}

} // Anonymous namespace

std::unique_ptr<ShaderCharacteristics> ScanFlow(const ProgramCode& program_code, u32 start_address,
                                                const CompilerSettings& settings,
                                                Registry& registry) {
    auto result_out = std::make_unique<ShaderCharacteristics>();
    if (settings.depth == CompileDepth::BruteForce) {
        result_out->settings.depth = CompileDepth::BruteForce;
        return result_out;
    }

    CFGRebuildState state{program_code, start_address, registry};
    // Inspect Code and generate blocks
    state.labels.clear();
    state.labels.emplace(start_address);
    state.inspect_queries.push_back(state.start);
    while (!state.inspect_queries.empty()) {
        if (!TryInspectAddress(state)) {
            result_out->settings.depth = CompileDepth::BruteForce;
            return result_out;
        }
    }

    bool use_flow_stack = true;

    bool decompiled = false;

    if (settings.depth != CompileDepth::FlowStack) {
        // Decompile Stacks
        state.queries.push_back(Query{state.start, {}, {}});
        decompiled = true;
        while (!state.queries.empty()) {
            if (!TryQuery(state)) {
                decompiled = false;
                break;
            }
        }
    }

    use_flow_stack = !decompiled;

    // Sort and organize results
    std::sort(state.block_info.begin(), state.block_info.end(),
              [](const BlockInfo& a, const BlockInfo& b) -> bool { return a.start < b.start; });
    if (decompiled && settings.depth != CompileDepth::NoFlowStack) {
        ASTManager manager{settings.depth != CompileDepth::DecompileBackwards,
                           settings.disable_else_derivation};
        state.manager = &manager;
        DecompileShader(state);
        decompiled = state.manager->IsFullyDecompiled();
        if (!decompiled) {
            if (settings.depth == CompileDepth::FullDecompile) {
                LOG_CRITICAL(HW_GPU, "Failed to remove all the gotos!:");
            } else {
                LOG_CRITICAL(HW_GPU, "Failed to remove all backward gotos!:");
            }
            state.manager->ShowCurrentState("Of Shader");
            state.manager->Clear();
        } else {
            auto characteristics = std::make_unique<ShaderCharacteristics>();
            characteristics->start = start_address;
            characteristics->settings.depth = settings.depth;
            characteristics->manager = std::move(manager);
            characteristics->end = state.block_info.back().end + 1;
            return characteristics;
        }
    }

    result_out->start = start_address;
    result_out->settings.depth =
        use_flow_stack ? CompileDepth::FlowStack : CompileDepth::NoFlowStack;
    result_out->blocks.clear();
    for (auto& block : state.block_info) {
        ShaderBlock new_block{};
        new_block.start = block.start;
        new_block.end = block.end;
        new_block.ignore_branch = BlockBranchIsIgnored(block.branch);
        if (!new_block.ignore_branch) {
            new_block.branch = block.branch;
        }
        result_out->end = std::max(result_out->end, block.end);
        result_out->blocks.push_back(new_block);
    }
    if (!use_flow_stack) {
        result_out->labels = std::move(state.labels);
        return result_out;
    }

    auto back = result_out->blocks.begin();
    auto next = std::next(back);
    while (next != result_out->blocks.end()) {
        if (state.labels.count(next->start) == 0 && next->start == back->end + 1) {
            back->end = next->end;
            next = result_out->blocks.erase(next);
            continue;
        }
        back = next;
        ++next;
    }

    return result_out;
}
} // namespace VideoCommon::Shader
