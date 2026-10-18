#include <afx.h>
#include <commctrl.h>
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <algorithm>
#include "../Lite/AnsiSequenceParser.h"
#include "../Lite/Sgr66.h"
#include "../Lite/SynchronizedOutput.h"
#include "../Lite/OscHyperlinks.h"
#include "../Lite/ArticleProgress.h"

static unsigned int checks = 0;
static const int IAC = 255;
static void Check(bool ok, const char* why)
{
    ++checks;
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", why); std::exit(1); }
}
static BYTE GetAttrBkColor(BYTE value) { return (value >> 4) & 7; }
class CTelnetConn;
struct Parent
{
    HWND m_hWnd = nullptr;
    struct Tab { void SetItem(int, TCITEM*) {} } tab;
    int ConnToIndex(CTelnetConn*) { return 0; }
    void SwitchToConn(CTelnetConn*) {}
    void SetForegroundWindow() {}
    void BringWindowToTop() {}
};
struct CTermView
{
    CTelnetConn* telnet = nullptr;
    Parent frame;
    Parent* parent = &frame;
    bool doflash = false, auto_switch = false;
    UINT codePage = 950;
    UINT GetCodePage() { return codePage; }
    void HideCaret() {}
    void ShowCaret() {}
    void UpdateWindow() {}
    void SetScrollBar() {}
    void InvalidateRect(BOOL) {}
};
struct { int sound = 0; CString wavepath; } AppConfig;
struct CConn { void ClearAllFlags() {} };
class CTelnetConn : public CConn
{
public:
    enum { MAX_LINE_COUNT = 32767 };
    struct Settings
    {
        int cols_per_page = 8, lines_per_page = 4, line_count = 6;
        bool line_wrap = false;
    } site_settings;
    CTermView ui;
    CTermView* view = &ui;
    char** screen = nullptr;
    long first_line = 0, last_line = 0;
    int scr_top = 0, scr_bottom = 3, scroll_pos = 0;
    CPoint cursor_pos = CPoint(0, 0), old_cursor_pos = CPoint(0, 0);
    BYTE cur_attr = 7, attr_flags = 0, saved_attr = 7;
    Sgr66::PendingAttribute sgr66_attribute;
    int remote_line_wrap = -1;
    SynchronizedOutput sync_output;
    OscHyperlinks osc_links, osc_snapshot;
    AnsiSequenceParser ansi_parser;
    BYTE* buf = nullptr;
    BYTE* last_byte = nullptr;
    char ansi_param[64] = {};
    char* pansi_param = ansi_param;
    bool insert_mode = false, is_getting_article = false, is_ansi_editor = false, is_telnet = true;
    unsigned int sends = 0;
    unsigned int dsrRequests = 0;
    int Send(const void*, int) { ++sends; return 0; }
    CTelnetConn() { ui.telnet = this; CreateBuffer(); }
    ~CTelnetConn() { for (int i = 0; i < site_settings.line_count; ++i) delete[] screen[i]; delete[] screen; }
    void OnText();
    void ProcessAnsiEscapeSequence();
    void SetCurrentAttributes(USHORT);
    static void SetBgColor(BYTE& attr, BYTE value) { attr = (attr & 143) | (value << 4); }
    static void SetFgColor(BYTE& attr, BYTE value) { attr = (attr & 248) | value; }
    void GotoXY(int, int);
    void SetScrollRange(int, int);
    void LineFeed();
    void LineFeed(int n) { while (n-- > 0) LineFeed(); }
    void ReSizeBuffer(long, int, int);
    char* ResizeLine(int, int);
    void CreateBuffer();
    void ClearAllFlags();
    int GetLineBufLen();
    int GetLineBufLen(int);
    LPBYTE GetLineAttr(const char*);
    LPBYTE GetLineAttr(int);
    void InitNewLine(char*);
    char* AllocNewLine();
    ArticleProgress::Progress GetArticleProgress();
    void CheckSynchronizedOutputTimeout() {}
    void BeginSynchronizedOutput() { sync_output.Begin(0, screen + first_line, site_settings.lines_per_page, GetLineBufLen()); }
    void EndSynchronizedOutput() { sync_output.End(); }
    void CheckStrTrigger() {}
    void UpdateCursorPos() {}
    void CheckHyperLinks() {}
    bool GetUpdateLine(int) { return false; }
    void UpdateLine(int) {}
    void RemoveUpdateLine(int) {}
    void ContinueCopyArticle() {}
    void SetUpdateLine(long, BYTE) {}
    void SetUpdateWholeLine(long) {}
    void EditorCarriageRetiurn() { ++cursor_pos.y; }
    void Back() { if (cursor_pos.x > 0) --cursor_pos.x; }
    void ClearCurrentLine(int) {}
    void ClearScreen(int) {}
    void InsertLines(int) {}
    void GoUp(int n) { cursor_pos.y = (std::max)(first_line, cursor_pos.y - n); }
    void GoDown(int n) { cursor_pos.y = (std::min)(last_line, cursor_pos.y + n); }
    void GoRight(int);
    void GoLeft(int n) { cursor_pos.x = (std::max)(0L, cursor_pos.x - n); }
    void SaveCursorPos() { old_cursor_pos = cursor_pos; }
    void RestoreCursorPos() { cursor_pos = old_cursor_pos; }
    void InsertChar(int) {}
    void DeleteLines(int) {}
    void DeleteChar(int) {}
    void BackTab(int) {}
    void SetMode(int) {}
    void ResetMode(int) {}
    void DeviceStatusReport(int) { ++dsrRequests; } // Observe dispatch; production remains a no-op.
    void ScrollUp() {}
    void ScrollDown() {}
    void Feed(std::string input)
    {
        buf = reinterpret_cast<BYTE*>(&input[0]); last_byte = buf + input.size(); OnText();
    }
    std::string Row(int y) { return std::string(screen[first_line + y], site_settings.cols_per_page); }
};
#include "ProductionBoundaryMethods.inc"

static void CheckOpaqueStrings()
{
    const std::string st = "\x1b\\";
    for (char kind : std::string("PX^_"))
    {
        const std::string start = std::string("\x1b") + kind;
        const std::string input = start + "\x1b[6n\x1b[31m\x1b[?7l\x1b[1;2r\x1b[?2026h"
            "\x1b]8;;https://example.com\a\r\n\t\b\a" + st;
        for (std::size_t split = 0; split <= input.size(); ++split)
        {
            CTelnetConn t;
            t.Feed("\x1b[?7h12345678"); // Keep the existing delayed wrap untouched.
            t.Feed(input.substr(0, split)); t.Feed(input.substr(split));
            Check(t.Row(0) == "12345678" && t.Row(1) == "        " &&
                t.cursor_pos == CPoint(8, t.first_line), "opaque input cannot change screen or pending wrap");
            Check(t.dsrRequests == 0 && t.sends == 0 && t.cur_attr == 7 && t.remote_line_wrap == 1 &&
                t.scr_top == 0 && t.scr_bottom == 3 && !t.sync_output.Active() && !t.ui.doflash,
                "production OnText cannot execute opaque queries, modes, SGR or BEL");
            t.Feed("X");
            Check(t.Row(1) == "X       " && t.GetLineAttr(t.first_line + 1)[0] == 7 &&
                !t.osc_links.At(t.screen[t.first_line + 1], 0), "text after ST keeps wrap, color and link scope");
            t.Feed("\x1b[6n");
            Check(t.dsrRequests == 1 && t.sends == 0, "outside DSR reaches no-op handler; response remains disabled");
        }
        {
            CTelnetConn t;
            t.Feed("\x1b[?2026h");
            t.Feed(start + "\x1b[?2026l\x1b[6n" + st);
            Check(t.sync_output.Active() && t.dsrRequests == 0, "opaque end-sync cannot publish live frame");
            t.Feed("\x1b[?2026l");
            Check(!t.sync_output.Active(), "real end-sync after opaque string still releases frame");
        }
        for (const std::string suffix : { std::string(), std::string("\x1b") })
        {
            CTelnetConn t;
            t.Feed(start + "partial" + suffix);
            t.ClearAllFlags(); // Extracted production lifecycle reset, not a duplicate implementation.
            t.Feed("NEW\x1b[6n");
            Check(t.Row(0) == "NEW     " && t.dsrRequests == 1,
                "production reset removes unfinished opaque state across sessions");
        }
    }
}

static void CheckWrapModes()
{
    const std::string input = "\x1b[?7h12345678X";
    for (std::size_t split = 0; split <= input.size(); ++split)
    {
        CTelnetConn t;
        t.Feed(input.substr(0, split)); t.Feed(input.substr(split));
        Check(t.Row(0) == "12345678" && t.Row(1) == "X       ", "DECAWM ON wraps on next printable byte across every packet split");
        Check(!t.site_settings.line_wrap, "remote ON never changes saved local wrap setting");
    }
    {
        CTelnetConn t;
        t.Feed("\x1b[?7h\x1b[999CZ");
        Check(t.Row(0) == "       Z" && t.Row(1) == "        ", "cursor-right at margin must not trigger a printable wrap");
    }
    for (bool localWrap : { false, true })
    {
        CTelnetConn t;
        t.site_settings.line_wrap = localWrap;
        t.Feed("\x1b[?7l12345678XYZ");
        Check(t.Row(0) == "1234567Z" && t.Row(1) == "        ", "remote OFF replaces right-margin cell, never wraps");
        Check(t.site_settings.line_wrap == localWrap, "OFF leaves local preference unchanged");
        t.ClearAllFlags(); t.GotoXY(2, 1);
        t.Feed("abcdefghQ");
        Check(localWrap ? t.Row(2) == "Q       " : t.Row(1) == "abcdefgh", "reset restores original local preference");
        Check(t.remote_line_wrap == -1 && t.scr_top == 0 && t.scr_bottom == 3, "reset clears session modes and margins");
    }
    for (const std::string suffix : { std::string("\rX"), std::string("\nX"), std::string("\bX"),
        std::string("\tX"), std::string("\x1b[1;3HX"), std::string("\x1b[31mX") })
    {
        CTelnetConn t;
        t.Feed("\x1b[?7h12345678" + suffix);
        if (suffix == "\rX") Check(t.Row(0) == "X2345678", "CR cancels pending wrap");
        if (suffix == "\nX") Check(t.Row(1) == "       X" && t.Row(2) == "        ", "LF does not cause a second implicit LF");
        if (suffix == "\bX") Check(t.Row(0) == "123456X8", "BS addresses the visible last cell");
        if (suffix == "\tX") Check(t.Row(0) == "1234567X", "HT cancels pending wrap");
        if (suffix == "\x1b[1;3HX") Check(t.Row(0) == "12X45678", "CUP cancels pending wrap");
        if (suffix == "\x1b[31mX") Check(t.Row(1) == "X       " && t.GetLineAttr(t.first_line + 1)[0] == 1, "SGR retains pending wrap and next-character color");
    }
    {
        CTelnetConn t, other;
        t.Feed("\x1b[?7h"); other.Feed("12345678Z");
        Check(other.Row(0) == "12345678" && other.Row(1) == "        ", "untouched connections keep legacy default clipping");
        t.is_ansi_editor = true; t.Feed("\x1b[?7l12345678Z");
        Check(t.Row(0) == "12345678" && t.Row(1) == "        " && t.remote_line_wrap == 1, "remote DECAWM never changes editor line handling");
    }
    for (const std::string sequence : { std::string("\x1b]0;\x1b[?7h\a"), std::string("\x1b[?7\x18"),
        std::string("\x1b[?7;1000h"), std::string("\x1b[?7 h"), std::string("\x1b[?1000h") })
    {
        CTelnetConn t; t.Feed(sequence + "12345678Z");
        Check(t.remote_line_wrap == -1 && t.Row(0) == "12345678", "invalid, opaque and unsupported modes cannot enable wrap");
    }
    for (std::size_t split = 0; split <= 10; ++split)
    {
        CTelnetConn t;
        const std::string bytes = "123456\xa4\xa4" "X";
        t.Feed("\x1b[?7h"); t.Feed(bytes.substr(0, split < bytes.size() ? split : bytes.size()));
        t.Feed(bytes.substr(split < bytes.size() ? split : bytes.size()));
        Check(t.Row(0) == std::string("123456\xa4\xa4", 8) && t.Row(1) == "X       ", "complete Big5 pair at right edge survives packet splits");
    }
}

static void FillRows(CTelnetConn& t)
{
    for (int y = 0; y < t.site_settings.line_count; ++y)
    {
        std::memset(t.screen[y], 'A' + y, t.site_settings.cols_per_page);
        std::memset(t.GetLineAttr(y), y + 1, t.site_settings.cols_per_page);
    }
}

static void CheckScrolling()
{
    for (const std::string margins : { std::string("\x1b[2;3r"), std::string("\x1b[1;3r"), std::string("\x1b[r") })
    {
        for (std::size_t split = 0; split <= margins.size(); ++split)
        {
            CTelnetConn t; FillRows(t);
            t.GotoXY(3, 5); t.Feed(margins.substr(0, split)); t.Feed(margins.substr(split));
            Check(t.cursor_pos == CPoint(0, t.first_line), "every DECSTBM split homes cursor");
            const int bottom = margins == "\x1b[r" ? 4 : 3;
            t.GotoXY(bottom, 1); t.Feed("\n");
            Check(t.Row(bottom - 1) == "        ", "region bottom becomes a blank line");
            Check(t.cursor_pos == CPoint(0, t.first_line + bottom - 1), "scroll keeps cursor on bottom margin");
            Check(t.Row(bottom - 2) == std::string(8, static_cast<char>('C' + bottom - 1)), "preceding row carries bottom text upward");
            Check(t.GetLineAttr(t.first_line + bottom - 2)[0] == bottom + 2, "row colors follow pointer-based scroll");
            if (bottom == 3)
            {
                Check(t.Row(3) == "FFFFFFFF", "partial scroll leaves below-region row intact");
                Check(std::string(t.screen[0], 8) == "AAAAAAAA", "partial scroll never shifts scrollback");
                if (margins == "\x1b[2;3r") Check(t.Row(0) == "CCCCCCCC", "partial scroll leaves above-region row intact");
            }
            else Check(std::string(t.screen[0], 8) == "BBBBBBBB", "full-screen scroll retains legacy scrollback behavior");
        }
    }
    {
        CTelnetConn t; FillRows(t); t.Feed("\x1b[2;3r");
        t.GotoXY(4, 4); const std::string before = t.Row(1) + t.Row(2);
        t.Feed("\nX");
        Check(t.Row(1) + t.Row(2) == before && t.Row(3) == "FFFXFFFF", "LF below region cannot scroll region or advance below screen");
        t.GotoXY(1, 2); t.Feed("\n");
        Check(t.cursor_pos == CPoint(1, t.first_line + 1), "LF above region moves cursor without scrolling");
    }
    for (const char* command : { "\x1b[r", "\x1b[0;0r", "\x1b[;r", "\x1b[1;150r" })
    {
        CTelnetConn t; t.Feed("\x1b[2;3r\x1b[3;4H"); t.Feed(command);
        Check(t.scr_top == 0 && t.scr_bottom == 3 && t.cursor_pos == CPoint(0, t.first_line), "default, zero, and oversized margins safely cover local screen");
    }
    for (const char* command : { "\x1b[3;3r", "\x1b[4;2r", "\x1b[150;150r", "\x1b[2;3;4r", "\x1b[32768;4r" })
    {
        CTelnetConn t; t.Feed("\x1b[2;3r\x1b[3;4H"); t.Feed(command);
        Check(t.scr_top == 1 && t.scr_bottom == 2 && t.cursor_pos == CPoint(3, t.first_line + 2), "invalid margins leave cursor and old region unchanged");
    }
    {
        CTelnetConn t; FillRows(t);
        t.Feed("\x1b[2;3r\x1b[3;1H\x1b[?7h12345678X");
        Check(t.Row(1) == "12345678" && t.Row(2) == "X       " && t.Row(3) == "FFFFFFFF", "DECAWM wrapping respects active DECSTBM region");
    }
}

static void CheckResize()
{
    CTelnetConn t; FillRows(t);
    t.Feed("\x1b[?7h\x1b[3;4r\x1b[4;8H");
    // The four trailing attribute metadata bytes must be read within the row.
    std::memset(t.GetLineAttr(2) + 8, 0x5a, 4);
    t.ReSizeBuffer(6, 6, 3);
    Check(t.scr_top == 0 && t.scr_bottom == 2, "shrink resets both margins to valid screen");
    Check(t.cursor_pos.x <= 5 && t.cursor_pos.y >= t.first_line && t.cursor_pos.y <= t.last_line, "resize bounds cursor to visible screen");
    Check(t.remote_line_wrap == 1, "resize preserves per-session DECAWM");
    Check(t.GetLineAttr(2)[6] == 0x5a && t.GetLineAttr(2)[9] == 0x5a, "resize preserves row metadata from correct in-bounds offset");
    Check(t.Row(0) == "DDDDDD", "resize retains row text");
    t.ReSizeBuffer(6, 10, 4);
    Check(t.scr_top == 0 && t.scr_bottom == 3 && t.Row(1) == "DDDDDD    ", "grow restores full region and pads new columns");
}

static void CheckIntegration()
{
    for (int width : { 40, 80, 120, 160 })
        for (int height : { 24, 32, 64 })
        {
            CTelnetConn t;
            t.ReSizeBuffer(height + 2, width, height);
            const std::string row = std::string(width - 2, 'A') + "\xa4\xa4";
            for (char byte : std::string("\x1b[?7h") + row + "X") t.Feed(std::string(1, byte));
            Check(t.Row(0) == row && t.Row(1)[0] == 'X', "supported terminal dimensions keep full Big5 edge and wrap once");
            t.Feed("\x1b[1;150r");
            Check(t.scr_top == 0 && t.scr_bottom == height - 1 && t.cursor_pos == CPoint(0, t.first_line), "PTT 150-row limit clamps to supported local dimensions");
            Check(t.sends == 0, "wrap and margin commands send no response");
        }
    {
        CTelnetConn t;
        t.Feed("\x1b[2;3r\x1b[3;1H\x1b]8;;https://example.com/target\x1b\\LINK\x1b]8;;\x1b\\\r\n");
        const auto link = t.osc_links.At(t.screen[t.first_line + 1], 0);
        Check(t.Row(1) == "LINK    " && link && *link == "https://example.com/target", "OSC hyperlink follows its row through regional scrolling");
        Check(!t.osc_links.At(t.screen[t.first_line + 2], 0), "recycled row cannot retain old hyperlink");
    }
    {
        CTelnetConn t;
        t.Feed("\x1b[?2026h\x1b[?7h12345678X\x1b[2;3r");
        Check(t.sync_output.Active() && t.sync_output.Row(0)[0] == ' ' && t.Row(1)[0] == 'X', "sync snapshot remains frozen while new modes update live rows");
        t.Feed("\x1b[?2026l");
        Check(!t.sync_output.Active() && t.sync_output.CopyReady(), "mode commands do not lose synchronized-output end");
    }
    {
        CTelnetConn t;
        t.ReSizeBuffer(26, 80, 24);
        const std::string footer = "(100%) 1~3 \xa6\xe6";
        t.GotoXY(24, 1); t.Feed(footer);
        Check(t.GetArticleProgress().state == ArticleProgress::Complete, "normal article footer still recognized");
        t.Feed("\x1b[2;22r\x1b[22;1H\n");
        Check(t.GetArticleProgress().state == ArticleProgress::Complete, "regional scroll cannot corrupt protected article footer");
        t.Feed("\x1b[?2026h");
        Check(t.GetArticleProgress().state == ArticleProgress::Unavailable, "new modes retain synchronized article-copy barrier");
    }
    {
        CTelnetConn t;
        t.ui.codePage = CP_UTF8;
        t.ReSizeBuffer(26, 80, 24);
        // Fits in the row: this verifies byte preservation, not Unicode cell width.
        const std::string title = u8"   12   author   □ 動態標題…";
        t.Feed("\x1b[?7l" + title);
        Check(t.Row(0).substr(0, title.size()) == title, "UTF8 VCOL-like title and ellipsis bytes remain intact");
    }
}

int main()
{
    CTelnetConn t;
    t.Feed("\x1b[3;5H\x1b[2;3r");
    Check(t.scr_top == 1 && t.scr_bottom == 2, "DECSTBM sets margins");
    Check(t.cursor_pos == CPoint(0, t.first_line), "DECSTBM homes cursor without a server-supplied CUP");
    CheckWrapModes();
    CheckOpaqueStrings();
    CheckScrolling();
    CheckResize();
    CheckIntegration();
    std::printf("PASS: %u terminal boundary checks\n", checks);
}
