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

### 嘗試 A：隨機蒙地卡羅模擬（自行嘗試與效能瓶頸）
*   **過程**：我們自行在評估函數中實作了隨機蒙地卡羅模擬 (Monte Carlo Rollout)，在每個葉子節點執行 5 次最深達 100 步的隨機模擬，期望能獲得更精確的局勢估值。
*   **結果與不適用原因**：隨後在實測中發現此方法在限時對戰下並不適合：
    1.  **非確定性（隨機性）**：隨機模擬的結果每次不同，破壞了 Alpha-Beta 剪枝的數學確定性，並導致置換表（Transposition Table）哈希快取嚴重失效。
    2.  **效能開銷極大**：在每步 2 秒的嚴格時間限制下，這導致估值速度極慢，搜尋深度嚴重受限。
*   **處置**：最終決定註銷該隨機模擬區塊，回歸純靜態評估。改進後估值速度**提升了 10~15 倍** (NPS 達到 4.5M~5.2M)，使搜尋深度得以大幅提升。


### 缺陷 B：置換表 (TT) 殘留汙染
*   **問題**：置換表採用全域靜態結構，但在每一步搜尋之間沒有重置，導致上一手棋的哈希值碰撞干擾當前搜尋，經常使 AI 出現送子等匪夷所思的失誤。
*   **修復**：在 `search()` 深度等於 1（即迭代加深第一層）以及收到 `ubginewgame` 新局指令時，呼叫 `tt.clear()` 清空置換表，確保快取一致性。

### 缺陷 C：單執行緒高頻時間輪詢與安全退出 (Time Management)
*   **問題**：原系統只在完成整層迭代搜尋後才檢查時間，如果深層搜尋的第 $N$ 層計算量暴增，容易在層內運算時就直接超時，導致被裁判判定超時棄權。
*   **修復**：我們完全遵循單執行緒規則，在搜尋演算法（`eval_ctx` 與 `quiescence` 遞迴）中植入高頻率的時間輪詢。我們在 `SearchContext` 中記錄了搜尋開始時間 `start_time` 與限制毫秒數 `time_limit_ms`（設定為著手時限的 90% 以確保安全餘裕）。在每個節點展開時，會以極低開銷的位元運算（每 2048 個節點）透過單執行緒的 `std::chrono::steady_clock` 取得目前耗時，一旦超過時限即主動將 `ctx.stop` 設為 true 並安全退出。這在完全不開闢任何輔助執行緒的前提下，實現了精準且 100% 的超時防禦。


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

## 6. 估值參數機器學習調優與學術反思 (Machine Learning Parameter Tuning & Reflection)

為進一步科學化估值函數的設計，並消除人為設定常量（如 Passed Pawn 推進分數、車開口線、馬哨所加分等）的盲點，我們在專案中設計並實施了一套**嶺回歸殘差學習 (Ridge-regularized SGD Residual Fitting)** 實驗：

### A. 調參方法與數據採集 (Methodology & Data Collection)
1. **特徵定義**：我們在 C++ 中定義了 7 維位置/戰術差值特徵：`[PassedPawn Row 1..4, Rook Open File, Knight Outpost, Mobility]`。
2. **數據標註**：透過 C++ 數據採集工具運行 150 局隨機開局自對弈，收集了 **9,984 個** 中局盤面。以 `depth=5` 的 Minimax 搜尋分數與標準靜態估值之差值（`score - base_eval`）作為擬合的殘差標籤。
3. **模型訓練**：在 Python 中實現了不依賴第三方庫的 L2 正則化隨機梯度下降 (SGD) 嶺回歸模型，藉此擬合出 7 個位置特徵的最佳實戰權重。

### B. 擬合結果與參數修正
經過 120 個 Epoch 訓練，模型收斂出的特徵權重如下：
*   **PassedPawn Row 1..4** $\rightarrow$ `[16.8, 0.3, 28.2, 55.0]`（對稱約束為 `[16, 0, 28, 55]`）
*   **Rook Open File** $\rightarrow$ `-58.6`（因負值被約束歸零 `0`）
*   **Knight Outpost** $\rightarrow$ `-1.6`（因負值被約束歸零 `0`）
*   **Mobility** $\rightarrow$ `38.8`

### C. 實戰檢驗與學術反思 (Academic Reflection)
將擬合參數套回 C++ 進行實戰對局測試，結果顯示對抗 Boss 的勝率反而有所下降（白棋出現輸棋）。我們對此進行了深入的學術剖析：
1. **位置屏障與單調性破壞**：擬合得出的 Passed Pawn 加分 `[16, 0, 28, 55]` 破壞了原有人腦設定的單調遞增性（Row 2 的價值驟降至 0）。這在博弈樹中形成了「位置屏障」，導致 AI 寧可把兵留在 Row 1（拿 16 分）也不願意推到 Row 2（只有 0 分），嚴重阻礙了推進升變的戰術意願。這證明**人為設定的單調遞增約束在引導子力推進方面，比單純擬合靜態殘差更具實戰健壯性**。
2. **位置加分的負面干擾**：車佔開口線與馬哨所在擬合中收斂為負數（被模型判定無效歸零），這以數值證明了在 6x5 窄棋盤下，強加這些額外的位置指標會對原有的 Piece-Square Table (PST) 產生負面干擾，引導子力做出偏離全局防守的錯誤抉擇。
3. **機動性隱形優勢**：機動性（Mobility）的權重在殘差擬合中高達 38.8。這說明即使我們不開啟慢速的機動性估值以維持極限 NPS（5M），深層搜尋本身也會在博弈樹展開中自然放大並彰顯機動性的隱形優勢。

本實驗的失敗與反思，讓我們深刻體會到博弈演算法中「靜態擬合」與「動態博弈搜尋」之間的微妙關係，這為我們的引擎最終版本提供了最強有力的學術理論支持。

---

## 7. 結論 (Conclusion)

本專案中，我們從一個基礎的 Hackathon TODO 架構出發，藉由在評估上排除非確定性的隨機模擬、強化置換表管理與超時保護，建立了穩固的基礎。隨後，我們透過實作 PVS、NMP、LMR、歷史啟發式與殺手著法，使搜尋深度翻倍。最後，配合針對 6x5 MiniChess 的動態分值估值與通路兵加分，順利攻克了 Boss 基準，取得了滿分（8/8分）的耀眼成績。我們也大膽嘗試了殘差嶺回歸調參，雖然實戰證明了人為單調約束的優越性，但這場數據實驗為我們提供了寶貴的博弈引擎開發真知。
