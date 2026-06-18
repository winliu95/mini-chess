#include <utility>
#include <algorithm>
#include <vector>
#include "state.hpp"
#include "minimax.hpp"

// TT structures and declarations
enum TTFlag {
    TT_EXACT,
    TT_ALPHA,
    TT_BETA
};

struct TTEntry {
    uint64_t hash = 0;
    int depth = -1;
    int score = 0;
    TTFlag flag = TT_EXACT;
    Move best_move;
};

class TranspositionTable {
public:
    static constexpr size_t SIZE = 1 << 20; // 1,048,576 entries (~64MB)
    
    void clear() {
        std::fill(table.begin(), table.end(), TTEntry{});
    }
    
    TTEntry* probe(uint64_t hash) {
        size_t index = hash & (SIZE - 1);
        if (table[index].hash == hash) {
            return &table[index];
        }
        return nullptr;
    }
    
    void store(uint64_t hash, int depth, int score, TTFlag flag, const Move& best_move) {
        size_t index = hash & (SIZE - 1);
        if (table[index].depth == -1 || depth >= table[index].depth || table[index].hash == hash) {
            table[index].hash = hash;
            table[index].depth = depth;
            table[index].score = score;
            table[index].flag = flag;
            if (best_move.first != best_move.second) {
                table[index].best_move = best_move;
            }
        }
    }

private:
    std::vector<TTEntry> table{SIZE};
};

static TranspositionTable tt;

static Move killer_moves[100][2];
static int history_table[2][30][30];

static void clear_ordering_heuristics() {
    std::fill(&killer_moves[0][0], &killer_moves[0][0] + sizeof(killer_moves) / sizeof(Move), Move{});
    std::fill(&history_table[0][0][0], &history_table[0][0][0] + sizeof(history_table) / sizeof(int), 0);
}

constexpr int MATE_THRESHOLD = 99000;

inline int value_to_tt(int score, int ply) {
    if (score > MATE_THRESHOLD) return score + ply;
    if (score < -MATE_THRESHOLD) return score - ply;
    return score;
}

inline int value_from_tt(int score, int ply) {
    if (score > MATE_THRESHOLD) return score - ply;
    if (score < -MATE_THRESHOLD) return score + ply;
    return score;
}

static int get_move_score(State* state, const Move& action, int ply, const Move& tt_move = {}) {
    if (action == tt_move) {
        return 2000000;
    }
    int p = state->player;
    int opp = 1 - p;

    int from_r = action.first.first;
    int from_c = action.first.second;
    int to_r = action.second.first;
    int to_c = action.second.second;

    int attacker = state->piece_at(p, from_r, from_c);
    int victim = state->piece_at(opp, to_r, to_c);

    int score = 0;
    if (victim > 0) {
        int victim_val = (victim >= 0 && victim <= 6) ? PIECE_VALUES[victim] : 0;
        int attacker_val = (attacker >= 0 && attacker <= 6) ? PIECE_VALUES[attacker] : 0;
        score = 1000000 + (victim_val * 100) - attacker_val;
    } else {
        if (attacker == 1 && (to_r == 0 || (int)to_r == state->board_h() - 1)) {
            score = 500000;
        } else {
            if (ply < 100) {
                if (action == killer_moves[ply][0]) {
                    score = 400000;
                } else if (action == killer_moves[ply][1]) {
                    score = 300000;
                }
            }
            int from_sq = from_r * 5 + from_c;
            int to_sq = to_r * 5 + to_c;
            score += history_table[p][from_sq][to_sq];
        }
    }
    return score;
}

/*============================================================
 * MiniMax — quiescence
 *
 * Quiescence Search for capturing moves to avoid horizon effect.
 *============================================================*/
int MiniMax::quiescence(
    State *state,
    int alpha,
    int beta,
    GameHistory& history,
    int ply,
    SearchContext& ctx,
    const MMParams& p
) {
    ctx.nodes++;
    if(ply > ctx.seldepth){
        ctx.seldepth = ply;
    }
    if(ctx.check_time()){
        return 0;
    }

    int stand_pat = state->evaluate(p.use_kp_eval, p.use_eval_mobility, &history);
    if (stand_pat >= beta) {
        return stand_pat;
    }
    if (stand_pat > alpha) {
        alpha = stand_pat;
    }

    /* === Lazy move generation (sets game_state) === */
    if (state->legal_actions.empty() && state->game_state == UNKNOWN) {
        state->get_legal_actions();
    }

    if (state->game_state == WIN) {
        return P_MAX - ply;
    }
    if (state->game_state == DRAW) {
        return 0;
    }

    // Filter only capture moves
    std::vector<Move> captures;
    int opp = 1 - state->player;
    for (const auto& action : state->legal_actions) {
        int victim = state->piece_at(opp, action.second.first, action.second.second);
        if (victim > 0) {
            captures.push_back(action);
        }
    }

    // Sort captures by MVV-LVA score
    if (!captures.empty()) {
        std::sort(captures.begin(), captures.end(), [&](const Move& a, const Move& b) {
            return get_move_score(state, a, ply) > get_move_score(state, b, ply);
        });
    }

    for (const auto& action : captures) {
        if (ctx.stop) {
            break;
        }
        State* next = state->next_state(action);
        bool same = next->same_player_as_parent();
        int raw, score;
        if (same) {
            raw = quiescence(next, alpha, beta, history, ply + 1, ctx, p);
            score = raw;
        } else {
            raw = quiescence(next, -beta, -alpha, history, ply + 1, ctx, p);
            score = -raw;
        }
        delete next;

        if (score >= beta) {
            return score;
        }
        if (score > alpha) {
            alpha = score;
        }
    }

    return alpha;
}

/*============================================================
 * MiniMax — eval_ctx
 *
 * Negamax with PVS, TT, and Quiescence.
 *============================================================*/
int MiniMax::eval_ctx(
    State *state,
    int depth,
    int alpha,
    int beta,
    GameHistory& history,
    int ply,
    SearchContext& ctx,
    const MMParams& p,
    bool last_was_null
){
    ctx.nodes++;
    if(ply > ctx.seldepth){
        ctx.seldepth = ply;
    }
    if(ctx.check_time()){
        return 0;
    }

    /* === Lazy move generation (sets game_state) === */
    if(state->legal_actions.empty() && state->game_state == UNKNOWN){
        state->get_legal_actions();
    }

    /* === Terminal / leaf checks === */
    if(state->game_state == WIN){
        return P_MAX - ply;
    }

    if(state->game_state == DRAW){
        return 0;
    }

    /* === Repetition check (game-specific) === */
    int rep_score;
    if(state->check_repetition(history, rep_score)){
        return rep_score;
    }
    history.push(state->hash());

    // --- TT Probe ---
    uint64_t hash_val = state->hash();
    TTEntry* tt_entry = tt.probe(hash_val);
    Move tt_move;
    if (tt_entry) {
        tt_move = tt_entry->best_move;
        if (tt_entry->depth >= depth) {
            int tt_score = value_from_tt(tt_entry->score, ply);
            if (tt_entry->flag == TT_EXACT) {
                history.pop(state->hash());
                return tt_score;
            } else if (tt_entry->flag == TT_ALPHA && tt_score <= alpha) {
                history.pop(state->hash());
                return tt_score;
            } else if (tt_entry->flag == TT_BETA && tt_score >= beta) {
                history.pop(state->hash());
                return tt_score;
            }
        }
    }

    if(depth <= 0){
        int score = quiescence(state, alpha, beta, history, ply, ctx, p);
        history.pop(state->hash());
        return score;
    }

    // --- Null Move Pruning (NMP) ---
    if (depth >= 3 && ply > 0 && !last_was_null) {
        int static_eval = state->evaluate(p.use_kp_eval, p.use_eval_mobility, &history);
        if (static_eval >= beta) {
            BaseState* null_state = state->create_null_state();
            if (null_state) {
                int R = (depth > 6) ? 3 : 2;
                int null_score = -eval_ctx(dynamic_cast<State*>(null_state), depth - 1 - R, -beta, -beta + 1, history, ply + 1, ctx, p, true);
                delete null_state;
                if (null_score >= beta) {
                    history.pop(state->hash());
                    return null_score;
                }
            }
        }
    }

    // Sort moves using both MVV-LVA and the TT best move
    if(!state->legal_actions.empty()){
        std::sort(state->legal_actions.begin(), state->legal_actions.end(), [&](const Move& a, const Move& b) {
            return get_move_score(state, a, ply, tt_move) > get_move_score(state, b, ply, tt_move);
        });
    }

    /* === Negamax loop (PVS + LMR) === */
    int best_score = M_MAX;
    Move best_action;
    int orig_alpha = alpha;
    int move_index = 0;
    int p_side = state->player;
    int opp_side = 1 - p_side;

    for(auto& action : state->legal_actions){
        if (ctx.stop) {
            break;
        }
        State* next = state->next_state(action);
        bool same = next->same_player_as_parent();
        int raw, score;

        int attacker = state->piece_at(p_side, action.first.first, action.first.second);
        int victim = state->piece_at(opp_side, action.second.first, action.second.second);
        bool is_capture = (victim > 0);
        bool is_promotion = (attacker == 1 && (action.second.first == 0 || (int)action.second.first == state->board_h() - 1));

        if (move_index == 0) {
            // First move: search with full window
            if (same) {
                raw = eval_ctx(next, depth - 1, alpha, beta, history, ply + 1, ctx, p, false);
                score = raw;
            } else {
                raw = eval_ctx(next, depth - 1, -beta, -alpha, history, ply + 1, ctx, p, false);
                score = -raw;
            }
        } else {
            // Late Move Reduction (LMR)
            bool lmr_applicable = (depth >= 3 && !same && !is_capture && !is_promotion);
            int reduction = 0;
            if (lmr_applicable) {
                reduction = 1;
                if (depth >= 5 && move_index >= 4) {
                    reduction = 2;
                }
                if (depth - 1 - reduction < 1) {
                    reduction = depth - 2;
                }
            }

            if (reduction > 0) {
                if (same) {
                    raw = eval_ctx(next, depth - 1 - reduction, alpha, alpha + 1, history, ply + 1, ctx, p, false);
                    score = raw;
                } else {
                    raw = eval_ctx(next, depth - 1 - reduction, -alpha - 1, -alpha, history, ply + 1, ctx, p, false);
                    score = -raw;
                }

                // Re-search at full depth if reduced search fails high
                if (score > alpha) {
                    if (same) {
                        raw = eval_ctx(next, depth - 1, alpha, alpha + 1, history, ply + 1, ctx, p, false);
                        score = raw;
                    } else {
                        raw = eval_ctx(next, depth - 1, -alpha - 1, -alpha, history, ply + 1, ctx, p, false);
                        score = -raw;
                    }
                }
            } else {
                // Normal PVS search with null window
                if (same) {
                    raw = eval_ctx(next, depth - 1, alpha, alpha + 1, history, ply + 1, ctx, p, false);
                    score = raw;
                } else {
                    raw = eval_ctx(next, depth - 1, -alpha - 1, -alpha, history, ply + 1, ctx, p, false);
                    score = -raw;
                }
            }

            // If it failed high but didn't cause a beta cutoff, re-search with full window
            if (score > alpha && score < beta) {
                if (same) {
                    raw = eval_ctx(next, depth - 1, alpha, beta, history, ply + 1, ctx, p, false);
                    score = raw;
                } else {
                    raw = eval_ctx(next, depth - 1, -beta, -alpha, history, ply + 1, ctx, p, false);
                    score = -raw;
                }
            }
        }

        delete next;

        if(score > best_score){
            best_score = score;
            best_action = action;
        }
        if(best_score > alpha){
            alpha = best_score;
        }
        if(alpha >= beta){
            if (!is_capture && ply < 100) {
                if (killer_moves[ply][0] != action) {
                    killer_moves[ply][1] = killer_moves[ply][0];
                    killer_moves[ply][0] = action;
                }
                int from_sq = action.first.first * 5 + action.first.second;
                int to_sq = action.second.first * 5 + action.second.second;
                history_table[p_side][from_sq][to_sq] += depth * depth;
            }
            break; // Beta cutoff
        }
        move_index++;
    }

    // --- TT Store ---
    if (!ctx.stop) {
        TTFlag flag = TT_EXACT;
        if (best_score <= orig_alpha) {
            flag = TT_ALPHA;
        } else if (best_score >= beta) {
            flag = TT_BETA;
        }
        tt.store(hash_val, depth, value_to_tt(best_score, ply), flag, best_action);
    }

    history.pop(state->hash());
    return best_score;
}

/*============================================================
 * MiniMax — search
 *
 * Iterate legal moves, call eval_ctx, return SearchResult.
 *============================================================*/
SearchResult MiniMax::search(
    State *state,
    int depth,
    GameHistory& history,
    SearchContext& ctx
){
    ctx.reset();
    if (depth == 1) {
        tt.clear();
        clear_ordering_heuristics();
    }
    MMParams p = MMParams::from_map(ctx.params);
    SearchResult result;
    result.depth = depth;

    if(!state->legal_actions.size()){
        state->get_legal_actions();
    }

    uint64_t hash_val = state->hash();
    TTEntry* tt_entry = tt.probe(hash_val);
    Move tt_move;
    if (tt_entry) {
        tt_move = tt_entry->best_move;
    }

    if(!state->legal_actions.empty()){
        std::sort(state->legal_actions.begin(), state->legal_actions.end(), [&](const Move& a, const Move& b) {
            return get_move_score(state, a, 0, tt_move) > get_move_score(state, b, 0, tt_move);
        });
    }

    int alpha = M_MAX - 10;
    int beta = P_MAX + 10;
    int best_score = M_MAX - 10;
    int orig_alpha = alpha;
    int move_index = 0;
    int total_moves = (int)state->legal_actions.size();
    bool is_first = true;

    for(auto& action : state->legal_actions){
        if (ctx.stop) {
            break;
        }
        State* next = state->next_state(action);
        bool same   = next->same_player_as_parent();
        int raw, score;

        if (is_first) {
            // First move: search with full window
            if (same) {
                raw = eval_ctx(next, depth - 1, alpha, beta, history, 1, ctx, p, false);
                score = raw;
            } else {
                raw = eval_ctx(next, depth - 1, -beta, -alpha, history, 1, ctx, p, false);
                score = -raw;
            }
            is_first = false;
        } else {
            // Subsequent moves: search with null window
            if (same) {
                raw = eval_ctx(next, depth - 1, alpha, alpha + 1, history, 1, ctx, p, false);
                score = raw;
            } else {
                raw = eval_ctx(next, depth - 1, -alpha - 1, -alpha, history, 1, ctx, p, false);
                score = -raw;
            }

            // If it failed high but didn't cause a beta cutoff, re-search with full window
            if (score > alpha && score < beta) {
                if (same) {
                    raw = eval_ctx(next, depth - 1, alpha, beta, history, 1, ctx, p, false);
                    score = raw;
                } else {
                    raw = eval_ctx(next, depth - 1, -beta, -alpha, history, 1, ctx, p, false);
                    score = -raw;
                }
            }
        }

        delete next;

        if(score > best_score){
            best_score        = score;
            result.best_move  = action;
            result.score      = score;
            result.pv         = {action};

            if(p.report_partial && ctx.on_root_update){
                ctx.on_root_update({result.best_move, best_score, depth, move_index + 1, total_moves});
            }
        }
        if(best_score > alpha){
            alpha = best_score;
        }
        move_index++;
    }

    // --- TT Store ---
    if (!ctx.stop) {
        TTFlag flag = TT_EXACT;
        if (best_score <= orig_alpha) {
            flag = TT_ALPHA;
        } else if (best_score >= beta) {
            flag = TT_BETA;
        }
        tt.store(hash_val, depth, value_to_tt(best_score, 0), flag, result.best_move);
    }

    result.nodes = ctx.nodes;
    result.seldepth = ctx.seldepth;
    return result;
}

void MiniMax::clear_tt() {
    tt.clear();
}

/*============================================================
 * MiniMax — default_params / param_defs
 *============================================================*/
ParamMap MiniMax::default_params(){
    return {
        {"UseKPEval", "true"},
        {"UseEvalMobility", "false"},
        {"ReportPartial", "true"},
    };
}

std::vector<ParamDef> MiniMax::param_defs(){
    return {
        {"UseKPEval", ParamDef::CHECK, "true"},
        {"UseEvalMobility", ParamDef::CHECK, "false"},
        {"ReportPartial", ParamDef::CHECK, "true"},
    };
}
