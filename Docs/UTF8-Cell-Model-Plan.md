# UTF-8 畫面格模型與 CPR 前置評估

日期：2026-10-18。PCMan 基準：`master` `8590116`。
狀態：第一階段 `5879222`、第二階段 B `ba0cc1f` 已提交；C 已補接收串流 decoder 與測試，仍未掛入正式 `OnText`／畫面或啟用 CPR。
實作、固定政策與測試結果見 [第一階段驗收](Terminal-Cell-Model-Validation.md)。下列來源比對與後續整合需求保留。
使用者要求本輪 CPR 必須包含 UTF-8，因此不採用「只回覆 Big5／UAO、UTF-8 全部忽略」的方案。

## 結論

CPR 不是只在空白的 `DeviceStatusReport` 填入格式字串即可。
目前 `cols_per_page` 同時代表畫面欄數與每列文字位元組容量，游標、顏色及選取索引也沿用位元組。
必須先區分「傳輸位元組」「Unicode 字元」「畫面格」，讓顯示、操作及 CPR 使用同一個畫面格座標。

例如從第 1 欄寫入 `A中B`：UTF-8 是 5 位元組，但依 PTT 的 ASCII／中文字寬是 4 格，下一位置應為第 5 欄，不是第 6 欄。
這是依現有逐位元組遞增游標的程式碼推導，並非此次新執行的 GUI 測試。

PTT 伺服器實際會以 `CR + C4 A1 + CSI 6n` 偵測編碼：UTF-8 的 `C4 A1` 是窄字 U+0121，回覆欄位 2；
Big5／UAO 的同一對位元組佔兩格，回覆欄位 3。錯誤欄寬可能影響站台編碼選擇，不能用錯誤座標先行回覆。
依據：[logind 查詢](https://github.com/ptt/pttbbs/blob/68b88d8753d69000033037a0c84a4ee3596b937a/daemon/logind/logind.c#L2596)、
[回覆處理](https://github.com/ptt/pttbbs/blob/68b88d8753d69000033037a0c84a4ee3596b937a/daemon/logind/logind.c#L2369)。
伺服器程式碼不等於已逐一驗證所有正式站台部署。

## 官方來源與可借鑑的設計

使用者提供的兩個倉庫分工不同：`ptt/pttbbs` 是伺服器，`ptt/ptt-term` 是網頁終端。
本次固定查核 `pttbbs` 的 `main`：`68b88d8753d69000033037a0c84a4ee3596b937a`，
以及 `ptt-term` 的 `dev`：`90f87d5fe3ec56dbd748962ca22cfb8919c575ee`。
`ptt-term` 的 [README](https://github.com/ptt/ptt-term/blob/90f87d5fe3ec56dbd748962ca22cfb8919c575ee/README.md)
確認其為 `term.ptt.cc`／`term.ptt2.cc` 的官方上游；沒有據此推論正式部署一定等於該 `dev` 提交。

| 來源 | 查核結果 | PCMan 可借鑑／注意事項 |
| --- | --- | --- |
| [網頁版 ANSI 解析](https://github.com/ptt/ptt-term/blob/90f87d5fe3ec56dbd748962ca22cfb8919c575ee/src/js/ansi_parser.js#L101) | 接收端保留 UTF-8 未完成位元組，再解碼後交給文字緩衝 | 跨封包解碼狀態應保留在連線內；不能將各封包獨立解碼，也不能先把整個 ANSI 串流當普通文字轉碼 |
| [網頁版文字緩衝](https://github.com/ptt/ptt-term/blob/90f87d5fe3ec56dbd748962ca22cfb8919c575ee/src/js/term_buf.js#L530) | `TermChar` 保存文字、屬性、網址與寬字前／後格標記 | 字元資料與傳輸位元組數分離；每格顏色可保留一字雙色，後格不應被複製成第二個字 |
| [網頁版字寬](https://github.com/ptt/ptt-term/blob/90f87d5fe3ec56dbd748962ca22cfb8919c575ee/src/js/wcwidth.js) | 有 0／1／2 格分類、BMP 查表、補充平面分支與 UAO ambiguous 修正 | 不能把所有非 ASCII 當兩格，也不能以 Windows 字型實際像素寬決定協定欄位 |
| [網頁版 CPR](https://github.com/ptt/ptt-term/blob/90f87d5fe3ec56dbd748962ca22cfb8919c575ee/src/js/ansi_parser.js#L406) | 用緩衝區游標加 1 產生回覆 | 可借鑑由模型提供座標，但不能直接複製其參數接受範圍與右邊界處理，PCMan 要另設測試 |
| [網頁版選取匯出](https://github.com/ptt/ptt-term/blob/90f87d5fe3ec56dbd748962ca22cfb8919c575ee/src/js/term_buf.js#L1425) | 從畫面格重建文字／ANSI，處理寬字後格及 SGR 66 | 不再以選取欄數直接當字串位元組長度 |
| [伺服器文字欄寬](https://github.com/ptt/pttbbs/blob/68b88d8753d69000033037a0c84a4ee3596b937a/common/sys/string.c#L707) | `mb_width` 區分字元位元組長度及格數，使用 CJK 字寬政策 | CPR 驗收以固定版本的 PTT 格數規則為對照；細節與限制見 [CPR 來源研究](CPR-Research.md) |

網頁版也有需要獨立驗證之處：其 `wcwidth` 能回傳零寬，但查核版本的
[`puts`](https://github.com/ptt/ptt-term/blob/90f87d5fe3ec56dbd748962ca22cfb8919c575ee/src/js/term_buf.js#L457)
逐 UTF-16 code unit 取 `str[i]`，並以 `isFullWidth` 決定一格／兩格；不是直接依 0／1／2 格結果寫入。
因此零寬字與代理對仍需由接收至匯出全流程驗證；不能把
[字寬函式測試](https://github.com/ptt/ptt-term/blob/90f87d5fe3ec56dbd748962ca22cfb8919c575ee/test/wcwidth.test.mjs)
等同於完整終端機、組合字或 emoji 序列已正確。
以上是原始碼靜態查核，不是此次對網頁版執行測試或登入驗收的結果。

## PCMan 影響範圍

| 使用端 | 現有耦合 | 調整要求 |
| --- | --- | --- |
| `TelnetConn.cpp::OnText`／定位／折行 | 每個可列印位元組增加 `cursor_pos.x` | 先完成解碼，再依畫面格寫入；跨封包、無效輸入、右邊界與寬字覆寫共用規則 |
| `TelnetConn.h::GetLineAttr/GetLineBufLen`／resize | 文字與顏色都按 `cols` 固定容量配置 | 欄數只代表格數；文字儲存量與列尾 metadata 不再套用位元組＝格數的位移 |
| `TermView.cpp::DrawLine/DrawLineOld/DrawLineBlink/ExtTextOut` | `get_chw`、位元組步長與半字裁切共同決定位置 | GDI adapter 由格模型取得字元與格範圍；一般／舊式／閃爍繪圖須一致，UTF-16 僅作繪圖序列化 |
| 選字、雙擊選字、查找、一般／帶色複製 | 以 `screen[row] + x` 直接截取字串 | 滑鼠座標是格；透過同一個文字擷取 interface 取得完整字元，不能截出半個 UTF-8 字元或代理對 |
| OSC 8／自動網址辨識／下劃線 | 網址範圍與 byte offset 共用 | 明確區分文字搜尋偏移及畫面格範圍；連結命中、覆寫、捲動與快照一起更新 |
| DEC2026／文章下載 | 同步快照複製整列原始記憶體；進度解析使用列寬作 byte 上限 | 快照包含完整格資料；文章解析取得序列化文字的實際長度，但仍只解析有效底列 |
| SGR 66／ANSI 編輯器 | 既有 Big5 每半格顏色、原始資料與匯出格式 | 保留既有 Big5／UAO 成果；UTF-8 欄寬必要調整不等於本輪必須新增 UTF-8 半字雙色 |
| 編碼選單 | `CConn::view` 共用 `CTermView`，`cp_id` 位於 view；切換目前只重畫 | 新解碼狀態須隔離，且明訂切換時的未完成字元及歷史資料處理；不默默把現有選單改成每站台設定 |
| CSI／控制字串解析 | OSC 已有吸收狀態；DCS 等字串尚非既有支援範圍 | 啟用會送出資料的 CPR 前，須確認未支援字串中的 `CSI 6n` 不會被誤執行；必要時先補完整吸收，而非宣稱已支援 DCS 協定 |

這是終端資料模型調整，不是 C/C++ 原始碼檔案編碼轉換，也不適合順帶全面改寫 MFC／資源／設定格式。

## 建議的 Module／Interface

建議新增純 C++ 的畫面格 Module，將解碼、格配置、寬字完整性與待折行狀態集中管理。
在既有 ANSI 分派器與文字儲存／讀取的 Seam 接入；網路 Telnet／WSS、Windows GDI 與剪貼簿是 Adapter，
不由格模型直接開 socket、操作視窗或改寫個人設定。

Interface 的核心不變條件：

- 游標、選取、捲動範圍及 OSC 命中一律以格表示；傳輸位元組長度使用不同欄位，不共用 `x`。
- 一個寬字只有一份字元內容與一個後格標記，但左右兩格可保留各自屬性。
- 只有一個權威游標與畫面；不另維護「供 CPR 專用」的 UTF-8 影子游標。
- 輸入可逐位元組到達；UTF-8 未完成序列、Big5 前導位元組及 SGR 66 暫存狀態可跨封包，但不可跨連線。
- 格內字元使用完整 Unicode scalar／文字儲存；不能把一個 `wchar_t` 當成所有 Unicode 字元的完整表示。
- 待折行是獨立狀態；查詢座標不取消折行、不改游標、不消耗彩色屬性。
- 提供格範圍的讀取／文字匯出與完整快照；使用端不再自行計算字元位元組位移。
- Big5／UAO 使用既有字碼表並保留雙半格行為；UTF-8 使用明訂、可測的 PTT 字寬政策。
  兩種編碼皆可透過相同讀取 Interface 使用，不需要讓每個畫面使用端自行判斷編碼。

字寬政策以固定 PTT 來源為對照，與網頁版做差異測試；遇到差異要記錄取捨，而不是混用兩套表。
不把一般 grapheme 合併規則直接等同 PTT 協定格數；組合標記、變體選擇符、ZWJ、emoji 及補充平面必須列入案例。
無效序列的替代字元、零寬字附著、沒有前字時的處理、寬字剩一格及覆寫後半格，都須在實作前訂出一致規則。

## 分段實作與驗收建議

1. 先建立可獨立驗證的串流解碼／字寬／格模型，暫不啟用 CPR，不變更正式 BBS 的現有行為。
2. 接入 UTF-8 接收、畫面、選字／複製、連結、捲動／resize、同步與文章匯出；同時保護 Big5／UAO、ANSI 編輯器、SGR 66 既有行為。
   編碼選單切換的語意須明確確認，不以自動偵測功能取代使用者設定。
3. 完成兩種編碼的模型整合及控制字串安全吸收後，才啟用普通 `CSI 6n` 的 CPR。只接受完整單一請求；不把 `?6n`、多參數或其他 DSR 誤當 CPR。
   回覆從目前 live 畫面的座標取得，不從 scrollback 捲軸位置、同步快照或字型像素量測反推。

上述是同一項含 UTF-8 的 CPR 工作分段交付，不代表第一階段先發布 Big5-only CPR。
SGR Mouse、DECOM、新設定頁及全面 Unicode 文字塑形不與本輪綁定；若必要取捨使範圍擴大，先確認再實作。

### 第二階段 A：編碼切換決策與原始列保存（2026-10-18）

使用者確認保留「全域切換編碼，並重新解讀既有畫面及歷史內容」。不改成每連線設定，也不只套用後續接收。
因此不能只保存 Unicode 再轉碼回舊編碼；未定義字元、錯誤序列及每個位元組的屬性會不可逆地遺失。

新增 `EncodedTerminalRow.h` 保存原始位元組與各位元組的屬性／opaque link ID；讀取時產生有來源範圍的格投影。
`WindowsTerminalRowDecoder.h` adapter 重用既有 `CUcs2Conv`／B2U，不另建 UAO 字碼表；
UTF-8 與 legacy 讀取皆不改來源、不擁有或更新游標。解碼器新增原始長度回報，避免將無效前綴與重新處理的下一字合併。

投影只處理一個已儲存列，不是網路串流處理、完整 ANSI 歷史重播或 resize。
暫時因錯誤編碼或較窄投影而不可見的來源仍保留，不能把讀取裁切當成刪除；後續 B 已補資料層更新政策，但正式呼叫端仍待接入。
完整 Interface、測試與限制見 [原始列保存驗證](Encoded-Row-Validation.md)。

### 第二階段 B：列更新與儲存生命週期（2026-10-18）

`EncodedRow` 新增回傳新來源的格範圍替換／清除、範圍複製及明確右側裁切；集中修復寬字、保存原始 style，失敗不改舊列。
新增真正文字／編輯分界，避免重組來源後把本來分開的 UTF-8 或 Big5 byte 誤接成另一字；不是封包分界。
`EncodedRowStorage` 提供稀疏的身分對應、複製、搬移、回收及來源快照，含來源額度拒絕保護，不預建全部歷史格向量。
兩者都沒有游標、全域 codepage、URL 或網路副作用。

第八／九組 Debug／Release／ASAN 各通過 2,551,208／131,344 項；完整政策及資料層／正式接點的區別見 [生命週期驗證](Encoded-Row-Lifecycle-Validation.md)。
`a4ab8af` 尚缺前輪兩標頭，本輪須連同新增儲存標頭納入版控；目前驗證仍使用完整工作目錄。

### 第二階段 C：接收串流解碼與失敗政策（2026-10-18）

`ba0cc1f` 已補齊 B 及前輪漏檔。新增 `EncodedTextDecoder`，在 ANSI／Telnet framing 後保留跨封包 UTF-8／legacy 前綴，輸出 scalar、格寬、原始 bytes／style 與來源 codepage，不另持有游標。
編碼切換先回傳舊前綴；資料層亦補零寬原始來源的安全附加。
第八／九／十組 Debug／Release／ASAN 各通過 2,551,215／131,351／325,461 項；正式 UAO／Shift-JIS adapter 另有實際轉碼測試。
接收 glyph 寫入來源列時區分連續 run 與編輯 seam，不以每個 Unicode scalar 當分界破壞換編碼後的原 byte 配對；零寬來源另附加至有效主格。
這仍是接收層前置，尚未呼叫正式 `OnText` 或切換 live 路徑；不能據此宣稱 UTF-8 游標、繪圖、全域切換已完成。

使用者確認額度不足時停止該連線、保留最後完整畫面並顯示原因；這類錯誤不得觸發自動重連／自動關閉分頁。
同步期間需保留最後完整快照與 OSC 所有權，不能由逾時或關閉事件釋出半成品；未同步時只能保證最後成功提交的模型，不能推斷整頁邊界。
正式策略仍待掛接與故障注入測試，詳見 [接收層驗證](Encoded-Text-Decoder-Validation.md)。

正式接入尚待：

1. 將 B 的列配置／回收、覆寫／清除、搬移／捲動及裁切掛入正式緩衝區；先選定總記憶體額度及更新拒絕處理，不能不理回傳值而讓模型／畫面／游標不同步。
   同步快照同時保存文字來源與 OSC 目標生命週期，不預先為全部 32,767 個歷史列建立完整格向量。
2. `OnText` 的未完成 UTF-8 與 Big5 前導位元組須保存於各連線；列投影的 EOF 不能用於封包結尾。
   完成字元後以格更新唯一游標，不另加供 CPR 使用的 byte／Unicode 影子游標。
3. 全域編碼切換通知所有分頁，結束舊編碼的未完成序列、更新投影及相關快取；保留原始歷史來源，不清空分頁、不中途轉碼覆蓋。
   未完成序列與游標如何銜接須有實際接收測試，不能由唯讀列測試推論已完成。
4. 接入一般／舊式／閃爍繪圖、選字／查找／一般與帶色複製、OSC／自動連結、同步及文章匯出，逐一移除 byte offset＝格座標假設。
5. 完成以上回歸及人工畫面驗收後，再進行第三階段普通 CPR 與控制字串安全吸收。

完整功能驗收規劃（第一階段已涵蓋模型案例，實際接收／顯示／CPR 仍待第二、三階段）：

- `C4 A1`：每個封包切點下，UTF-8 回第 2 欄、Big5／UAO 回第 3 欄；查詢不得受背景分頁或編碼選單切換干擾。
- ASCII／`A中B`／窄拉丁字／UAO ambiguous／組合標記／補充平面：畫面、下一游標與 CPR 格數一致。
- 最右欄、寬字剩一格、關閉折行、待折行後連續查詢：查詢不改變下一字的落點。
- `CUP`、相對移動、區內／區外 LF、區域重設、resize、重連、同步中查詢與兩連線隔離。
- 不完整／無效 UTF-8、ANSI／OSC 夾在字元附近、取消／溢位／未知或 private DSR：不誤回覆，不顯示控制 payload。
- 一般／帶色複製、選半個寬字、OSC 隱藏網址、文章底列、ANSI 存讀及編輯器：內容完整、顏色與連結位置一致。
- 正式接收至原始回覆位元組、各畫面／匯出 Adapter、loopback 人工畫面，分開記錄；字寬函式通過不等於 GUI 通過。

初評時僅查核來源與設計；使用者同意後已另完成第一階段及重跑既有七組測試，實際範圍依 [本輪驗證紀錄](Terminal-Cell-Model-Validation.md) 記錄，不將模型通過視為正式 BBS 功能已完成。
