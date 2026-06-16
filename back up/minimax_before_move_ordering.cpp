#include <utility>
#include "state.hpp"
#include "minimax.hpp"


/*============================================================
 * MiniMax — eval_ctx
 *
 * Negamax without pruning. Caller manages memory.
 *============================================================*/
int MiniMax::eval_ctx(
    State *state,
    int depth,
    int alpha,
    int beta,
    GameHistory& history,
    int ply,
    SearchContext& ctx,
    const MMParams& p
){
    ctx.nodes++;
    if(ply > ctx.seldepth){
        ctx.seldepth = ply;
    }
    if(ctx.stop){
        return 0;
    }

    /* === Lazy move generation (sets game_state) === */
    if(state->legal_actions.empty() && state->game_state == UNKNOWN){
        state->get_legal_actions();
    }

    /* === Terminal / leaf checks === */

    // [ Hackathon TODO 3-1 ] Return score for winning terminal state.
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

    if(depth <= 0){
        int score = state->evaluate(
            p.use_kp_eval, p.use_eval_mobility, &history
        );
        history.pop(state->hash());
        return score;
    }

    /* === Negamax loop === */
    int best_score = M_MAX;

    for(auto& action : state->legal_actions){
        // [ Hackathon TODO 3-2 ] Create child state after applying action.
        State* next = state->next_state(action);

        // [ Hackathon TODO 3-3 ] Search child one level deeper.
        // [ Hackathon TODO 3-4 ] Convert raw score to current player's perspective.
        bool same = next->same_player_as_parent();
        int raw, score;
        if (same) {
            raw = eval_ctx(next, depth - 1, alpha, beta, history, ply + 1, ctx, p);
            score = raw;
        } else {
            raw = eval_ctx(next, depth - 1, -beta, -alpha, history, ply + 1, ctx, p);
            score = -raw;
        }

        delete next;

        // [ Hackathon TODO 3-5 ] Update best_score if this child is better.
        if(score > best_score){
            best_score = score;
        }
        if(best_score > alpha){
            alpha = best_score;
        }
        if(alpha >= beta){
            break; // Beta cutoff
        }
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
    MMParams p = MMParams::from_map(ctx.params);
    SearchResult result;
    result.depth = depth;

    if(!state->legal_actions.size()){
        state->get_legal_actions();
    }

    int alpha = M_MAX - 10;
    int beta = P_MAX + 10;
    int best_score = M_MAX - 10;
    int move_index = 0;
    int total_moves = (int)state->legal_actions.size();

    for(auto& action : state->legal_actions){
        // [ Hackathon TODO 4-1 ] Search this move from the root.
        State* next = state->next_state(action);
        bool same   = next->same_player_as_parent();
        int raw, score;
        if (same) {
            raw = eval_ctx(next, depth - 1, alpha, beta, history, 1, ctx, p);
            score = raw;
        } else {
            raw = eval_ctx(next, depth - 1, -beta, -alpha, history, 1, ctx, p);
            score = -raw;
        }
        delete next;

        if(score > best_score){
            // [ Hackathon TODO 4-2 ] Keep this move if it is the best so far.
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

    // [ Hackathon TODO 4-3 ] Update result and return.
    result.nodes = ctx.nodes;
    return result;
}


/*============================================================
 * MiniMax — default_params / param_defs
 *============================================================*/
ParamMap MiniMax::default_params(){
    return {
        {"UseKPEval", "true"},
        {"UseEvalMobility", "true"},
        {"ReportPartial", "true"},
    };
}

std::vector<ParamDef> MiniMax::param_defs(){
    return {
        {"UseKPEval", ParamDef::CHECK, "true"},
        {"UseEvalMobility", ParamDef::CHECK, "true"},
        {"ReportPartial", ParamDef::CHECK, "true"},
    };
}
