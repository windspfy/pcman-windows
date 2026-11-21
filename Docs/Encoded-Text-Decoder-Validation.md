# 第二階段 C：接收串流解碼 adapter

日期：2026-10-18。基準：`ba0cc1f` 加上本輪未提交修改。
狀態：串流解碼 Module 與離線解析串接測試完成；**正式 `OnText`／畫面尚未切換，CPR 仍不回覆**。
這是正式整合前的接收層，不是已修正目前 PCMan 游標／繪圖的宣告。
前置見 [模型方案](UTF8-Cell-Model-Plan.md)及[列生命週期](Encoded-Row-Lifecycle-Validation.md)。

## 提交核對

已確認使用者提交 `ba0cc1f`，前輪三個原始列／Windows adapter／儲存標頭均已納入版控，`a4ab8af` 漏檔問題已補齊。
本輪新增 `Lite/EncodedTextDecoder.h`、`Tests/EncodedTextDecoderTests.cpp/.vcxproj`；提交時須納入這三個新檔，以及本輪既有檔案修改。
沒有代為 stage／commit／push，個人設定與 Toolbar 修改保留未動。

## Interface 與責任

依 codebase-design 將接收的原始 bytes、解碼狀態及格數集中成一個 Module。
`EncodedTextDecoder` **不擁有游標、畫面、歷史、全域選單或 socket**，回傳資料批次，不能直接更新一個 CPR 專用游標。
正式 caller 應先通過既有 ANSI／Telnet framing，再交給文字解碼器；不是把完整 ANSI 串流先轉碼。

| 操作 | 約定 |
| --- | --- |
| `Feed(byte, style)` | 接受一個已分派的文字 byte；UTF-8 前綴最多四 bytes，legacy 保留待配對前導 byte；回傳 0～2 個完整 glyph，無封包結尾 flush |
| `Batch`／`Glyph` | 每個 glyph 含 scalar、0／1／2 格寬度、來源 codepage、原始 `EncodedRow`。無效 UTF-8 呈現 U+FFFD，但來源仍是原 byte，重新處理的下一字是獨立 glyph |
| `FinishText()` | 真正文字／定位／編碼／EOF 分界才完成未結束前綴；設定下一段來源分界，重複呼叫沒有多餘輸出。不是每個封包結束時呼叫 |
| `UseUtf8()`／`UseLegacy(decoder)` | 切換時回傳尚未完成的**舊編碼**來源；caller 必須依每個 glyph 的舊 codepage 完成寫入，再繼續新編碼。重選同編碼不 flush |
| `Reset()` | 丟棄舊連線未完成前綴，保留已選編碼，下一 byte 開始新來源段；不清空畫面／歷史 |
| `Pending()`／`CodePage()` | 純查詢，不改前綴、不建立字元、不移動任何游標 |

預設純 C++ 解碼器是 UTF-8；這不是修改應用程式的預設編碼。
正式 caller 必須依既有全域選單選用 codec，`CP_ACP` 先解析成實際 Windows codepage，不能把這個快取誤做每連線的使用者設定。
UTF-8 採既有嚴格 decoder 及固定 PTT 字寬；legacy 使用傳入 adapter 的 lead 分類／映射／格數。
`WindowsRowDecoder` 新增 `CodePage()`／`IsLead()`，重用原 UAO 表與 Windows DBCS 分類，不新增編碼偵測。

所有輸入 bytes 的 style 都保留；UTF-8 呈現沿用第一 byte style，Big5 可保留左右半格不同顏色。
`Style.link` 仍只是 opaque 數字：正式 caller 必須另外保留前綴所用的 OSC target 生命週期，本輪沒有完成 URL 所有權整合。

解碼／adapter／輸出配置發生例外時，單一 `Feed`／`FinishText` 的解碼狀態與 pending 原文會回復；不會只回傳前半段結果或丟掉待續傳前綴。
新 adapter closures 在完成舊文字前先建好，失敗不切換到新編碼。
這只保護解碼 Module，**不等於畫面、歷史與網路資料已完成交易式提交**；caller 仍須處理額度與停止接收。

## 零寬字來源附加

`WriteUtf8Glyph`／`WriteLegacyGlyph` 將一個完整接收 glyph 寫入指定格，寬字放不下時拒絕，由 caller 的唯一游標決定折行。
預設保留編輯分界；只有同一文字段、緊鄰上一次寫入時才傳入 `continuedRun=true`，沿用 decoder 的真實來源分界。
否則每個 Unicode scalar 都會被誤當控制分界：例如 UTF-8 的「中A」換成 Big5 重新解讀時，原 bytes 本可跨舊 scalar 配對，不能被人為切開。
真正定位／覆寫或 decoder 標記的新文字段仍須隔開；此方法不另外持有游標或判斷接收次序。

`EncodedRow::AppendUtf8Marks(columns, leadColumn, suffix)` 讓正式 writer 可以把零寬來源附加到既有主格，而不用以空欄位替換或重算游標。
只接受完整、非控制的零寬 scalar；拒絕跨真實文字分界、寬字後格或沒有原字的虛擬空白。
回傳新來源，不改舊列／快照，不轉碼原始 bytes；文字呈現仍依原有最多 32 個附加字政策及來源 payload 額度。
caller 必須先判斷是否有有效附著點；正常孤立零寬字依既有政策略過，不可因錯誤呼叫此方法就當作記憶體不足中止連線。

## 額度耗盡：使用者已確認的正式行為

使用者選擇：**停止該連線、保留最後完整畫面，並顯示錯誤原因**，不默默丟字或淘汰歷史。
這是本輪確認的需求，**正式失敗路徑尚未接入或驗證**，不是已完成的斷線功能。

正式整合時必須：

1. 在來源保存及更新成功前，不先推進游標或覆寫顯示資料；所有 `TrySet`／`Copy` 失敗都要處理。
2. 中止後續接收，保留最後成功提交的模型；顯示明確原因。這次失敗避開自動重連與自動關閉分頁，否則無法保留畫面。
3. 若 DEC2026 同步尚未完成，保留上一完整呈現快照與 OSC 目標，不可經由逾時／`OnClose` 清掉快照後顯示半成品。
4. 沒有同步邊界時，不能知道站台整頁何時完整：只能保留最後成功提交的一致模型狀態，不宣稱可還原任意未同步整頁。
5. 以小額度注入測試真正接收／斷線路徑，而非用大量資料耗盡使用者記憶體。實際總額度、錯誤提示及快照接點仍待實作。

## 自動測試

Debug／Release Win32 完整重建，以及 Release ASAN 同套測試均通過：

- 第八組格模型／原始列：**2,551,215** 項，新增零寬來源附加與非法附著點保護。
- 第九組正式 Windows／UAO：**131,351** 項，新增真正 UAO 與 Shift-JIS 串流及編碼切換。
- 第十組 `EncodedTextDecoderTests`：**325,461** 項，無 MFC／網路／剪貼簿依賴。

第十組包括 UTF-8／legacy 跨封包、所有測試字串切點、malformed-prefix 回復、逐 byte style、重連 reset、兩解碼器隔離、舊編碼 flush、失敗回復與無效 adapter 結果。
65,536 種雙 byte 輸入與共用嚴格 decoder 做 scalar 差異回歸，另獨立核對每個原始 byte、style 與來源分界；大量檢查不是同等數量的獨立使用者情境。

離線 framing fixture 直接使用正式 `AnsiSequenceParser`／`Sgr66::PendingAttribute`，檢查指定 SGR、OSC、同步事件與純查詢插在文字附近時不漏 payload、不意外 flush。
fixture 只實作文件所述的測試控制動作，**沒有呼叫正式 `CTelnetConn::OnText`、沒有完整 SGR handler 或模擬第二個終端游標**；也不宣稱完成 DCS 安全吸收或 CPR。
legacy 純 C++ 測試替身只驗證解碼器 interface，真正 UAO／Shift-JIS 另由第九組測試，不拿替身當字碼表驗證。
另有 decoder glyph → 原始列 → UTF-8／legacy 投影的離線寫入測試，區分連續接收段與真正編輯分界；不是正式 BBS 游標測試。

```powershell
MSBuild Tests/EncodedTextDecoderTests.vcxproj /t:Rebuild /p:Configuration=Debug /p:Platform=Win32
./Tests/bin/Debug/EncodedTextDecoderTests.exe
MSBuild Tests/EncodedTextDecoderTests.vcxproj /t:Rebuild /p:Configuration=Release /p:Platform=Win32
./Tests/bin/Release/EncodedTextDecoderTests.exe
```

本輪沒有重建主程式、重跑原七組／PowerShell fixture、啟動 GUI、操作剪貼簿或登入站台；ASAN 只調整測試程序 runtime PATH。
目前不需新人工畫面驗收。下一步仍需將 decoder、來源列、唯一游標與正式繪圖／選取／連結／同步／匯出一致掛接，完成上述失敗處理後才切換 live 路徑；CPR 留第三階段。
