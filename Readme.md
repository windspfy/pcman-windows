PCMan
---
這是 PCMan BBS 用戶端的程式碼。PCMan Combo 已停止維護並自主要開發分支移除；
最後版本封存於 Git tag `combo-final`。

除 Lite 目錄下的 Rijndael.cpp 和 Rijndael.h 為 George Anescu 撰寫，
不是使用 GPL 授權以外，其餘程式碼皆是使用 GPL 授權。

因為授權問題，本專案無法包含圖形檔，各種圖示暫時以單色的 bmp 取代，
請下載舊版 PCMan 的 zip 檔，從中取出各 bmp 圖檔，並放到正確位置。

首次釋出時間 2007.01.01

### PTT 與 PTT2 黑名單及關鍵字過濾（規劃中）

此功能預計用於看板文章列表與文章閱讀畫面。兩站的所有看板共用設定，每條規則可選擇套用到文章、推文或兩者。

* **ID 黑名單**比對完整帳號。作者命中時，列表遮蔽作者和標題，閱讀時也遮蔽該作者的正文。推文者命中時，遮蔽其帳號與發言。
* **關鍵字**預設只要文字「包含」就命中，也可選「完全相同」。標題命中就處理整個標題，推文命中就處理該則發言，正文命中就處理命中的畫面列。兩種規則都忽略英文字母大小寫。標點和字詞間的空格會參與比對，畫面對齊用的填補空白會略過。
* **正文跨列比對**限於可確認的自動折行。例如「暴」在上一列末尾、「雷」在下一列開頭，若兩列屬於同一段自動折行的文字，「包含『暴雷』」就會命中，兩列都套用遮蔽。遇到手動換行，或無法確認折行原因時，會分列比對，此時跨列的「暴雷」就不會命中。
* **遮蔽方式**可跟隨共用預設（初始為星號），也可逐條指定。隱藏會讓文字與當前底色相同；星號會保留原本欄寬；標色共用一組配色，文字和底色各可選擇保留原色或使用終端機色盤。同一欄位命中多條規則時，依隱藏、星號、標色的順序優先套用。

遮蔽只改變畫面顯示，文字位置維持原樣。複製時會取得完整原文，包括畫面上已隱藏或換成星號的內容。

Getting started
---
   * [PCMan程式專案建置和執行：逐步解說](../../wiki/Building_PCMan)

使用 Visual Studio 2026 建置
---

請安裝「使用 C++ 的桌面開發」工作負載，並包含下列元件：

* MSVC v143 x86/x64 建置工具
* 適用於 x86 的 MFC
* Windows 11 SDK 
* vcpkg

開啟 `PCMan.sln` 後可直接建置 `Debug|Win32` 或 `Release|Win32`。專案已啟用
vcpkg manifest，首次建置會自動下載及編譯相依套件。

也可以在 Developer PowerShell for Visual Studio 中執行：

```powershell
msbuild PCMan.sln /m /t:Rebuild /p:Configuration=Release /p:Platform=Win32
```

WebSocket 連線使用 Windows 內建的 WinHTTP，不需要額外部署 WebSocket、OpenSSL
或其他 TLS 程式庫；WSS 連線需要 Windows 8 以上。快捷鍵 JSON 讀寫使用
`nlohmann/json`，由 vcpkg manifest 自動還原。
