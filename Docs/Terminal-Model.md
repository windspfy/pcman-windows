# 終端文字模型：規格與測試

本文件合併原「格模型第一階段」「原始列」「列生命週期」「接收串流解碼」四份驗收紀錄，只保留現行的 Interface、政策與測試入口。
逐階段的提交基準、檢查數及「尚未接入」的當時狀態已移除；目前接入進度見[相容性清單](PTT-Compatibility-Checklist.md#目前進度與待辦)，設計來源見[模型方案](UTF8-Cell-Model-Plan.md)。

## 檔案與責任

| 檔案 | 責任 |
| --- | --- |
| `Lite/TerminalCellModel.h` | 純 C++17 的 `Utf8Decoder`、`Screen`、`Cell`、`Style` 與呈現快照 |
| `Lite/PttWidthProfile.h` | 固定 PTT CJK 字寬政策 |
| `Lite/EncodedTerminalRow.h` | `EncodedRow`：保存原始位元組及逐 byte 顏色／link ID，產生 UTF-8 或 legacy `Projection` |
| `Lite/WindowsTerminalRowDecoder.h` | `WindowsRowDecoder`：重用正式 `CUcs2Conv` 與 Windows DBCS 分類的 legacy adapter |
| `Lite/EncodedRowStorage.h` | 稀疏列儲存，以列身分保存非空來源 |
| `Lite/EncodedTextDecoder.h` | 接收串流解碼，回傳 glyph 批次 |
| `Tests/TerminalCellModelTests`、`WindowsTerminalRowTests`、`EncodedTextDecoderTests` | 上述各 Module 的自動測試 |

各 Module 不開 socket，不操作全域選單、GDI 或剪貼簿，也不持有第二套游標；使用端必須依既有全域編碼選單選擇 codec。

## 格模型（`Screen`）

`Screen` 使用零起算格座標；`Cursor().column == 1` 代表下一個位置是第 2 欄。可見游標永遠在格範圍內，待折行獨立存於 `wrapPending`，重複讀取 `Cursor()` 不消耗它。

| 操作 | 約定 |
| --- | --- |
| `WriteUtf8(bytes, style)` | 只接收已分派的文字；未完成 UTF-8 保留到後續呼叫，封包結尾不 flush |
| `FinishText()` | EOF 或真正的文字／控制切換時，把未完成前綴轉成一個 U+FFFD；不是每個封包結束時呼叫 |
| `WriteScalar` | 接受一個 Unicode scalar；超過 U+10FFFF 或 surrogate 以 U+FFFD 取代 |
| `At`／`Text(row, begin, end)` | 寬字只保存一份內容；選到半字時略過該字 |
| `MoveTo`／CR／LF／捲動區 | 合法定位會完成未結束文字、取消待折行；非法定位回傳 false 且不改狀態 |
| `Erase`／覆寫 | 涉及寬字任一半時清除完整舊字及屬性；無效範圍拋出例外 |
| `Resize` | 保留左上方可容納的完整字元，不 reflow；重設捲動區、限制游標、取消待折行 |
| `Capture()`／`Reset()` | 深拷貝呈現快照；Reset 清空文字、屬性、未完成序列、游標及捲動區 |

尺寸上限 160 欄／64 列。模型只管理可見列，歷史資料由 `EncodedRowStorage` 負責。

### 解碼、字寬與邊界政策

1. UTF-8 嚴格接受 Unicode scalar，拒絕 overlong、surrogate、超過 U+10FFFF 與不合法 continuation；以 maximal-subpart 恢復，中斷的前綴產生 U+FFFD，造成中斷的位元組重新處理。
2. 同一字元的顏色與 link ID 以第一個位元組為準。
3. 字寬固定取自 `ptt/pttbbs` `68b88d8753d69000033037a0c84a4ee3596b937a` 的 [wcwidth.c](https://github.com/ptt/pttbbs/blob/68b88d8753d69000033037a0c84a4ee3596b937a/common/sys/wcwidth.c)（未定義 `USE_MK_CJK_AMBIGUOUS` 的預設分支，控制字元負寬度轉 0）。例如 U+0121 為 1 格、中文與 U+FFFD 為 2 格、U+0301 為 0 格、U+FF61 為 1 格。不依 locale、UTF-16 長度或字型像素。
4. 零寬字附加到最近寫入字元，不推進游標；每字最多 32 個附加 scalar，沒有前字時略過。
5. 不做正規化，也不把 ZWJ／旗幟／膚色序列合併成瀏覽器式 grapheme。
6. 寬字只剩一格時：折行開啟則換到下一列完整寫入；折行關閉則略過該字。

## 原始列（`EncodedRow`）

來源只有原始位元組及 style，不儲存 codepage；讀取時以 `ReadUtf8(columns)` 或 `ReadLegacy(columns, adapter)` 選擇解讀，不改寫來源。因此全域編碼切換可重新解讀所有既有畫面與歷史。

- 投影欄寬 1～160。`Projection::Text(begin,end)` 輸出完整字元的 UTF-32，`RawText` 輸出對應原始 bytes；選到半個寬字均略過。
- `Source(column)` 回傳該格的原始範圍，`ColumnForByte(offset)` 反向映射。
- 右側放不下完整寬字時只截斷呈現、保留原始資料；用較寬投影重讀可恢復。
- 無效或未完成 UTF-8 呈現 U+FFFD，但來源仍保存原 bytes。
- 每列 payload 上限 21,120 bytes（160 × 33 × 4），超出拋出 `length_error`。
- `ReadUtf8` 會完成列尾未結束前綴，**不得當作網路封包解析器**。

### 更新

更新方法回傳新來源列；範圍錯誤、放不下、配置失敗或超過額度時不改原來源或已取得快照。

| Interface | 政策 |
| --- | --- |
| `ReplaceUtf8`／`ReplaceLegacy` | 以 `[begin,end)` 格範圍替換；較短補空白，較長或裁到字元則拒絕 |
| 空來源作 replacement | 清除選定格；全列清除同時刪掉隱藏後綴與孤立來源 |
| 覆寫／清除舊寬字任一半 | 移除完整舊字及附加字，另一半補預設空白並清掉舊屬性 |
| `SliceUtf8`／`SliceLegacy` | 內部範圍搬移；保留完整字元的 bytes／style，切到半字補空白 |
| `CropUtf8`／`CropLegacy` | 明確、破壞性的右側裁切，被裁掉的資料不會因放大而復活；不是唯讀投影，不可為換錯編碼而暫時不可見的資料呼叫 |

`Byte::boundaryBefore` 標記真正文字／控制或編輯分界，不是傳輸 byte。相同 bytes 若中間發生過定位，重新讀取時不得自動接成新字（例如被分界隔開的 `E4` 與 `B8 AD` 不會合成「中」）。覆寫與範圍複製保留分界；一般 SGR 屬性切換與封包邊界都不是分界。

### Windows／UAO adapter

`WindowsRowDecoder(codepage, converter)` 使用呼叫端已初始化的正式 UAO converter。CP950 沿用 `CUcs2Conv` 的配對範圍，完整配對即使映射失敗仍佔兩 byte／兩格（例如 PTT 的 `C4 A1`）；其他一或雙位元組 codepage 依 DBCS 前導 byte 分段。`CP_ACP` 先解析成實際 ACP；UTF-8、無效 codepage 或需超過兩 bytes 的 codepage 不接受。adapter 使用自有補零輸入暫存，避免 `CUcs2Conv` 多讀一個 byte。

## 稀疏列儲存（`EncodedRowStorage`）

以列配置的身分（`const void*`，不解參考）保存非空來源；未儲存的列視為空，32,767 個空歷史列不需配置格向量。

| 操作 | 行為 |
| --- | --- |
| `At`／`TrySet` | 唯讀取得來源／設定非空列；空來源釋放該身分。`At` 回傳參考，不可跨修改保留 |
| 列指標旋轉 | 來源跟著身分移動，不複製 payload |
| `Erase` | 在回收列之前移除來源，避免位址重用讀到舊內容 |
| `Copy` | 整列深拷貝，含逐 byte style、分界與隱藏後綴；複製空列清空目的地 |
| `Move` | resize 換成新列身分時搬移，不需暫時加倍額度 |
| `Capture`／`Reset` | 最多 64 列的獨立快照／清空舊連線資料 |

額度由 adapter 傳入：列數 1～32,767，payload 總額度大於零。`TrySet`／`Copy` 額度不足回傳 false，不淘汰舊列、不丟字；caller **不可忽略回傳值**，不可先改舊畫面或先推進游標。payload 用量不等於 RAM。此 Module 不追蹤 OSC URL 的生命週期。

## 接收串流解碼（`EncodedTextDecoder`）

不擁有游標、畫面、歷史、全域選單或 socket，只回傳 glyph 批次。caller 須先通過既有 ANSI／Telnet framing，再把文字 bytes 交給它。

| 操作 | 約定 |
| --- | --- |
| `Feed(byte, style)` | 回傳 0～2 個完整 glyph；UTF-8 前綴最多四 bytes，legacy 保留待配對前導 byte |
| `Batch`／`Glyph` | 含 scalar、0／1／2 格寬度、來源 codepage 與原始 `EncodedRow`；無效 UTF-8 為 U+FFFD，來源仍是原 byte |
| `FinishText()` | 真正文字／定位／編碼／EOF 分界才完成未結束前綴並設定下一段分界；不是每個封包結尾 |
| `UseUtf8()`／`UseLegacy(decoder)` | 切換時回傳尚未完成的**舊編碼**來源，caller 須依每個 glyph 的舊 codepage 寫完才繼續新編碼；重選同編碼不 flush |
| `Reset()` | 丟棄舊連線的未完成前綴，保留已選編碼，不清畫面 |
| `Pending()`／`CodePage()` | 純查詢，不改狀態；CPR 查詢不得藉此 flush 前綴、移動游標或取消待折行 |

預設為 UTF-8，但正式 caller 必須依全域編碼選單選用 codec。例外發生時單次 `Feed`／`FinishText` 的解碼狀態與 pending 原文會回復，新 closure 在完成舊文字前先建好；這不代表畫面與歷史已交易式提交。

`WriteUtf8Glyph`／`WriteLegacyGlyph` 把一個完整 glyph 寫入指定格，放不下時拒絕，由 caller 的唯一游標決定折行。只有同一文字段、緊鄰上一次寫入時才傳 `continuedRun=true`，否則每個 scalar 都會被誤當控制分界，破壞 UTF-8 重新以 Big5 解讀時跨 scalar 的配對。

`Style.link` 只是 opaque 數字，正式 caller 須另外保存 OSC target 的生命週期。

## 測試

| 測試專案 | 範圍 |
| --- | --- |
| `TerminalCellModelTests` | 全部 1,112,064 個 Unicode scalar 的往返與寬度、固定字寬 golden vectors、所有封包切點、折行與寬字邊界、捲動區、snapshot、resize、`EncodedRow` 讀取／更新／裁切／分界、儲存生命週期，以及固定種子的混合操作 |
| `WindowsTerminalRowTests` | 以正式 `Ucs2Conv.cpp` 與版控 B2U 比對 65,536 組雙 byte 與 256 組單 byte；golden strings、`C4 A1`、雙半格屬性、範圍複製、裁切 |
| `EncodedTextDecoderTests` | 串流解碼、跨封包邊界、編碼切換、故障回復 |

大量檢查是回歸保護，不等於同等數量的獨立使用者情境，也不是無上限 fuzz。`WindowsTerminalRowTests` 驗證 adapter 不改變既有字碼表，不獨立證明 UAO 表的語意正確。建置與執行方式見 [Tests/README.md](../Tests/README.md#自動測試索引)；`WindowsTerminalRowTests` 需傳入 B2U 所在目錄，例如 `Lite/Release/PCMan`。
