# CPR 與 UTF-8 欄寬：第一手資料查核

日期：2026-10-18。範圍：供 CPR／UTF-8 欄寬調整評估使用，不代表已實作、已通過測試或已驗證站台部署版本。
本文件只整理原廠手冊、終端實作者規格及官方原始碼；本機影響範圍與後續設計另見 [UTF-8 cell 模型評估](UTF8-Cell-Model-Plan.md)。
後續使用者已同意並完成第一階段獨立模型，實作及測試另記於 [格模型驗收](Terminal-Cell-Model-Validation.md)；尚未接入畫面或啟用 CPR，不改寫本文件的來源查核為實測結果。

## 結論

CPR 不能只補一個格式化回覆函式。PTT 官方登入程式會用 CPR **判斷連線編碼**：先送 `CR C4 A1 ESC[6n`，再以回覆的欄數是否為 2 決定切換 UTF-8。
因此必須先讓 UTF-8 解碼、畫面格數及游標座標一致；不能把多位元組長度當欄數，也不能回報經猜測或過期的座標。
此探針的程式依據為 [logind 發送處](https://github.com/ptt/pttbbs/blob/68b88d8753d69000033037a0c84a4ee3596b937a/daemon/logind/logind.c#L2596) 與 [回覆接收處](https://github.com/ptt/pttbbs/blob/68b88d8753d69000033037a0c84a4ee3596b937a/daemon/logind/logind.c#L2369)。

以下將「來源明定」、「來源程式的可觀察行為」與「本專案建議／推論」分開；不把公開倉庫等同於目前站台部署版本。
PTT 查核固定於預設 `main` 的 `68b88d8753d69000033037a0c84a4ee3596b937a`；舊 `master` 不是本輪的現行來源。

## 1. CPR／DSR 的範圍

| 請求 | 回覆／處理範圍 | 來源 |
| --- | --- | --- |
| `ESC[6n` | 普通 CPR：`ESC[row;colR`，先列後欄，左上角為 1、1；不加換行或 NUL | [VT100 DSR](https://vt100.net/docs/vt100-ug/chapter3.html#DSR)、[CPR](https://vt100.net/docs/vt100-ug/chapter3.html#CPR) |
| `ESC[5n` | 工作狀態詢問，不是 CPR；正常狀態為 `ESC[0n` | [VT100 DSR](https://vt100.net/docs/vt100-ug/chapter3.html#DSR) |
| `ESC[?6n` | DEC-specific DECXCPR，回覆帶 `?`；xterm 的不同 VT 等級還可能加 page 參數，不可冒充普通 CPR | [xterm ctlseqs 的 DSR／DECXCPR](https://invisible-island.net/xterm/ctlseqs/ctlseqs.html)、[xterm 實作](https://github.com/ThomasDickey/xterm-snapshots/blob/5c75ead9a75d4b2e666ca06fa94503b5766ed93f/charproc.c#L4598) |

最小範圍建議：只接受完整、單一數值參數為 6 的普通 CSI；其他 DSR、private 前綴、參數列表、subparameter、intermediate、取消／截斷／溢位序列維持不回覆。
前導零可依數值 6 處理。這是本專案保守的輸入邊界，不宣稱 VT100 規格要求所有擴充或非標準序列都如此處理。

## 2. 原點、捲動區與待折行

VT100 明定 CPR 列號受 DECOM 影響：DECOM reset 時原點為全畫面左上角，與捲動區無關；set 時才相對於 margin 原點。
DECSTBM 本身只是設定捲動區，不能視為自動啟用相對原點。
PCMan 尚未實作 DECOM，因此本輪建議仍回報終端全畫面座標，扣除的是本機 scrollback 的 `first_line`，不是 `scroll_pos` 或 `scroll_region_top`。
前兩句是 [VT100 CPR](https://vt100.net/docs/vt100-ug/chapter3.html#CPR)／[DECOM](https://vt100.net/docs/vt100-ug/chapter3.html#DECOM) 的規定；最後一句是依目前本機模型作的實作建議。

待折行不能被 CPR 消耗。xterm 的 CPR 分支直接讀取 `cur_row`、`cur_col`，輸出回覆後只重設解析狀態，不呼叫游標移動或改寫 `do_wrap`；後續可列印字元仍經 `dotext` 的折行處理。
這是 [xterm 固定版本的實作行為](https://github.com/ThomasDickey/xterm-snapshots/blob/5c75ead9a75d4b2e666ca06fa94503b5766ed93f/charproc.c#L4603)，不是 VT100 CPR 段落對所有待折行細節的明文規定。
對 PCMan 的建議：當 `x == cols` 表示待折行時，回覆最後一欄 `cols`，但不要把內部 `x` 改成 `cols - 1`，也不要提前 LF。
既有 `OnText` 對非 SGR Dispatch 的通用正規化會取消此狀態，之後加入 CPR 時必須單獨處理，不能直接沿用該副作用。

## 3. PTT 登入探針與可重現預期

官方 `daemon/logind/logind.c` 的行為：

1. 在初始編碼為 Big5 時送出 `\r\xc4\xa1\033[6n`，共 7 bytes。註解說明此二位元組序列同時合法於 Big5／UTF-8，但不在 UAO，用於偵測。
2. 接收器要求 ANSI CPR 為無 private prefix、兩個參數、結尾 `R`。
3. 若第二參數為 2，便設定 `init_encoding = CONV_UTF8`；必要時切換目前編碼並重畫歡迎畫面及帳號提示。
4. 本段判斷只讀欄數，不能據此宣稱站台會驗證 CPR 所有列欄是否合法。

以上直接對應 [發送程式](https://github.com/ptt/pttbbs/blob/68b88d8753d69000033037a0c84a4ee3596b937a/daemon/logind/logind.c#L2596) 及 [接收程式](https://github.com/ptt/pttbbs/blob/68b88d8753d69000033037a0c84a4ee3596b937a/daemon/logind/logind.c#L2369)。

由這些 bytes 與字寬模型推導的測試預期如下；這是可重現的測試設計，不是本輪已實測的紀錄。

| 客戶端編碼 | `C4 A1` 的處理 | CR 後輸出的 CPR 欄數 |
| --- | --- | --- |
| UTF-8 | 解碼為 U+0121 `ġ`，1 格 | 2 |
| Big5／UAO | 保留二位元組字元的 2 格進位語意；缺字的繪製須另核對，不能改為按 UTF-8 解碼 | 3 |

UTF-8 預期 1 格可由 PTT 的 [ucs_width](https://github.com/ptt/pttbbs/blob/68b88d8753d69000033037a0c84a4ee3596b937a/common/sys/string.c#L707) 及其 [字寬表](https://github.com/ptt/pttbbs/blob/68b88d8753d69000033037a0c84a4ee3596b937a/common/sys/wcwidth.c#L300) 推導。
Big5 二位元組格數的依據為 [mb_char_width](https://github.com/ptt/pttbbs/blob/68b88d8753d69000033037a0c84a4ee3596b937a/include/multibyte.h#L206)；不將「不在 UAO」誤寫為可以丟掉 bytes 或必定有某個可見字形。
列號應為送入 CR 前所在終端列；CR 只歸零欄位，不能額外換列。

## 4. PTT 的 UTF-8 字寬政策

以下皆為固定版本公開來源，部署時可能使用不同編譯選項。

- [common/sys/utf8.c](https://github.com/ptt/pttbbs/blob/68b88d8753d69000033037a0c84a4ee3596b937a/common/sys/utf8.c#L8) 保存跨 byte 的解碼狀態，檢查 continuation、overlong、surrogate 與最大碼點；「完整字元」不是每個 byte 或 UTF-16 code unit。
- [mb_bytes／mb_width／stream_width_n](https://github.com/ptt/pttbbs/blob/68b88d8753d69000033037a0c84a4ee3596b937a/common/sys/string.c#L689) 分離字元編碼長度與顯示格數。UTF-8 有 CJK／PUA fast path；其他字元經 `mb_char_width`；串流寬度略過 ANSI 控制序列。
- [mb_char_width](https://github.com/ptt/pttbbs/blob/68b88d8753d69000033037a0c84a4ee3596b937a/include/multibyte.h#L206) 在 UTF-8 下呼叫 `ucs_width`，Big5 下以完整雙位元組字元計 2 格。`ucs_width` 使用 `mk_wcwidth_cjk`，負寬度轉為 0。
- [mk_wcwidth_cjk／is_cjk_ambiguous](https://github.com/ptt/pttbbs/blob/68b88d8753d69000033037a0c84a4ee3596b937a/common/sys/wcwidth.c#L232) 不只有一張標準 East Asian Width 表：`USE_MK_CJK_AMBIGUOUS` 選用區間表；另一分支使用預先計算位元表及額外區間，包含部分符號、PUA、`U+1F000..U+1FFFF`。本次搜尋未找到在倉庫內啟用該 macro 的定義，不推定所有部署均採相同分支。
- [一般 mk_wcwidth](https://github.com/ptt/pttbbs/blob/68b88d8753d69000033037a0c84a4ee3596b937a/common/sys/wcwidth.c#L123) 使用 combining 區間表及寬字區間。它不是現代 emoji grapheme cluster 引擎，不能以單碼點寬度表宣稱 ZWJ、旗幟、VS16 或所有新字元均完整支援。
- [pfterm 接收字元](https://github.com/ptt/pttbbs/blob/68b88d8753d69000033037a0c84a4ee3596b937a/mbbsd/pfterm.c#L1807) 取得 `mb_char_width`，`w <= 0` 直接略過。這不等於把 combining mark 附著於上一格，也不能當作完整組合字渲染的參考實作。

因此可借鑑的是「串流解碼、碼點／格數分離、PTT 相容寬度政策」的分層；不能將所有 non-ASCII 定為 2 格，也不應直接以系統字型的像素寬決定協定游標欄數。
Unicode 本身亦提醒 East_Asian_Width 並非可直接套用於所有現代終端機的完整方案；grapheme segmentation 是另一層問題。
參考 [Unicode UAX #11 Scope](https://www.unicode.org/reports/tr11/#Scope) 與 [UAX #29](https://www.unicode.org/reports/tr29/)。

## 5. 同步輸出期間的 CPR

同步輸出規格要求凍結的是畫面呈現，終端仍繼續處理輸入文字及控制序列；並未在此規格逐項定義 CPR 的排程。
本專案建議因此為：在收到完整 CPR 請求時立即回覆「目前解析中的邏輯游標」，不能讀凍結快照，也不能等 ESU 後才回覆或強制提前顯示畫面。
這是依 [同步輸出語意](https://github.com/contour-terminal/vt-extensions/blob/master/synchronized-output.md#semantics) 作的相容性推論。
PTT 會在輸出層使用 `?2026h/l`，可見 [mbbsd/io.c](https://github.com/ptt/pttbbs/blob/68b88d8753d69000033037a0c84a4ee3596b937a/mbbsd/io.c#L21)。

## 6. 安全與編碼邊界建議

CPR 回覆會進入遠端的輸入通道；不是無副作用的本地畫面查詢。最低限度應：

- 僅在完整、允許的請求下回覆一次，限定目前正常連線；ANSI 編輯器、離線檔案與已關閉連線不送出。
- 輸出固定 ASCII 控制序列與有界正整數，不附 CR、LF、NUL，不經本地回顯，不串入帳密、剪貼簿或畫面文字。
- 列／欄限制於當前終端尺寸；捲軸位置、字型縮放與視窗像素尺寸不改變報告座標。
- 避免把其他 DSR 回覆、未知字串或 OSC／DCS 內容誤作 CPR 請求；跨封包、取消、過長和非法序列須自動測試。
- 不自行新增 PTT 編碼自動切換；正確回報目前已選編碼的格數，讓站台依其流程處理。

以上是本專案風險控制建議。xterm 官方文件另提醒普通 CPR 格式可能與修改鍵 F3 序列相同，因此站台端亦需有適當請求／回覆脈絡；不能保證任何收到 `CSI row;col R` 的應用都會安全處理。
依據：[xterm DSR 說明](https://invisible-island.net/xterm/ctlseqs/ctlseqs.html)。

## 7. 官方網頁版參考的核實範圍

已核實 [PTT 官方首頁](https://www.ptt.cc/index.html) 連至 [互動終端 term.ptt.cc](https://term.ptt.cc/)，與 `/bbs` 的靜態文章閱讀入口不同；也列有 PTT2 終端入口。
使用者另提供官方推薦原始碼倉庫 [ptt/ptt-term](https://github.com/ptt/ptt-term)。其解碼、格數、CPR 與渲染的逐項查核由 [本輪主評估文件](UTF8-Cell-Model-Plan.md) 記錄，不以名稱相近的第三方客戶端替代。
本研究未下載或查核部署網站的 JavaScript bundle，亦未證明網站部署與任何 Git commit 完全相同；不把網頁版的 helper／單元測試等同於所有 Unicode 畫面行為均正確。

## 後續最小安全順序

先核定 UTF-8 欄寬政策、解碼／cell 模型與測試界線，再實作普通 CPR。
至少先驗證官方 `C4 A1` 登入探針、`A中B`、完整多位元組跨封包、寬字右邊界與待折行、combine／emoji 的明確支援或降級規則；繪製、選取、OSC 範圍與回覆必須使用同一份座標模型。
最後才驗證同步輸出、scrollback／resize、既有 Big5／UAO／SGR 66 行為及實際 PTT 登入。
本文件不核准擴充 DECOM、DECXCPR、Mouse 或重寫整個終端協定；記錄的是前置評估範圍，後續第一階段實作與驗證另見上述驗收文件。
