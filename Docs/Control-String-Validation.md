# CPR 前置：未支援控制字串安全吸收

日期：2026-10-18。基準提交：`6458d7a`；本文件記錄其後的工作目錄修改。

## 本輪完成範圍

正式共用 `AnsiSequenceParser` 新增 DCS／SOS／PM／APC 的 7-bit 字串吸收。
既有 `CTelnetConn::OnText` 已使用此 parser，因此這是正式接收路徑的行為調整，不是另一個未接入的模型。
依 codebase-design 的接點設計，規則集中在原有 Module，維持相同 `Feed` Interface；呼叫端不自行重做字串掃描。

這些字串的內容**不實作、不儲存、不回傳、不顯示，也不當成 CSI／OSC 執行**。
例如 `ESC P ... ESC [ 6 n ... ESC \` 中的查詢不會抵達 `DeviceStatusReport`。
原先未吸收時，`ESC P` 被忽略後即返回文字狀態，內容仍可能繼續進入一般文字／CSI 路徑。

控制碼名稱與 7-bit 對應依據 [xterm 官方控制序列文件](https://invisible-island.net/xterm/ctlseqs/ctlseqs.html#h2-C1-8-Bit-Control-Characters)。
以下錯誤恢復、只採 7-bit 與持續丟棄政策是 PCMan 的明確選擇，不宣稱完整 ECMA-48／xterm 相容。

| 字串 | 開始 | 正常結束 | BEL |
| --- | --- | --- | --- |
| DCS | `ESC P` | `ESC \`（ST） | 丟棄，不結束 |
| SOS | `ESC X` | ST | 丟棄，不結束 |
| PM | `ESC ^` | ST | 丟棄，不結束 |
| APC | `ESC _` | ST | 丟棄，不結束 |
| OSC（既有） | `ESC ]` | ST 或 BEL | 結束 OSC，不響鈴 |

共同規則：

- `CAN`／`SUB` 取消目前字串；取消後的 bytes 恢復一般解析，並非繼續隱藏。
- 狀態跨接收區塊保留，ST 的兩個 bytes 可分開到達；連線的既有 reset 清除未完成狀態。
- 不以封包結尾、BEL、換行或內嵌另一段 `ESC [`／`ESC ]` 提早釋出 DCS／SOS／PM／APC。
- 若 ESC 後不是 `\`，繼續丟棄；連續 ESC 後的 `\` 仍可結束。
- 四種新增字串只保存狀態，不配置 payload buffer；超長輸入不會越過某個長度後漏出尾端。
- 未終止的字串會持續隱藏後續輸入，直到 ST、取消或連線 reset；沒有猜測性的逾時恢復。
- `0x90`／`0x98`／`0x9E`／`0x9F` 不是開始碼，`0x9C` 不是 ST，保留既有 Big5／UAO／UTF-8 的高位元組政策。
- 只辨識 ESC 直接跟四種開始碼；帶 intermediate 的其他 ESC 指令不誤判為字串。
- Telnet IAC 仍由原協定層處理；本輪不重新設計 Telnet framing。
- 自訂觸發器仍按原始接收資料比對，沒有改成過濾後文字。上述隔離指 ANSI 解析／終端畫面指令，不保證使用者自行設定的 raw-data 觸發器不會命中 payload。

## 自動驗證

Debug／Release 主程式均完整重建成功，使用既有 VS／SDK 與本機 vcpkg 套件，未重新安裝依賴。
這是建置成功，不宣稱原有警告已全部消除或全新 Windows 安裝驗證。
十組測試在兩組態各完整重建並執行通過：

| 測試 | 每組態檢查數 |
| --- | ---: |
| AnsiSequenceParserTests | 17,765 |
| KeepAliveTests | 2,318 |
| SynchronizedOutputTests | 100 |
| ArticleProgressTests | 810 |
| OscHyperlinkTests | 1,343 |
| Sgr66Tests | 197,757 |
| TerminalBoundaryTests | 1,280 |
| TerminalCellModelTests | 2,551,215 |
| WindowsTerminalRowTests | 131,351 |
| EncodedTextDecoderTests | 326,841 |

另於獨立輸出目錄以 Release＋AddressSanitizer 重建並執行 parser／boundary／text decoder，
分別通過 17,765／1,280／326,841 項，未報 ASAN 錯誤；不是主程式 GUI 的 ASAN 驗收。
大量計數包含切點及既有列舉輸入，不等於相同數量的獨立人工使用情境。

新增案例：

- 四種字串的所有兩段切點及逐 byte 輸入，內嵌 CPR／SGR／清除／折行／DEC2026／OSC 不執行。
- C0、NUL、DEL、高位元組、BEL、連續 ESC、取消、未完成、每種超過 1 MiB、連線隔離與 reset。
- `TerminalBoundaryTests` 擷取正式 `OnText`／分派及 reset 方法，確認畫面、游標／待折行、顏色、捲動範圍、OSC 目標及同步狀態不被 payload 改動。
  其 DSR 替身只記錄是否抵達，不送回資料；字串外的查詢能抵達，字串內不能抵達。
- `Sgr66Tests` 同樣使用擷取的正式 `OnText`，確認 Big5 兩個 bytes 之間插入 opaque 字串不會混入 payload 或改色。
- `EncodedTextDecoderTests` 的 parser＋decoder 離線 fixture 驗證 UTF-8／Big5 前綴及一次性屬性不被 payload 消耗；
  此 fixture 仍不是正式 UTF-8 格模型／GDI／剪貼簿整合驗收。

重跑命令見 [測試總覽](../Tests/README.md)，沿用原十組 project，無新增測試專案。
本輪未執行新的 GUI、站台人工測試或 PowerShell fixture 回歸，亦未代為提交／推送。
個人 Config／Toolbar 與無關產物保留不動。

## 未完成主線

`EncodedTextDecoder`／來源列仍未掛入正式接收、繪圖、選取、匯出及全域編碼切換；
超額停止連線並保留完整畫面的正式處理亦未完成。
**CPR 仍不回覆，第二階段整體未完成。** 本輪是先完成後續啟用 CPR 的必要解析安全前置，
不新增供 CPR 專用的影子游標、不提前發布 Big5-only CPR。
下一步回到上述正式格模型整合；完整範圍見 [整合計畫](UTF8-Cell-Model-Plan.md)。
