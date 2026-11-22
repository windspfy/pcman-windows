#include <afxwin.h>
#include <commctrl.h>
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <algorithm>
#include "../Lite/EncodedTextDecoder.h"
#include "../Lite/AnsiSequenceParser.h"
#include "../Lite/Sgr66.h"
#include "../Lite/SynchronizedOutput.h"
#include "../Lite/OscHyperlinks.h"
#include "../Lite/EncodedRowStorage.h"
#include "../Lite/WindowsTerminalRowDecoder.h"
static CUcs2Conv g_ucs2conv;
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
    void UpdateStatus() {}
    HWND m_hWnd = nullptr;
    struct Tab { void SetItem(int, TCITEM*) {} } tab;
    int ConnToIndex(CTelnetConn*) { return 0; }
    void SwitchToConn(CTelnetConn*) {}
    void SetForegroundWindow() {}
    void BringWindowToTop() {}
};
struct CTermView
{
    void Invalidate(BOOL) {}
    CTelnetConn* telnet = nullptr;
    Parent frame;
    Parent* parent = &frame;
    bool doflash = false, auto_switch = false;
    UINT codePage = 950;
    UINT GetCodePage() { return codePage; }
    int left_margin = 0, chw = 1, lineh = 2;
    bool blight = true;
    UINT SetDCColors(CDC*, BYTE, BOOL) { return ETO_OPAQUE; }
    void FillBkRect(CDC&, CRect&, BYTE, BOOL) {}
    void DrawUtf8Line(CDC&, LPCSTR, BYTE*, BYTE*, int);
    void HideCaret() {}
    void ShowCaret() {}
    void UpdateWindow() {}
    void SetScrollBar() {}
    void OnVScroll(UINT, UINT, CScrollBar*) {}
    void InvalidateRect(BOOL) {}
};
struct { int sound = 0; CString wavepath; bool old_textout = false; } AppConfig;
struct CConn { void ClearAllFlags() {} };
class CTelnetConn : public CConn
{
public:
    enum { MAX_LINE_COUNT = 32767 };
    struct Settings
    {
        int cols_per_page = 8, lines_per_page = 4, line_count = 6;
        bool line_wrap = false, localecho = false;
    } site_settings;
    CTermView ui;
    CTermView* view = &ui;
    char** screen = nullptr;
    long first_line = 0, last_line = 0;
    int scr_top = 0, scr_bottom = 3, scroll_pos = 0;
    CPoint cursor_pos = CPoint(0, 0), old_cursor_pos = CPoint(0, 0);
    CPoint sel_start{}, sel_end{};
    BYTE cur_attr = 7, attr_flags = 0, saved_attr = 7;
    Sgr66::PendingAttribute sgr66_attribute;
    TerminalCells::EncodedTextDecoder utf8_decoder;
    OscHyperlinks::Target utf8_prefix_link;
    void WriteUtf8Glyph(const TerminalCells::EncodedTextDecoder::Glyph&, const OscHyperlinks::Target&,
        const TerminalCells::EncodedTextDecoder::Glyph* second = nullptr);
    void FinishReceivedText();
    bool source_limit_stopped = false, is_connected = true;
    int closes = 0;
    void Close() { ++closes; }
    void StopForSourceLimit();
    TerminalCells::EncodedRow ReadUtf8Source(const char* row);
    void MirrorUtf8Row(char* row);
    bool HasUtf8Blink(const char* row);
    BYTE pending_big5 = 0, pending_big5_attr = 7;
    OscHyperlinks::Target pending_big5_link;
    void FinishBig5Text();
    void WriteReceivedGlyph(BYTE, BYTE, const OscHyperlinks::Target&, bool);
    int remote_line_wrap = -1;
    SynchronizedOutput sync_output;
    OscHyperlinks osc_links, osc_snapshot;
    TerminalCells::EncodedRowStorage encoded_rows{MAX_LINE_COUNT, MAX_LINE_COUNT * 160u};
    AnsiSequenceParser local_parser, ansi_parser;
    BYTE* buf = nullptr;
    BYTE* last_byte = nullptr;
    char ansi_param[64] = {};
    char* pansi_param = ansi_param;
    bool insert_mode = false, is_getting_article = false, is_ansi_editor = false, is_telnet = true;
    unsigned int sends = 0;
    bool is_disconnected = false;
    std::string reply;
    int Send(const void* data, int len) { reply.append(static_cast<const char*>(data), len); ++sends; return len; }
    CTelnetConn() { ui.telnet = this; CreateBuffer(); }
    ~CTelnetConn() { for (int i = 0; i < site_settings.line_count; ++i) delete[] screen[i]; delete[] screen; }
    void OnText(bool fromNetwork = false);
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
    BOOL IsEmptyLine(LPSTR, int);
    void EditorCarriageRetiurn();
    void EditorLineFeed(LPSTR, LPBYTE, int);
    void EditorLineBack(LPSTR, LPBYTE, int);
    void End();
    void Back(int num = 1);
    void ClearCurrentLine(int);
    void ClearScreen(int) {}
    void InsertLines(int) {}
    void GoUp(int n) { cursor_pos.y = (std::max)(first_line, cursor_pos.y - n); }
    void GoDown(int n) { cursor_pos.y = (std::min)(last_line, cursor_pos.y + n); }
    void GoRight(int);
    void GoLeft(int n) { cursor_pos.x = (std::max)(0L, cursor_pos.x - n); }
    void SaveCursorPos() { old_cursor_pos = cursor_pos; }
    void RestoreCursorPos() { cursor_pos = old_cursor_pos; }
    void ShiftUtf8Cells(int, bool);
    void InsertChar(int);
    void DeleteLines(int) {}
    void DeleteChar(int);
    void Delete(int);
    void BackTab(int) {}
    void SetMode(int) {}
    void ResetMode(int) {}
    void DeviceStatusReport(int);
    void ScrollUp() {}
    void ScrollDown() {}
    void Feed(std::string input)
    {
        buf = reinterpret_cast<BYTE*>(&input[0]); last_byte = buf + input.size(); OnText(true);
    }
    std::string Row(int y) { return std::string(screen[first_line + y], site_settings.cols_per_page); }
};
static std::wstring painted;
static bool IsAttrBlink(BYTE value) { return (value & 128) != 0; }
static BOOL CaptureText(HDC, int, int, UINT, LPCRECT, LPCWSTR text, UINT count, const INT*)
{
    painted.append(text, count);
    return TRUE;
}
#define ExtTextOutW CaptureText
#include "ProductionBoundaryMethods.inc"
#undef ExtTextOutW

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
            Check(t.sends == 0 && t.cur_attr == 7 && t.remote_line_wrap == 1 &&
                t.scr_top == 0 && t.scr_bottom == 3 && !t.sync_output.Active() && !t.ui.doflash,
                "production OnText cannot execute opaque queries, modes, SGR or BEL");
            t.Feed("X");
            Check(t.Row(1) == "X       " && t.GetLineAttr(t.first_line + 1)[0] == 7 &&
                !t.osc_links.At(t.screen[t.first_line + 1], 0), "text after ST keeps wrap, color and link scope");
            t.Feed("\x1b[6n");
            Check(t.reply == "\x1b[2;2R", "ordinary query returns live cursor after opaque string");
        }
        {
            CTelnetConn t;
            t.Feed("\x1b[?2026h");
            t.Feed(start + "\x1b[?2026l\x1b[6n" + st);
            Check(t.sync_output.Active() && t.sends == 0, "opaque end-sync cannot publish live frame");
            t.Feed("\x1b[?2026l");
            Check(!t.sync_output.Active(), "real end-sync after opaque string still releases frame");
        }
        for (const std::string suffix : { std::string(), std::string("\x1b") })
        {
            CTelnetConn t;
            t.Feed(start + "partial" + suffix);
            t.ClearAllFlags(); // Extracted production lifecycle reset, not a duplicate implementation.
            t.Feed("NEW\x1b[6n");
            Check(t.Row(0) == "NEW     " && t.sends == 1,
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

static void CheckBig5Receive()
{
    const std::string pair = "\xa4\xa4";
    const std::string input = "\x1b[?7h1234567\x1b]8;;https://example.com\a\x1b[31m\xa4"
        "\x1b[6n\x1b]8;;\a\x1b[32m\xa4X";
    for (size_t cut = 0; cut <= input.size(); ++cut)
    {
        CTelnetConn t;
        t.Feed(input.substr(0, cut));
        t.Feed(input.substr(cut));
        const auto row = t.screen[t.first_line + 1];
        Check(t.Row(0) == "1234567 " && t.Row(1) == pair + "X     " &&
            t.cursor_pos == CPoint(3, t.first_line + 1), "wrap complete Big5 pair at every packet split");
        Check(t.GetLineAttr(row)[0] == 1 && t.GetLineAttr(row)[1] == 2 &&
            t.osc_links.At(row, 0) && !t.osc_links.At(row, 1) && t.reply == "\x1b[1;8R",
            "SGR and OSC between halves retain original attributes and link ownership");
    }
    for (const std::string mode : {std::string(), std::string("\x1b[?7l")})
    {
        CTelnetConn t;
        t.Feed(mode + "1234567" + pair);
        Check(t.Row(0) == "1234567 " && t.cursor_pos.x == 7, "non-fitting pair leaves final column intact");
        t.Feed("!");
        Check(t.Row(0) == "1234567!", "ASCII still fits after omitted pair");
    }
    CTelnetConn t;
    t.Feed("A\xa4");
    Check(t.Row(0) == "A       " && t.cursor_pos.x == 1, "packet end cannot publish a lead byte");
    t.Feed("!"); // Not a Big5 trail.
    Check(t.Row(0) == std::string("A\xa4!     "), "invalid trail preserves both original bytes");
    t.Feed("\xa4\rZ");
    Check(t.Row(0) == std::string("Z\xa4!\xa4    "), "finish old lead before cursor motion");
    t.Feed("\xa4");
    t.ClearAllFlags();
    t.Feed("Q");
    Check(!t.pending_big5 && t.Row(0)[1] == 'Q', "new session cannot reuse old lead");
    CTelnetConn edge;
    edge.Feed("123456" + pair + "\x1b[1;8H\x1b[?7h" + pair);
    Check(edge.Row(0) == "123456  " && edge.Row(1) == pair + "      ",
        "wrapping over an existing wide trailing cell clears both old halves");
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
    using TerminalCells::EncodedRow;
    for (const UINT cp : {950u, static_cast<UINT>(CP_UTF8)})
    {
        CTelnetConn t; t.ui.codePage = cp;
        const std::string wide = cp == 950 ? "\xa4\xa4" : "\xe4\xb8\xad";
        const EncodedRow initial("A" + wide + "B");
        auto& store = t.encoded_rows;
        char* row = t.screen[t.first_line];
        auto raw = [](const EncodedRow& r) {
            std::string s; for (const auto& b : r.Bytes()) s += static_cast<char>(b.value); return s;
        };
        // Seed different source and mirror values to verify which survives EL.
        Check(store.TrySet(row, initial), "seed source for EL and crop");
        t.Feed("\x1b[1;3H\x1b[K");
        Check(raw(store.At(row)) == "A       ", "EL through right half repairs whole glyph");
        Check(store.TrySet(row, initial), "reseed for left-half erase");
        std::memcpy(row, "12345678", 8);
        t.Feed("\x1b[1;2H\x1b[1K");
        Check(t.Row(0) == "   B    " && t.cursor_pos.x == 1,
            "EL 1 includes cursor and preserves the active source suffix");
        Check(raw(store.At(row)) == "   B", "EL prefix repairs trailing half and keeps suffix");
        t.Feed("\x1b[2K");
        Check(t.Row(0) == "        ", "EL 2 clears whole byte buffer");
        Check(!store.StoredRows(), "EL 2 returns row to virtual blank");
        Check(store.TrySet(row, initial), "reseed for width change");
        t.ReSizeBuffer(6, 2, 4);
        Check(raw(store.At(t.screen[t.first_line])) == "A", "physical crop never retains half a glyph");
        t.ReSizeBuffer(6, 8, 4);
        Check(raw(store.At(t.screen[t.first_line])) == "A", "growing cannot resurrect discarded suffix");
    }
    {
        TerminalCells::EncodedRowStorage store(2, 8);
        int a, b;
        Check(store.TrySet(&a, EncodedRow("A\xe4\xb8\xad" "B")) && store.TrySet(&b, EncodedRow("123")), "seed limited store");
        Check(!store.TryEdit(&b, [](const EncodedRow& old) {
            return old.ReplaceUtf8(8, 4, 5, EncodedRow("X"));
        }) && store.SourceBytes() == 8 && store.At(&b).ReadUtf8(3).Text(0, 3) == U"123",
            "overwrite quota failure preserves bytes and accounting");
        int visited = 0;
        Check(!store.TryCropAll([&](const EncodedRow& r) {
            if (++visited == 2) throw std::bad_alloc();
            return r.ReadUtf8(1);
        }) && visited == 2 && store.SourceBytes() == 8 && store.StoredRows() == 2 &&
            store.At(&a).ReadUtf8(4).Text(0, 4) == U"A中B" && store.At(&b).ReadUtf8(3).Text(0, 3) == U"123",
            "second-row failure leaves first crop uncommitted");
        Check(store.TryEdit(&a, [](const EncodedRow& old) {
            return old.ReplaceUtf8(8, 2, 3, EncodedRow("X"));
        }) && store.At(&a).ReadUtf8(4).Text(0, 4) == U"A XB", "overwrite only one half repairs the other");
    }
    {
        CTelnetConn t;
        auto& store = t.encoded_rows;
        Check(!store.StoredRows(), "real buffer allocates virtual blank source rows");
        Check(store.TrySet(t.screen[0], TerminalCells::EncodedRow("old")) &&
            store.TrySet(t.screen[1], TerminalCells::EncodedRow("keep")), "seed real row identities");
        char* kept = t.screen[1];
        t.cursor_pos.y = t.last_line;
        t.LineFeed();
        Check(t.screen[0] == kept && store.At(t.screen[0]).Bytes().size() == 4 &&
            store.StoredRows() == 1 && store.SourceBytes() == 4, "scroll rotates source and erases recycled identity");
        t.ReSizeBuffer(6, 10, 4);
        Check(store.StoredRows() == 1 && store.At(t.screen[0]).Bytes().size() == 4,
            "width change transfers source identity");
        Check(store.TrySet(t.screen[5], TerminalCells::EncodedRow("tail")), "seed removed row");
        t.ReSizeBuffer(4, 10, 4);
        Check(store.StoredRows() == 1 && store.SourceBytes() == 4, "shrink releases removed row source");
        t.InitNewLine(t.screen[0]);
        Check(!store.StoredRows() && !store.SourceBytes(), "explicit clear releases source");
    }
    CTelnetConn t;
    t.Feed("\x1b[3;5H\x1b[2;3r");
    Check(t.scr_top == 1 && t.scr_bottom == 2, "DECSTBM sets margins");
    Check(t.cursor_pos == CPoint(0, t.first_line), "DECSTBM homes cursor without a server-supplied CUP");
    CheckWrapModes();
    {
        CTelnetConn blink;
        blink.ui.codePage = CP_UTF8;
        blink.Feed(u8"中中中");
        const char* row = blink.screen[blink.first_line];
        Check(!blink.HasUtf8Blink(row), "plain UTF-8 row needs no blink repaint");
        blink.Feed("\x1b[5mX");
        Check(blink.HasUtf8Blink(row) && !IsAttrBlink(blink.GetLineAttr(row)[7]),
            "blink repaint includes a visible glyph whose byte offset exceeds the row width");
        blink.Feed("\x1b[1;7H\x1b[0mX");
        Check(!blink.HasUtf8Blink(row), "overwriting final blinking glyph ends timer redraw");
    }
    {
        CTelnetConn links;
        links.ReSizeBuffer(6, 32, 4);
        links.ui.codePage = CP_UTF8;
        links.Feed(u8"中中中中中中中中中https://x.tw!");
        const auto visible = links.ReadUtf8Source(links.screen[links.first_line]).ReadUtf8(32).AsciiColumns();
        Check(visible == std::string(18, ' ') + "https://x.tw! ",
            "URL scanner gets one byte per visible column including the URL tail beyond the mirror");
    }
    for (int limit : {2, 3})
    {
        CTelnetConn batch;
        batch.ui.codePage = CP_UTF8;
        batch.encoded_rows = TerminalCells::EncodedRowStorage(1, limit);
        batch.Feed("B\xe2");
        batch.Feed("A\x1b[6n");
        if (limit == 2)
            Check(batch.source_limit_stopped && batch.cursor_pos.x == 1 && batch.Row(0) == "B       " &&
                batch.utf8_decoder.Pending() && !batch.sends, "rejected recovery batch retains prefix, cursor and old row");
        else
            Check(!batch.source_limit_stopped && batch.reply == "\x1b[1;5R" &&
                batch.ReadUtf8Source(batch.screen[batch.first_line]).ReadUtf8(8).Text(0,4) == U"B\ufffdA",
                "accepted recovery batch publishes both glyphs and correct report");
    }
    for (bool scroll : {false, true})
    {
        CTelnetConn split;
        split.ui.codePage = CP_UTF8;
        split.encoded_rows = TerminalCells::EncodedRowStorage(1, 8);
        split.Feed(scroll ? "\x1b[4;1H" : "\x1b[1;1H");
        split.Feed("\x1b[?7h123456\xe2");
        const auto before = split.cursor_pos;
        const auto history = split.screen[0];
        split.Feed("A");
        Check(split.source_limit_stopped && split.cursor_pos == before && split.screen[0] == history &&
            split.Row(scroll ? 3 : 0) == "123456  ", "second recovery glyph cannot partially commit a first glyph before a refused wrap");
        CTelnetConn limited;
        limited.ui.codePage = CP_UTF8;
        limited.encoded_rows = TerminalCells::EncodedRowStorage(1, 8);
        limited.Feed(scroll ? "\x1b[4;1H" : "\x1b[1;1H");
        limited.Feed("12345678\x1b[?7h");
        const auto cursor = limited.cursor_pos;
        const auto oldTop = limited.screen[0];
        limited.Feed(u8"中");
        Check(limited.source_limit_stopped && limited.cursor_pos == cursor &&
            limited.screen[0] == oldTop && limited.Row(scroll ? 3 : 0) == "12345678",
            "refused wide wrap leaves the old edge, cursor and scrollback intact");
    }
    {
        CTelnetConn recycled;
        recycled.ui.codePage = CP_UTF8;
        recycled.encoded_rows = TerminalCells::EncodedRowStorage(2, 11);
        char* top = recycled.screen[0];
        Check(recycled.encoded_rows.TrySet(top, TerminalCells::EncodedRow("old")), "seed recyclable source");
        recycled.Feed("\x1b[4;1H12345678\x1b[?7h");
        recycled.Feed(u8"中\x1b[6n");
        Check(recycled.screen[recycled.last_line] == top && recycled.reply == "\x1b[4;3R" &&
            recycled.encoded_rows.SourceBytes() == 11 && recycled.Row(2) == "1234567 " &&
            recycled.ReadUtf8Source(top).ReadUtf8(8).Text(0,2) == U"中",
            "accepted wide wrap reuses scrollback quota, clears the edge and commits the new cursor");
    }
    {
        CTelnetConn limited;
        limited.ui.codePage = CP_UTF8;
        limited.encoded_rows = TerminalCells::EncodedRowStorage(1, 2);
        limited.Feed("AB\x1b[?2026hC\x1b[6n\x1b[?2026l");
        limited.Feed("Z\x1b[6n");
        limited.StopForSourceLimit();
        Check(limited.closes == 1 && limited.source_limit_stopped && limited.is_disconnected &&
            !limited.is_connected && !limited.sends, "quota rejection stops one connection once and suppresses later CPR");
        Check(limited.cursor_pos.x == 2 && limited.Row(0) == "AB      " && limited.sync_output.Active(),
            "single-glyph refusal preserves row, cursor and frozen frame");
    }
    {
        CTelnetConn unicode;
        unicode.ui.codePage = CP_UTF8;
        unicode.Feed("\xc4");
        Check(unicode.cursor_pos.x == 0, "partial UTF8 probe occupies no cells");
        unicode.Feed("\xa1\x1b[6n");
        Check(unicode.reply == "\x1b[1;2R", "PTT probe reports UTF8 column two");
        CTelnetConn legacy;
        legacy.Feed("\r\xc4\xa1\x1b[6n");
        Check(legacy.reply == "\x1b[1;3R", "PTT probe reports Big5 column three");
        CTelnetConn switched;
        switched.ui.codePage = CP_UTF8;
        switched.Feed(u8"abc中中X");
        switched.FinishReceivedText();
        switched.ui.codePage = 950;
        switched.Feed("\r\x1b[31m\xa4\xa4!\x1b[6n");
        Check(switched.reply == "\x1b[1;4R", "Big5 write after UTF-8 switch reports three occupied columns");
        switched.FinishReceivedText();
        switched.ui.codePage = CP_UTF8;
        const auto retained = switched.ReadUtf8Source(switched.screen[switched.first_line]);
        const std::string expected = std::string("\xa4\xa4!") + u8"中中X";
        std::string source;
        for (const auto& byte : retained.Bytes()) source += static_cast<char>(byte.value);
        Check(source == expected && retained.Bytes()[0].style.attribute == 1 &&
            retained.Bytes()[1].style.attribute == 1 && retained.Bytes().back().style.attribute == 7,
            "legacy edits retain both new byte colors and the UTF-8 suffix beyond the mirror");
        CTelnetConn edit;
        edit.ui.codePage = CP_UTF8;
        edit.Feed(u8"A中B中XY");
        const auto edited = [&]() { return edit.ReadUtf8Source(edit.screen[edit.first_line]).ReadUtf8(8).Text(0,8); };
        edit.Feed("\x1b[1;2H\x1b[@\x1b[6n");
        Check(edited() == U"A 中B中X" && edit.reply == "\x1b[1;2R", "ICH shifts complete UTF-8 glyphs and keeps CPR position");
        edit.Feed("\x1b[1;4H\x1b[P");
        Check(edited() == U"A  B中X ", "DCH at a continuation repairs the wide glyph and shifts suffix");
        edit.Feed("\x1b[1;7H\x1b[K");
        Check(edited() == U"A  B中  " && !memcmp(edit.screen[edit.first_line], u8"A  B中 ", 8),
            "partial erase mirrors the edited UTF-8 bytes at their actual byte offsets");
        Check(unicode.cursor_pos.x == 1, "UTF8 probe advances to CPR column two");
        unicode.Feed("\r\x1b[?7h");
        unicode.Feed(u8"A中B中中X");
        Check(unicode.cursor_pos == CPoint(1, unicode.first_line + 1) &&
            unicode.ReadUtf8Source(unicode.screen[unicode.first_line]).ReadUtf8(8).Text(0,8) == U"A中B中中",
            "retain eleven source bytes across eight visible cells and wrap the next glyph");
    }
    for (bool grouped : {false, true})
    {
        CTelnetConn terminal;
        terminal.ReSizeBuffer(6, 80, 4);
        terminal.ui.codePage = CP_UTF8;
        terminal.Feed(u8"請輸入您的密碼😀");
        AppConfig.old_textout = grouped;
        painted.clear();
        CDC dc;
        terminal.ui.DrawUtf8Line(dc, terminal.screen[terminal.first_line], nullptr, nullptr, 0);
        Check(painted.find(L"請輸入您的密碼😀") == 0, "UTF8 password prompt and surrogate pair reach GDI intact");
        const std::string schedule = u8"06:00   ゆるゆる図鑑                               テレ東系列";
        const CString pasted = WrapPasteText(schedule.c_str(), 0, 80, "\r\n", CP_UTF8);
        Check(pasted.GetLength() == static_cast<int>(schedule.size()) && pasted == schedule.c_str(),
            "plain UTF-8 paste preserves Japanese schedule bytes exactly");
        Check(WrapPasteText(u8"テレ東系列", 0, 8, "\r\n", CP_UTF8) == u8"テレ東系\r\n列",
            "paste wraps at the cell boundary before the last complete Japanese character");
        CTelnetConn editor;
        editor.ReSizeBuffer(6, 80, 4);
        editor.ui.codePage = CP_UTF8;
        editor.is_ansi_editor = true;
        for (bool colored : {false, true})
        {
            editor.cursor_pos.x = 0;
            std::string input = colored ? "\x1b[31m" + schedule + "\x1b[m" : std::string(pasted.GetString(), pasted.GetLength());
            editor.buf = reinterpret_cast<BYTE*>(input.data());
            editor.last_byte = editor.buf + input.size();
            editor.OnText(false);
            painted.clear();
            editor.ui.DrawUtf8Line(dc, editor.screen[editor.cursor_pos.y], nullptr, nullptr, 0);
            const std::wstring expectedEditor = L"06:00   ゆるゆる図鑑                               テレ東系列";
            Check(painted.compare(0, expectedEditor.size(), expectedEditor) == 0 &&
                painted.find_first_not_of(L' ', expectedEditor.size()) == std::wstring::npos,
                "ANSI editor local input renders UTF-8 schedule without trailing garbage");
        }
        std::string longLine;
        for (int i = 0; i < 40; ++i) longLine += u8"中";
        std::string article = "\r\n\x1b[32m" + longLine + "\x1b[m\r\n" + schedule;
        const int articleRow = editor.cursor_pos.y + 1;
        editor.insert_mode = true;
        editor.buf = reinterpret_cast<BYTE*>(article.data());
        editor.last_byte = editor.buf + article.size();
        editor.OnText(false);
        const auto body = editor.ReadUtf8Source(editor.screen[articleRow]).ReadUtf8(80);
        Check(body.RawText(0,80) == longLine && body.At(78).style.attribute == 2,
            "ANSI editor retains all 120 UTF-8 bytes of an 80-column colored article row");
        const auto following = editor.ReadUtf8Source(editor.screen[articleRow + 1]).ReadUtf8(80);
        Check(following.RawText(0, editor.cursor_pos.x) == schedule,
            "the following Japanese schedule starts on a fresh editor row");
        editor.cursor_pos = CPoint(40, articleRow);
        editor.EditorCarriageRetiurn();
        editor.cursor_pos.x = 0;
        editor.Back();
        const auto joined = editor.ReadUtf8Source(editor.screen[articleRow]).ReadUtf8(80);
        Check(joined.RawText(0, 80) == longLine && joined.At(78).style.attribute == 2,
            "splitting then joining a full UTF-8 editor row preserves text and colors");
        terminal.Feed("\r\x1b[2K" + schedule);
        painted.clear();
        terminal.ui.DrawUtf8Line(dc, terminal.screen[terminal.first_line], nullptr, nullptr, 0);
        const std::wstring expected = L"06:00   ゆるゆる図鑑                               テレ東系列";
        Check(painted.compare(0, expected.size(), expected) == 0 &&
            painted.find_first_not_of(L' ', expected.size()) == std::wstring::npos,
            "Japanese schedule repaint emits only spaces after the last network name");
        auto exported = terminal.ReadUtf8Source(terminal.screen[terminal.first_line]).ReadUtf8(80).RawText(0,80);
        exported.erase(exported.find_last_not_of(' ') + 1);
        Check(exported == schedule, "Japanese schedule retains exact UTF-8 row bytes for export");
    }
    {
        CTelnetConn query;
        query.Feed("\x1b[n\x1b[5n\x1b[6;1n\x1b[;6n\x1b[6;n\x1b[?6n");
        Check(query.sends == 0, "reject status queries other than plain numeric CPR");
        query.Feed("\x1b[0006n");
        Check(query.sends == 1, "accept a single numeric CPR with leading zeros");
    }
    CheckBig5Receive();
    CheckOpaqueStrings();
    CheckScrolling();
    CheckResize();
    CheckIntegration();
    std::printf("PASS: %u terminal boundary checks\n", checks);
}
