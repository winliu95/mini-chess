# MiniChess AI Engine - Project 2 Report (專案報告)

本報告詳細記錄了我們在 MiniChess (6×5 棋盤) 搜尋引擎中，從最原始的 `[Hackathon TODO]` 基礎範本，一路修改、除錯、並實作多項先進搜尋優化與估值演算法，最終成功在 **2秒時限限制下以 2-0 擊敗 Visible-Boss 基準** 的完整過程。

---

## 1. 原始 TODO 需求與基礎實作 (Initial TODOs)

在專案開始時，系統有數個關鍵部分的 `[Hackathon TODO]` 尚未填寫，導致編譯失敗或無法正常搜尋。我們首先完成了以下基礎架構：

### A. Naive 移動生成器修復 (Knight Move Gen)
在 `src/games/minichess/state.cpp` 中完成了 Naive 移動生成器的馬 (Knight) 的移動邏輯（TODO 2），使單元測試 `state_test` 中的Naive 與 Bitboard 移動生成器相互校驗能夠順利通過：
```cpp
// 節錄 Naive 騎士移動生成邏輯
case 3: // Knight
    for(auto& offset : knight_offsets){
        int nr = i + offset[0], nc = j + offset[1];
        if(nr >= 0 && nr < BOARD_H && nc >= 0 && nc < BOARD_W){
            if(!self_board[nr][nc]){
                all_actions.push_back(Move(Point(i, j), Point(nr, nc)));
                if(oppn_board[nr][nc] == 6){
                    this->game_state = WIN;
                    this->legal_actions = all_actions;
                    return;
                }
            }
        }
    }
    break;
```

### B. 基礎估值與國王定位 (Evaluation & King Detection)
在 `State::evaluate` 中實作了搜尋葉子節點的基礎估值功能：
*   **TODO 1-3**：快速掃描棋盤定位己方與敵方國王位置，以供國王親和度（Tropism）計算。
*   **TODO 1-4**：依據各棋子的基礎價值（Material）加上棋子位置表（PST）與國王親和度，計算出主動方的評估得分：
    $$\text{Score} = \text{Self Material} - \text{Opponent Material} + \text{PST Bonus} + \text{Tropism Bonus}$$
*   **TODO 1-5**：實作機動性（Mobility）評估，加減雙方合法著法的數量差值。

### C. 基礎搜尋演算法 (Minimax / Alpha-Beta)
在 `src/policy/minimax.cpp` 中，我們完成了基礎的負極大值 (Negamax) 與 Alpha-Beta 搜尋邏輯：
*   **TODO 3**：完成遞迴式的 `eval_ctx` 搜尋深度下降、正負視角轉換、與 Alpha/Beta 裁剪。
*   **TODO 4**：完成根節點 `search` 對所有合法移動的走法疊代與最佳移动 (Best Move) 的選取。

---

## 2. 重大缺陷與 Bug 修復 (Major Bug Fixes)

在基礎功能完成後，我們發現在面對強大對手（如 Boss）或在嚴格時限下，AI 容易超時或下出低俗的著法。我們發現並解決了以下四個系統性缺陷：

### 缺陷 A：隨機蒙地卡羅模擬（非確定性與效率殺手）
*   **問題**：原代碼的評估函數中，在每個葉子節點都執行了 5 次隨機蒙地卡羅模擬 (Monte Carlo Rollout)，每條路徑最深達 100 步。這導致估值**極慢**，且因為隨機性導致 Alpha-Beta 剪枝與置換表（Transposition Table）嚴重失效。
*   **修復**：直接註銷該隨機模擬區塊。改進後估值速度**提升了 10~15 倍** (NPS 達到 4.5M~5.2M)，搜尋能深入更多層數。

### 缺陷 B：置換表 (TT) 殘留汙染
*   **問題**：置換表採用全域靜態結構，但在每一步搜尋之間沒有重置，導致上一手棋的哈希值碰撞干擾當前搜尋，經常使 AI 出現送子等匪夷所思的失誤。
*   **修復**：在 `search()` 深度等於 1（即迭代加深第一層）以及收到 `ubginewgame` 新局指令時，呼叫 `tt.clear()` 清空置換表，確保快取一致性。

### 缺陷 C：超時防禦（Watchdog 執行緒）
*   **問題**：原系統只在完成整層搜尋後才檢查時間，如果第 $N$ 層搜尋超時，將直接被裁判判定超時棄權。
*   **修復**：在 `ubgi.cpp` 啟動搜尋時，開闢一個獨立的定時執行緒（不干擾搜尋本體）。當時間達到限制的 90% 時，主動設定 `g_ctx.stop = true` 進行強制中斷，確保絕不超時。

---

## 3. 進階搜尋優化 (Advanced Search Optimizations)

為在 2 秒內擊敗 Boss 基準，我們在 `minimax.cpp` 中引入了以下進階搜尋策略：

### A. 主變路徑搜尋 (Principal Variation Search, PVS)
對排序第一的移动（最可能是最佳移動）進行全視窗搜尋 $[-\beta, -\alpha]$；對後續的所有移動只進行零視窗搜尋 $[-\alpha-1, -\alpha]$。如果零視窗搜尋失敗（得分高於 $\alpha$），才對其進行全視窗重新搜尋。這大大減少了多餘分支的搜尋時間。

### B. 空步裁剪 (Null Move Pruning, NMP)
如果己方不走子（將出牌權讓給對手），對手在減少深度 $R$（$R=2\sim3$）的搜尋下仍然無法突破 `beta`，說明己方形勢極佳，直接觸發 Beta 剪枝。這對於快速裁剪無威脅的分支非常有效。

### C. 後半步深度縮減 (Late Move Reductions, LMR)
在著法排序後，排在後方的安靜移動（非吃子、非升變）成為最佳走法的機率極低。我們在深度大於等於 3 時，對後半段的安靜著法進行減少深度（如減去 1 或 2 層）的快速搜尋。如果失敗，則順利剪枝；否則才進行全深度重新搜尋。

### D. 歷史啟發式與殺手著法 (History Heuristic & Killer Moves)
為了給安靜移動排定更優的順序：
*   **Killer Moves**：記錄每一層引發 Beta 剪枝的 quiet move，在其他同層節點優先搜尋這兩步。
*   **History Heuristic**：建立全域歷史表，每當某個移動引發剪枝，就累加歷史分數（增加 `depth * depth`），歷史高分的移動會在隨後排序時往前排。

---

## 4. 針對裁判規則的估值重設計 (Evaluation Redesign)

這是我們能穩健擊敗 Boss 的核心戰術改進：

### A. 動態價值重設計 (Dynamic Material Values)
*   **衝突**：裁判的 100 步平局和棋判定為：車=6、馬=7、角=8（角/馬大於車）。如果中局我們直接套用此規則，AI 會認為角比車強，從而愚蠢地用車去送吃對方的角。
*   **解決方案**：
    1.  **中局 (step < 100)**：使用傳統棋力比例：車=100、角=60、馬=60。使 AI 展現高超的中局控制力，保護己方重子。
    2.  **殘局與和棋 (step >= 100)**：當步數達到 100（在搜尋樹的深層葉子），切換回裁判的確切分值（車=60, 馬=70, 角=80），並關閉 PST 與親和度評估。這讓 AI 能在深層精準判斷哪些和棋分支在裁判規則下是己方獲勝。
    3.  **修復**：修正 `create_null_state()` 複製 `step` 計數的 Bug，確保深層空步搜尋的估值亦能精確對齊殘局。

### B. 通路兵偵測與推進加分 (Passed Pawn Bonus)
6x5 MiniChess 的棋盤狹窄，兵極易升變為后。我們實作了通路兵（前方與鄰近列無敵兵阻擋）的偵測，並依據其前進步數（越接近 promotion 越好）給予漸進的高額加分（最高達 **+55 分**）：
```cpp
// 節錄 Passed Pawn 評估代碼
if (p_self == 1 && is_passed_pawn(this->board, this->player, r, c)) {
    int advance = (this->player == 0) ? (4 - r) : (r - 1);
    if (advance == 1) self_score += 5;
    else if (advance == 2) self_score += 15;
    else if (advance == 3) self_score += 30;
    else if (advance == 4) self_score += 55;
}
```

---

## 5. 最終對戰成果 (Evaluation Results)

經過此番優化，我們的搜尋深度從原來的 **13層** 躍升至 **22～29層**。

在 2秒 限時對戰下，測試結果如下：
*   **Random Baseline**：**PASS (2-0)**
*   **Minimax-Weak Baseline**：**PASS (2-0)**
*   **Minimax-Strong Baseline**：**PASS (2-0)**
*   **Visible-Boss Baseline**：**PASS (2-0)**

我們的 AI 在面對強悍的 **Boss 基準** 時：
*   執白（先手）順利贏棋 (`1-0`)。
*   執黑（後手）憑藉強大的通路兵攻勢與深層搜尋，同樣順利攻破 Boss 國王防線 (`1-0`)，取得 2-0 的完勝！

---

## 6. 結論 (Conclusion)

本專案中，我們從一個基礎的 Hackathon TODO 架構出發，藉由排除非確定性的蒙地卡羅模擬、強化置換表管理與超時保護，建立了穩固的基礎。隨後，我們透過實作 PVS、NMP、LMR、歷史啟發式與殺手著法，使搜尋深度翻倍。最後，配合針對 6x5 MiniChess 的動態分值估值與通路兵加分，順利攻克了 Boss 基準，取得了滿分（8/8分）的耀眼成績。
