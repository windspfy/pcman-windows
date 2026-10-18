# 第二階段 A：原始列保存與編碼投影

日期：2026-10-18。基準：第一階段提交 `5879222` 加上本輪未提交修改。
狀態：原始列 Module、Windows／UAO adapter 及測試完成；**尚未接入正式接收／畫面，第二階段整體未完成，CPR 仍不回覆**。
前置：[格模型方案](UTF8-Cell-Model-Plan.md)、[第一階段驗收](Terminal-Cell-Model-Validation.md)。

## 已確認的需求

使用者選擇保留全域編碼切換，並重新解讀所有分頁的既有畫面與歷史資料。
不能改成「只影響後續接收」，也不能把原始資料先轉成 Unicode，再嘗試反向轉碼還原。
本輪建立支持此需求的資料保存層，尚未改動正式編碼選單或替換歷史緩衝區。

依 codebase-design 的 Module／Adapter 分工：原始列集中保存及映射，Windows 轉碼使用獨立 adapter；
原始列不開 socket、不操作全域選單、GDI 或剪貼簿，也不持有第二套游標。

## 新增與修改

- `Lite/EncodedTerminalRow.h`：`EncodedRow` 保存原始位元組及逐 byte 的顏色／opaque link ID，產生 UTF-8 或 legacy 的 `Projection`。
- `Lite/TerminalCellModel.h`：既有 `Utf8Decoder` 新增 `FeedWithLength`／`FinishWithLength`；沿用同一解碼流程，額外回報每個 scalar／錯誤 maximal subpart 消耗的原始長度。既有 scalar-only interface 不變。
- `Lite/WindowsTerminalRowDecoder.h`：重用正式 `CUcs2Conv::Big52Ucs2` 與已載入 B2U；其他 legacy 編碼使用 Windows 轉碼。未更改原轉碼來源或表格。
- `Tests/TerminalCellModelTests.cpp/.vcxproj`：第八組增加原始範圍、投影及編碼往返測試。
- `Tests/WindowsTerminalRowTests.cpp/.vcxproj`：獨立第九組，直接編譯正式 `Lite/Ucs2Conv.cpp`，唯讀使用版控 B2U，不擷取假的 UAO 對照表。

主程式的 `OnText`、繪圖、選取、編碼選單、同步快照及 CPR 尚未接入新 Module；`Lite.vcxproj` 沒有引用新標頭。
因此本輪不改變目前 PCMan 顯示／接收行為，不能把以下測試當成正式 UTF-8 顯示修正。

## Interface 與保存政策

`EncodedRow` 的來源只有原始位元組及 style，不儲存一個全域或每連線的 codepage。
呼叫端讀取時選用 `ReadUtf8(columns)` 或 `ReadLegacy(columns, adapter)`；來源不被讀取改寫。

- 投影欄寬限 1～160；一個寬字有一個主格／後格，UTF-8 沿用第一階段 PTT 字寬與最多 32 個零寬附加字政策。
- UTF-8 兩半格沿用第一 byte style；legacy 可保存左右半格不同 style。所有原始 byte 的 style 仍保留，不因投影而歸一化。
- `Projection::Text(begin,end)` 匯出格範圍內完整字元的 UTF-32；`RawText` 匯出對應原始 bytes，兩者用途不同。選半個寬字均略過該字，右側虛擬空白按空白匯出。
- `Source(column)` 提供該格對應的原始範圍；`ColumnForByte(offset)` 把字內位元組與組合附加字映射回主格。超出投影寬度的來源映射到 `Columns()`，不是合法畫面格。
- 投影右側不足以放完整寬字時，截斷呈現後綴、保留原始資料，不跨列折行。重新以較寬投影讀取可恢復內容；**不等於正式 resize 保證恢復已刪除的資料**。
- 無效／未完成 UTF-8 呈現 U+FFFD，但來源仍保存原本 bytes；不是將來源替換為 U+FFFD 的 UTF-8。孤立及超額零寬字不呈現，但原始來源仍在。
- 投影是獨立 value snapshot；來源列被替換後，已取得投影仍擁有自己的文字、來源、顏色及 link ID。這不是已接入 DEC2026 的證明。
- 每列來源 payload 上限為 21,120 bytes（160 × 33 × 4），超出拋出 `length_error`。這不是每列實際 RAM 用量；style、格向量及來源映射另有成本。
  空來源不配置原始空白；尚未實作整個歷史緩衝區，不宣稱已完成 scrollback 效能或總量管理。

`ReadUtf8` 是一個完整儲存列的讀取，因此會完成列尾未結束前綴；**不得把它當作網路封包解析器**。
接收時仍應使用各連線的串流解碼器；不能在每個封包結尾呼叫 `FinishWithLength`。

## Windows／UAO adapter

`WindowsRowDecoder(codepage, converter)` 使用呼叫端已初始化的正式 UAO converter，不自行載入／更換表格。
CP950 沿用 `CUcs2Conv` 的配對範圍；完整配對即使映射失敗仍消耗兩 byte／兩格，PTT 的 `C4 A1` 不會被捨棄而改變欄數。
其他一／雙位元組 Windows codepage 依 DBCS 前導 byte 分段並轉碼；CP932 單 byte 半形片假名保持一格。
`CP_ACP` 解析成當前 Windows ACP；如果 ACP 是 UTF-8，呼叫端須改走 `ReadUtf8`。
UTF-8、無效 codepage 或需要超過兩 bytes 的 legacy codepage 不接受為此 adapter；不是新的自動偵測編碼功能。

既有 `CUcs2Conv` 即使輸入長度為 1 仍讀取下一 byte；adapter 一律提供自有、補零的三 byte 輸入暫存，避免讀取來源列以外記憶體。
沒有順便修改舊 converter 的其他呼叫端、表格載入失敗處理或檔案驗證；這些既有限制不宣稱已修正。

## 自動測試結果

Debug／Release Win32 完整重建並執行通過：

- 第八組：**2,422,207** 項（原 2,286,759；新增 135,448），另以 Release ASAN 通過同一套最終檢查。
- 第九組：**131,339** 項；Release ASAN 同樣通過，包含真正 `Ucs2Conv.cpp` 的執行。

第八組涵蓋來源長度分割、malformed-prefix 後重新處理、補充平面、半字選取、零寬附加／上限、列尾前綴、隱藏後綴恢復、空列、來源額度、style／link 保存、快照及非法 adapter 結果。
另有 1,000 列固定種子位元組資料，反覆以 UTF-8／測試用 legacy adapter 讀取，檢查格配對、來源範圍、byte→格映射與來源未被改動；不是無限 fuzz。
原有全 Unicode scalar 解碼測試會經過新的同一解碼流程。

第九組逐一檢查 65,536 組雙 byte 輸入及 256 組單 byte，與既有正式 UAO converter 比對。
這是 adapter 不改變既有字碼表的差異回歸，**不是獨立證明整份 UAO 表的語意正確**。
另有已知中文／GBK／Shift-JIS／西歐字元 golden strings、`C4 A1` 及實際來源 bytes 的往返檢查。
第八組 legacy 測試替身僅驗證純 C++ interface；不拿它代替第九組正式 UAO 測試。

第九組新測試碼採 `/W4 /WX /utf-8`；原 `Ucs2Conv.cpp` 保持來源不變，僅該翻譯單元不將既有警告提升為錯誤。
ASAN 只為該程序加入已安裝 x86 runtime 路徑，沒有更改系統 PATH 或安裝套件。
沒有啟動 PCMan GUI、操作剪貼簿或登入 PTT；不需要現在重跑人工畫面，接入正式程式後再提供驗收腳本。

```powershell
MSBuild Tests/TerminalCellModelTests.vcxproj /t:Rebuild /p:Configuration=Debug /p:Platform=Win32
./Tests/bin/Debug/TerminalCellModelTests.exe
MSBuild Tests/WindowsTerminalRowTests.vcxproj /t:Rebuild /p:Configuration=Debug /p:Platform=Win32
./Tests/bin/Debug/WindowsTerminalRowTests.exe Lite/Release/PCMan
```

以上從倉庫根目錄執行；最後參數是版控 B2U 所在目錄，不是個人 Config 目錄，不需 U2B 或連線設定。
Release 同理改用對應組態及執行檔；不依賴主程式 EXE 資源。

下一步是原始列的覆寫／清除／捲動／resize 及正式接收、顯示、選取、OSC、同步、文章匯出接入，仍屬第二階段。
全域歷史重新解讀的使用者操作與切換中的未完成序列／游標仍需正式路徑驗證；不能僅憑唯讀投影測試宣布整項需求完成。
