# DECAWM／DECSTBM 實作與 VCOL 回歸驗收

日期：2026-10-17。基準：`master` `0a8cfaa`（SGR 66 及全域選單已隨 PR #9 合併）。
本輪已實作並完成下列自動測試；2026-10-17 使用者確認本機人工流程及實際站台測試均正常，驗收完成。
2026-10-18 合併核對：正式程式已隨 PR #10 合併至 `master`（`238922f`，功能提交 `3ee286b`）；該 PR 僅含三個正式程式檔，相關測試、腳本與本文件尚未提交。
實際站台驗收由使用者操作；不以工具自動登入、發表文章、刪除信件或修改個人設定。

## 公告依據與工作範圍

本輪重新取得三篇官方公告原文，不僅依賴前次摘要：

- [DECAWM 自動折行](https://www.ptt.cc/bbs/PttCurrent/M.1790662175.A.563.html)：接收 `ESC[?7h`／`ESC[?7l`，開啟／關閉右邊界折行；單向，不回覆。公告允許忽略，本輪選擇支援切換。
- [DECSTBM 捲動範圍](https://www.ptt.cc/bbs/PttCurrent/M.1790697156.A.14E.html)：接收 `ESC[top;bottomr`／`ESC[r`，限制／重設垂直捲動範圍；執行後游標回 Home，不需要站台另外送定位指令。
- [VCOL 動態分欄](https://www.ptt.cc/bbs/PttCurrent/M.1790827526.A.744.html)：列表欄寬、空白及省略內容可變；不是新增終端指令，不在客戶端重做站台排版。

最右欄覆寫、至少兩列的有效捲動區及 Home 語意另核對 [VT100 原廠手冊](https://vt100.net/docs/vt100-ug/chapter3.html#DECAWM)、[DECSTBM](https://vt100.net/docs/vt100-ug/chapter3.html#DECSTBM)。

## 已實作行為

### DECAWM

- 解析器以專用事件傳遞模式，不把 `?7` 當成普通 `CSI 7h/l`；可跨封包、逐位元組接收，接受數值前導零。
- 每連線保存暫時的遠端模式，未收到指令前使用原本 `line_wrap` 偏好；重連／清除連線狀態後回復本機偏好。
- `?7h`：寫滿後等下一個可列印位元組才換行；`?7l`：不換行，之後寫入覆蓋最右位置。
- 不修改磁碟設定或其他連線。ANSI 編輯器忽略這兩個模式，不改原本的編輯／插入行為。
- CR、LF、BS、HT 及游標定位消除待折行位置；SGR 可保留待折行，使下一行字元仍套用新顏色。
- 修正 BBS 游標向右移到邊界後誤觸折行；ANSI 編輯器仍允許原有列尾編輯位置。
- 保留既有未收到遠端模式時的行為：預設關閉折行，超出列寬的資料仍裁切，不突然改為覆寫尾端。
- 僅支援公告中的單一模式形式。含多個 private mode 的參數串（例如 `?7;1000h`）、中間字元、子參數、過長／溢位及未知指令仍完整忽略，不部分執行。

### DECSTBM 與 resize

- 省略或 0 的上下界使用全螢幕預設，超出本機列數的值限制在本機畫面內。
- 有效範圍至少兩列；相等／反向範圍與多於兩個參數的請求不改變原有範圍及游標。
- 有效設定與 `ESC[r` 重設均將游標移到畫面 `(1,1)`。未新增 DECOM 原點模式；仍採畫面座標。
- LF 僅在游標位於捲動區底部時捲動；位於區外則在畫面邊界內移動，不誤捲指定區域。
- 部分捲動區不移動區外畫面或 scrollback；全螢幕捲動保留原本歷史緩衝區行為。
- 改變緩衝區尺寸時重設上下邊界，限制 BBS 游標在新可見範圍；遠端折行模式維持至連線重設。
- 順帶修正 `ResizeLine` 原本讀取列尾屬性 metadata 時的錯誤位移，避免讀到配置區域之外；測試驗證尾端四個位元組保留。
- 維持 UI 的 24～64 列與 40～160 欄限制，不為模擬公告 150 列情境解除上限。

### VCOL

查核 `MouseCTL.cpp`、`BBSHyperLink.cpp`、`CheckStrTrigger` 與文章進度／下載路徑：
未發現內建功能依文章／信件／看板／使用者列表的固定作者或標題欄位擷取資料，因此沒有新增列表切片或正規表示式解析器。
文章下載仍以底列的百分比／行號判斷進度，不把列表誤認成文章。
自訂觸發器仍按原始接收區塊比對，不自動把使用者規則改成 regex，也不保證跨區塊比對。
若規則依賴特定空白、完整標題或尾端文字，應由使用者檢查與調整。

## 限制

- 本輪不是完整 VT100 模擬器。未擴大實作 DECOM、CPR、滑鼠回報或其他 private mode。
- 沿用既有位元組畫面／游標模型；UTF-8 寬字顯示欄寬仍有原有限制，不宣稱解決 Unicode 欄寬或所有跨行多位元組字元。
  Big5 測試涵蓋完整雙位元組字元恰好位於右邊界後的折行，不代表字元從最後一欄起始時的完整字形排版也已支援。
- 文章進度仍讀本機最後一列。若其他站台把進度列放在捲動區底而非畫面底部，仍可能保守拒絕／等待；不改成掃描任意畫面猜測進度。
- 實際站台測試依使用者回報記錄為正常；未提供逐項站台、組態、編碼及尺寸組合，不擴大宣稱所有部署／情境均已驗證。模擬資料本身不是實際 VCOL 畫面驗收證據。

## 合併後待辦

先補交 `TerminalBoundaryTests.cpp/.vcxproj`、`Extract-BoundaryMethods.ps1`、兩個正式 TerminalBoundary fixture 腳本，
以及 `AnsiSequenceParserTests.cpp`、`Sgr66Tests.cpp` 的配套修改和相關文件，再以乾淨 checkout 重驗。
`Sgr66Tests.cpp` 的接收替身需有新增的 `remote_line_wrap`；只取得 PR #10 的三個正式檔案不等於已取得完整測試配套。
個人設定、建置產物及另存的 `Start-TerminalBoundaryFixture2.ps1` 不在自動補交範圍。
後續功能順序見 [合併核對與下一步](PTT-Compatibility-Checklist.md#2026-10-18-合併核對與下一步)。

## 自動測試

以下為合併前本機工作目錄（含尚未提交的測試配套）之既有驗證紀錄；2026-10-18 僅核對合併與更新文件，未重新執行，不宣稱純 master 已通過相同測試。

新增 `Tests/TerminalBoundaryTests.vcxproj`，在建置時擷取正式接收、CSI 分派、游標定位／向右移動、捲動範圍、LF、resize、列初始化、連線狀態重設及文章進度方法。
畫面／網路 UI 使用替身，文字、顏色、OSC metadata、同步快照與緩衝區指標使用正式方法／模組，不維護一份假的捲動演算法。
第一個測試已在修正前重現 DECSTBM 沒有 Home；後續也重現 DECAWM 開啟未生效，以及游標向右到邊界後誤折行。

Debug／Release 主程式均建置成功，全部七組測試均重新建置、執行通過：

| 測試 | 每組態檢查數 |
| --- | ---: |
| TerminalBoundaryTests | 256 |
| AnsiSequenceParserTests | 11,759 |
| KeepAliveTests | 2,318 |
| SynchronizedOutputTests | 100 |
| ArticleProgressTests | 810 |
| OscHyperlinkTests | 1,343 |
| Sgr66Tests | 197,753 |

新增測試包含全部封包切點、逐位元組、模式與設定隔離、連線重設、有效／無效上下界、區外 LF、scrollback、resize、40／80／120／160 欄與 24／32／64 列、OSC 隨列移動、同步擷取屏障及底列保護。
另有 Windows PowerShell 的 `Tests/Test-TerminalBoundaryFixture.ps1`：30 項檢查，使用真正 loopback TCP 測完整腳本、resize／略過／結束、連接埠釋放與不誤宣告 GUI 通過。

```powershell
MSBuild Tests/TerminalBoundaryTests.vcxproj /t:Rebuild /p:Configuration=Debug /p:Platform=Win32
./Tests/bin/Debug/TerminalBoundaryTests.exe
MSBuild Tests/TerminalBoundaryTests.vcxproj /t:Rebuild /p:Configuration=Release /p:Platform=Win32
./Tests/bin/Release/TerminalBoundaryTests.exe
powershell -NoProfile -File Tests/Test-TerminalBoundaryFixture.ps1
```

## 人工驗收紀錄

2026-10-17：使用者確認本機人工測試已完成，並另確認實際站台測試也沒有問題；本輪 DECAWM／DECSTBM 與站台介面回歸依此記為驗收完成。
未另提供 Debug／Release、PTT1／PTT2、連線方式、編碼、尺寸與每個測試項目的對照，不將此回報擴大為所有組合、Unicode 邊界或自訂觸發器皆已驗證。

E 測試先前的頂端標記未捲走，確認操作的是「PCMan 所有選項設定」（全域預設），並未調整已開啟分頁的緩衝區尺寸。
改用「修改目前連線的設定值」後，使用者確認測試完成；此情況不記為 DECSTBM 實作缺陷，也不歸因於未證實的封包時序問題。

## 人工本機驗收流程（使用者確認完成，保留供重驗）

```powershell
powershell -NoProfile -File Tests/Start-TerminalBoundaryFixture.ps1
```

1. 60 秒內開啟新的 `telnet://127.0.0.1:22307` 分頁，Big5／UAO、80×24；從「選項 → 修改目前連線的設定值 → 站台選項」確認實際連線尺寸，不只查看全域預設。不要套用帳密、自動登入、巨集或觸發器。所有 Enter／Q 都在 PowerShell 視窗操作。
2. A：第 3 列最右四格為 `ABCG`，第 4 列不變；確認遠端關閉折行，即使本機偏好原為開啟也不應換行。
3. B：第 6 列最右為 `ABCD`，第 7 列開頭為紅色 `EFG`；只換行一次，分段指令不顯示控制文字。
4. C1：設定捲動區後，`HOME` 直接出現在第 1 列第 1 欄；沒有額外 CUP 掩蓋歸位結果。
5. C2：只有第 2～22 列上捲；第一列及最下兩列不動，第 22 列顯示 `NEW BOTTOM`。
6. C3：`OUTSIDE` 出現在最後一列，原捲動區不被再次捲動。
7. D：`ESC[r` 後，`RESET HOME` 從第 1 列第 1 欄開始。
8. E：等 PowerShell 提示調整尺寸後，開啟「選項 → 修改目前連線的設定值 → 站台選項」。若勾選「使用『PCMan 各項設定』中的全域設定」，先取消勾選，讓個別尺寸可以編輯。
   將「列數」24 改為 32，「行數」保持 80，按確定後回到 PowerShell 輸入 `32`。不需拖曳實體視窗；拖曳只改變視窗外觀，不能取代終端列數設定。
   `RESIZE TOP MUST SCROLL` 應捲出目前可見畫面（可能留在歷史緩衝區），第 32 列出現 `RESIZE BOTTOM`，不可殘留舊局部捲動區；原有 `ROW xx` 標籤不會重新編號。留空只會記為略過，不算完成。
   「選項 → 自訂 PCMan 各項設定」開啟的是全域預設，不會立即改變已開啟分頁的尺寸。若 E 的資料已送出後才發現設錯，應重啟腳本並以新的 80×24 分頁重測，不能只改尺寸就判定原次結果。
9. 關閉測試分頁，重跑腳本後重新連線，確認新連線不繼承舊捲動區／遠端模式；本機偏好未被指令改寫。
10. Debug／Release 各測一次；可用 `-Width 120 -Rows 32` 或 `-Width 160 -Rows 64` 重驗，PCMan 連線尺寸必須與參數一致。

腳本只負責提供畫面，不自動判斷通過；不回顯／儲存使用者傳入內容，不應輸入任何帳密。

## VCOL 實際站台回歸（使用者回報正常，保留供重驗）

使用者已確認實際站台測試沒有問題；以下保留建議檢查範圍，不代表已收到每個組合的逐項結果。

以平常使用的站台／連線方式，記錄 PTT1／PTT2、Debug／Release、編碼與終端尺寸：

1. 先以 80×24 檢查看板、文章、信件及使用者列表：選取列、上下翻頁與進出列表正常。
2. 再以 120／160 欄檢查長標題、看板描述、欄位延展與尾端 `…`；不得出現控制字串、殘字或非預期換行。
3. 檢查空列表／空搜尋結果，以及返回列表後是否清除前一畫面的長內容。
4. 複製包含中文與省略號的選取範圍；一般文章與帶色文章下載仍完整、不因列表欄位變動誤判進度。
5. 若有自訂觸發器／巨集，檢查依賴空白或完整標題的條件；未使用記為 N/A，不要求新建規則。
6. 不需發表測試文章或刪除信件。結果可接續記入 [新介面回歸紀錄](PTT-Interface-Regression.md)。
