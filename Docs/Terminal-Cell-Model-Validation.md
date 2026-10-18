# UTF-8 畫面格模型：第一階段實作與測試

日期：2026-10-18。基準：`master` `8590116` 加上本輪未提交修改。
狀態：第一階段已實作並通過自動測試，尚未接入正式 BBS／ANSI 編輯器、未啟用 CPR。
前置設計與來源見 [模型方案](UTF8-Cell-Model-Plan.md)及[CPR 研究](CPR-Research.md)。

## 本輪檔案與責任

- `Lite/TerminalCellModel.h`：純 C++17 的 `Utf8Decoder`、`Screen`、`Cell`、`Style` 與呈現快照。
- `Lite/PttWidthProfile.h`：固定 PTT CJK 字寬政策，不依系統 locale、UTF-16 長度或字型像素計算。
- `Tests/TerminalCellModelTests.cpp/.vcxproj`：獨立第八組測試；無 MFC、GUI、網路或剪貼簿依賴。

未修改正式 `OnText`、繪圖、選字／複製、編碼選單及 `DeviceStatusReport`，也未將新模型接入 `Lite.vcxproj`。
因此目前 PCMan 畫面與 CPR 行為不變；不以本輪模型測試宣稱已修正實際 UTF-8 顯示。

## Interface 與不變條件

`Screen` 使用零起算的格座標：`Cursor().column == 1` 代表下一個畫面位置是第 2 欄；這不是已送出的 CPR。
可見游標永遠在格範圍內，待折行獨立存於 `wrapPending`；重複讀取 `Cursor()` 不消耗待折行。

| 操作 | 約定 |
| --- | --- |
| `WriteUtf8(bytes, style)` | 只接收外部分派器已分出的文字；不自行解析 ANSI／Telnet。未完成 UTF-8 保留至後續呼叫，空字串／封包結尾不 flush |
| `FinishText()` | EOF 或真正的文字／控制切換時，將未完成前綴轉成一個 U+FFFD；重複呼叫無額外效果。不是每個封包結束時呼叫 |
| `WriteScalar` | 接受一個 Unicode scalar；先完成尚未結束的 UTF-8 前綴。超過 U+10FFFF 或 surrogate 值以 U+FFFD 取代 |
| `At`／`Text(row, begin, end)` | 讀取格或 `[begin,end)` 格範圍的 UTF-32 文字；寬字只保存一份內容，選到半字時略過不完整字元，不回傳半個 scalar |
| `MoveTo`／CR／LF／捲動區 | 是模型操作，不是 ANSI 解析器；合法定位會完成未結束文字、取消待折行及零寬附著點。非法定位／區域回傳 false，不更動狀態 |
| `Erase`／覆寫 | 涉及寬字任一半時清除完整舊字及其屬性，避免孤立後格；無效範圍拋出例外 |
| `SetStyle` | 可分別設定寬字左右半格的屬性與 opaque link ID；並非已實作 SGR 66／OSC 8 協定 adapter |
| `Resize` | 保留左上方可容納的完整字元，不 reflow；裁切的寬字整個略過，重設全畫面捲動區、限制游標並取消待折行。未完成解碼仍保留 |
| `Capture()` | 深拷貝文字、左右格屬性及可見游標；是呈現快照，不含可續傳連線所需的完整解碼／協定狀態 |
| `Reset()` | 清空文字、顏色／link ID、UTF-8 未完成序列、附著點、游標及捲動區，恢復模型預設開啟折行 |

模型尺寸上限為現有 UI 的 160 欄／64 列；允許 1 格大小供小範圍驗證，不改動 UI 的最小值。
建構／resize 在計算尺寸或配置前拒絕不合法數值；非法 resize 不破壞原畫面。
目前只管理可見列，不直接以這種配置預先建立 32,767 列 scrollback；歷史資料儲存與效能留待正式整合設計。

## 解碼、字寬與邊界政策

1. UTF-8 嚴格接受 Unicode scalar：拒絕 overlong、surrogate、超過 U+10FFFF 與不合法 continuation。
   不合法輸入按 maximal-subpart 恢復；遭中斷的前綴產生 U+FFFD，造成中斷的 ASCII／新前導位元組會重新處理，不吞掉後續正常字元。
2. 同一字元的顏色與 link ID 以第一個位元組為準；跨呼叫變換 style 不會讓一個字變成不同來源的文字。
3. 字寬固定自 `ptt/pttbbs` `68b88d8753d69000033037a0c84a4ee3596b937a` 的
   [wcwidth.c](https://github.com/ptt/pttbbs/blob/68b88d8753d69000033037a0c84a4ee3596b937a/common/sys/wcwidth.c)：
   選用未定義 `USE_MK_CJK_AMBIGUOUS` 的預設分支，控制字元的負寬度轉 0。
   原始 Markus Kuhn 授權及註記保留；使用 `char32_t` 避免 Windows `wchar_t` 僅 16 位元造成截斷。
   這不是宣稱支援最新 Unicode 字寬資料或所有部署使用相同政策。
4. 例如 U+0121 為 1 格、中文及 U+FFFD 為 2 格、U+0301 為 0 格；預設伺服器表的 U+FF61 為 1 格，
   不混入網頁版 UAO override 的 2 格結果。這是明訂的來源差異，不是依 UTF-8 位元組數判斷。
5. 零寬字附加至最近寫入字元，不推進游標或觸發待折行；沒有可附著前字時略過，定位／CR／LF 等會結束附著。
   每個字最多保存 32 個零寬附加 scalar，超過略過，避免無限記憶體成長；附加字沿用主字的屬性。
   與 PTT `pfterm` 直接略過零寬字不同，本模型保留受限的文字內容，但格數仍遵循固定政策。
6. 不執行正規化，也不將 ZWJ／旗幟／膚色等序列自動合併成瀏覽器 grapheme 的格數；emoji 各 scalar 仍按 PTT 政策計算。
   文字保存不等於已完成 GDI 塑形與所有組合字渲染。
7. 寬字只剩一格時：折行開啟則清除末格後換到下一列完整寫入；折行關閉則略過該字，保留原畫面。
   單欄模型無法容納寬字時直接略過，不消耗待折行。此原子寫入政策尚須在第二階段比對實際站台與繪圖 adapter。

## 自動測試結果

Debug／Release Win32 均以 v143、C++17、`/W4 /WX /utf-8` 完整重建，每組態通過 **2,286,759** 項檢查。
Release 另以 `/fsanitize=address` 建置，使用對應 v143 的 x86 ASAN runtime，執行同一組檢查全部通過，未回報記憶體錯誤。
ASAN 初次執行缺 runtime DLL；加入已安裝 runtime 的路徑至該次程序 PATH 後通過，未更動系統 PATH 或安裝套件。

涵蓋：

- 全部 1,112,064 個合法 Unicode scalar 的 UTF-8 解碼往返與寬度界線；這些大量檢查不等於同等數量的獨立使用者情境。
- 固定字寬 golden vectors、PTT `C4 A1` 窄字探針、補充平面、零寬／C0／C1 與錯誤序列。
- 所有封包切點及逐位元組輸入、第一位元組 style、無效前綴後的重新處理、顯式 EOF flush 與連線／模型隔離。
- 待折行純查詢、寬字末欄／剩一格、關閉折行、左右半覆寫、半字選取／清除、零寬上限及 ZWJ 明確政策。
- 區內／區外 LF、捲動區保護、snapshot 深拷貝、resize 裁切／重設、非法尺寸／範圍、reset。
- 2,500 次固定種子的混合操作，每次檢查游標界線、前／後格配對與字元儲存不變條件；不是無上限 fuzz 或全狀態證明。

最小測試先在未實作模型上重現 `A中B` 案例失敗；之後零寬字在關閉折行末欄附著錯誤亦先由測試重現，再修正通過。
其餘測試透過新 Module 的公開 Interface 執行，沒有以另一份假的畫面演算法取代正式模型。

原有七組自動測試亦在兩組態重新建置並執行通過：解析器 11,759、Keep-Alive 2,318、同步 100、文章 810、OSC 1,343、SGR 66 197,753、終端邊界 256。
本輪未修改既有主程式接收／繪圖來源或資源，未重新建置主程式；SGR 66 測試讀取既有同組態主程式資源。
未重跑 PowerShell fixture、啟動 GUI、登入 PTT 或驗證實際 CPR；不沿用舊人工回報當成新模型已完成驗收。

```powershell
MSBuild Tests/TerminalCellModelTests.vcxproj /t:Rebuild /p:Configuration=Debug /p:Platform=Win32
./Tests/bin/Debug/TerminalCellModelTests.exe
MSBuild Tests/TerminalCellModelTests.vcxproj /t:Rebuild /p:Configuration=Release /p:Platform=Win32
./Tests/bin/Release/TerminalCellModelTests.exe
```

目前不需人工操作新畫面，因正式畫面尚未使用新模型；第二階段接入後另補 GUI／選取／複製等驗收。
下一步依 [模型方案](UTF8-Cell-Model-Plan.md) 接入接收與讀取使用端，處理 Big5 adapter、編碼切換及 scrollback，不直接跳到 CPR 或 Mouse。
