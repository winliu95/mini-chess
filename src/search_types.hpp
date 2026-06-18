#pragma once
#include "base_state.hpp"
#include "search_params.hpp"
#include <vector>
#include <cstdint>
#include <functional>
#include <chrono>

class State;

struct RootUpdate {
    Move best_move;
    int score;
    int depth;
    int move_number;
    int total_moves;
};

struct SearchContext {
    uint64_t nodes = 0;
    int seldepth = 0;
    bool stop = false;
    ParamMap params;
    std::function<void(const RootUpdate&)> on_root_update;

    std::chrono::steady_clock::time_point start_time;
    int64_t time_limit_ms = 0;

    bool check_time() {
        if (stop) return true;
        if (time_limit_ms > 0 && (nodes & 2047) == 0) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count();
            if (elapsed >= time_limit_ms) {
                stop = true;
                return true;
            }
        }
        return false;
    }

    void reset(){
        nodes = 0;
        seldepth = 0;
    }
};

struct SearchResult {
    Move best_move;
    int score = 0;
    int depth = 0;
    int seldepth = 0;
    uint64_t nodes = 0;
    double time_ms = 0;
    std::vector<Move> pv;
};
