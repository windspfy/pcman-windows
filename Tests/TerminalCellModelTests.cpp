#include "../Lite/TerminalCellModel.h"
#include "../Lite/EncodedTerminalRow.h"
#include <cstdio>
#include <cstdlib>
#include <climits>
#include <vector>

using namespace TerminalCells;

static unsigned int checks = 0;
static void Check(bool ok, const char* why)
{
    ++checks;
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", why); std::exit(1); }
}

static std::string Encode(char32_t cp)
{
    // Independent UTF-8 serialization used only to exercise decoder round trips.
    std::string s;
    if (cp <= 0x7f) s += static_cast<char>(cp);
    else if (cp <= 0x7ff)
    {
        s += static_cast<char>(0xc0 | (cp >> 6));
        s += static_cast<char>(0x80 | (cp & 63));
    }
    else if (cp <= 0xffff)
    {
        s += static_cast<char>(0xe0 | (cp >> 12));
        s += static_cast<char>(0x80 | ((cp >> 6) & 63));
        s += static_cast<char>(0x80 | (cp & 63));
    }
    else
    {
        s += static_cast<char>(0xf0 | (cp >> 18));
        s += static_cast<char>(0x80 | ((cp >> 12) & 63));
        s += static_cast<char>(0x80 | ((cp >> 6) & 63));
        s += static_cast<char>(0x80 | (cp & 63));
    }
    return s;
}
static std::u32string Decode(const std::string& bytes)
{
    Utf8Decoder decoder;
    std::u32string text;
    for (unsigned char b : bytes) decoder.Feed(b, [&](char32_t cp) { text += cp; });
    decoder.Finish([&](char32_t cp) { text += cp; });
    return text;
}
static void CheckInvariant(const Screen& screen)
{
    const auto cursor = screen.Cursor();
    Check(cursor.row >= 0 && cursor.row < screen.Rows() && cursor.column >= 0 &&
        cursor.column < screen.Columns(), "cursor always addresses a visible cell");
    Check(!cursor.wrapPending || cursor.column == screen.Columns() - 1, "pending wrap only at last column");
    for (int row = 0; row < screen.Rows(); ++row)
        for (int col = 0; col < screen.Columns(); ++col)
        {
            const auto& cell = screen.At(row, col);
            if (cell.continuation)
                Check(col > 0 && screen.At(row, col - 1).width == 2 && cell.width == 0 && cell.text.empty(),
                    "continuation always belongs to one complete wide glyph");
            else
            {
                Check(!cell.text.empty() && cell.text.size() <= 33 &&
                    cell.width == static_cast<unsigned int>(Width(cell.text.front())), "bounded scalar text and width agree");
                if (cell.width == 2)
                    Check(col + 1 < screen.Columns() && screen.At(row, col + 1).continuation,
                        "wide lead always has a continuation");
            }
        }
}
static void CheckDecoderAndWidths()
{
    struct Golden { char32_t cp; int width; };
    // Literals from the frozen PTT default profile, not from the product calculation.
    for (const auto& g : std::vector<Golden>{ {0,0}, {0x7f,0}, {0x9f,0}, {U'A',1},
        {0x121,1}, {0xe2,1}, {0xa1,2}, {0x301,0}, {0x1160,0}, {0x200d,0},
        {0x2500,2}, {0x4e2d,2}, {0xe000,2}, {0xfe0f,0}, {0xff61,1}, {0xfffd,2},
        {0x1f600,2}, {0x20000,2}, {0xe0100,0}, {0x10fffd,2}, {0x10ffff,1},
        {0xd800,0}, {0x110000,0} })
        Check(Width(g.cp) == g.width, "PTT width golden vector");
    Check(Decode("\xc4\xa1") == U"\u0121", "PTT encoding probe decodes to one narrow Unicode scalar");
    Check(Decode("\xf0\x9f\x98\x80") == U"\U0001f600", "four bytes decode to one supplementary scalar");
    struct Invalid { std::string bytes; std::u32string expected; };
    for (const auto& g : std::vector<Invalid>{
        {"\x80", U"\ufffd"}, {"\xc0\xaf", U"\ufffd\ufffd"},
        {"\xe0\x80\x80", U"\ufffd\ufffd\ufffd"},
        {"\xed\xa0\x80", U"\ufffd\ufffd\ufffd"},
        {"\xf4\x90\x80\x80", U"\ufffd\ufffd\ufffd\ufffd"},
        {"\xf5\x80", U"\ufffd\ufffd"}, {"\xe2\x82", U"\ufffd"},
        {"\xe2\x82" "A", U"\ufffdA"}, {"\xe2\xc4\xa1", U"\ufffd\u0121"},
        {"\xff\xfe", U"\ufffd\ufffd"} })
        Check(Decode(g.bytes) == g.expected, "strict maximal-subpart UTF8 recovery without losing next character");
    Utf8Decoder first, second;
    std::u32string out;
    first.Feed(0xe4, [&](char32_t cp) { out += cp; });
    Check(first.Pending() && !second.Pending() && out.empty(), "decoder state is local and incomplete input is not emitted");
    first.Reset(); first.Finish([&](char32_t cp) { out += cp; });
    Check(out.empty() && !first.Pending(), "reset drops an old session's unfinished input");
    // Every Unicode scalar: validates all lead/second-byte ranges, including NUL.
    for (std::uint32_t cp = 0; cp <= 0x10ffff; ++cp)
    {
        if (cp >= 0xd800 && cp <= 0xdfff) continue;
        const auto scalar = static_cast<char32_t>(cp);
        const auto decoded = Decode(Encode(scalar));
        Check(decoded.size() == 1 && decoded[0] == scalar, "every legal Unicode scalar round-trips through streaming decoder");
        Check(Width(scalar) >= 0 && Width(scalar) <= 2, "every scalar has bounded PTT width");
    }
}
static void CheckStreaming()
{
    const std::string bytes = u8"A中B\u0121\U0001f600e\u0301";
    for (std::size_t split = 0; split <= bytes.size(); ++split)
    {
        Screen screen(20, 3);
        screen.WriteUtf8(std::string_view(bytes).substr(0, split));
        screen.WriteUtf8(std::string_view(bytes).substr(split));
        Check(screen.Text(0, 0, 8) == U"A中B\u0121\U0001f600e\u0301", "all packet splits preserve full characters and zero-width mark");
        Check(screen.Cursor().column == 8 && !screen.PendingUtf8(), "all packet splits have identical cell cursor");
        CheckInvariant(screen);
    }
    Screen bytewise(20, 3);
    for (char b : bytes) bytewise.WriteUtf8(std::string_view(&b, 1));
    Check(bytewise.Text(0, 0, 8) == U"A中B\u0121\U0001f600e\u0301", "one byte per packet preserves supplementary characters");
    Screen probe(8, 2);
    probe.WriteUtf8("\xc4");
    Check(probe.Cursor().column == 0 && probe.PendingUtf8(), "probe lead byte does not consume a cell");
    probe.WriteUtf8("\xa1");
    Check(probe.Cursor().column == 1 && probe.At(0, 0).text == U"\u0121", "probe would report column two after integration, not three");
    Screen styled(12, 2);
    styled.WriteUtf8("\xe4", {15, 42}); styled.WriteUtf8("\xb8\xad", {2, 90});
    Check(styled.At(0, 0).style.attribute == 15 && styled.At(0, 1).style.link == 42,
        "character style and link are latched at the first byte");
    styled.SetStyle(0, 1, {11, 42});
    Check(styled.At(0, 0).style.attribute == 15 && styled.At(0, 1).style.attribute == 11,
        "wide halves retain independent attributes without duplicating text");
    styled.WriteUtf8("\xe2", {1, 1}); styled.WriteUtf8("A", {2, 2});
    Check(styled.At(0, 2).text == U"\ufffd" && styled.At(0, 2).style.attribute == 1 &&
        styled.At(0, 4).text == U"A" && styled.At(0, 4).style.attribute == 2,
        "invalid prefix and reprocessed byte keep separate styles");
    styled.WriteUtf8("\xf0\x9f"); styled.FinishText();
    const auto after = styled.Cursor(); styled.FinishText();
    Check(!styled.PendingUtf8() && styled.Cursor().column == after.column, "EOF flush is explicit and idempotent");
    Screen copy = probe;
    copy.Reset(); copy.WriteUtf8("X");
    Check(probe.At(0, 0).text == U"\u0121", "screen copies are independent");
}
static void CheckGlyphsAndMargins()
{
    Screen screen(4, 3);
    screen.WriteUtf8(u8"AB中");
    Check(screen.Cursor().column == 3 && screen.Cursor().wrapPending, "wide glyph ending at edge leaves wrap pending");
    const auto snapshot = screen.Capture();
    for (int i = 0; i < 5; ++i) Check(screen.Cursor().wrapPending, "cursor queries do not consume wrap");
    screen.WriteUtf8(u8"\u0301");
    Check(screen.At(0, 2).text == U"中\u0301" && screen.Cursor().wrapPending, "mark attaches to last wide lead without wrapping");
    screen.WriteUtf8("X");
    Check(screen.At(1, 0).text == U"X" && screen.Cursor().column == 1, "next printable glyph wraps exactly once");
    Check(snapshot.rows[0][2].text == U"中" && snapshot.cursor.wrapPending, "snapshot preserves complete text and cursor independently");
    screen.MoveTo(0, 3); screen.WriteUtf8("Z");
    Check(screen.At(0, 2).text == U" " && screen.At(0, 3).text == U"Z", "overwriting wide trailing half clears old lead");
    screen.MoveTo(0, 0); screen.WriteUtf8(u8"中文");
    screen.MoveTo(0, 1); screen.WriteUtf8(u8"中");
    Check(screen.At(0, 0).text == U" " && screen.At(0, 3).text == U" ", "new wide glyph repairs both overlapping old glyphs");
    Check(screen.Text(0, 1, 2).empty() && screen.Text(0, 2, 3).empty() &&
        screen.Text(0, 1, 3) == U"中", "selection omits incomplete wide glyphs from either side");
    screen.Erase(0, 2, 3);
    Check(screen.At(0, 1).text == U" " && !screen.At(0, 2).continuation, "erase of one half removes the whole glyph and metadata");
    Screen edge(4, 2);
    edge.WriteUtf8(u8"ABC中");
    Check(edge.At(0, 3).text == U" " && edge.At(1, 0).text == U"中", "wide glyph with one cell remaining wraps atomically");
    edge.Reset(); edge.SetAutoWrap(false); edge.WriteUtf8(u8"ABC中Z");
    Check(edge.Text(0, 0, 4) == U"ABCZ" && edge.Cursor().row == 0 && !edge.Cursor().wrapPending,
        "disabled wrap omits unfittable wide glyph and overwrites last cell with narrow glyph");
    edge.WriteUtf8(u8"\u0301");
    Check(edge.At(0, 3).text == U"Z\u0301", "mark at disabled-wrap margin attaches to the written glyph");
    Screen single(1, 2); single.WriteUtf8("A"); single.WriteUtf8(u8"中");
    Check(single.Cursor().row == 0 && single.Cursor().wrapPending && single.At(0, 0).text == U"A",
        "unfittable wide glyph cannot consume wrap in one-column viewport");
    edge.Reset(); edge.WriteUtf8(u8"\u0301");
    Check(edge.At(0, 0).text == U" " && edge.Cursor().column == 0, "orphan zero-width scalar is ignored");
    edge.WriteUtf8("e");
    for (int i = 0; i < 1000; ++i) edge.WriteUtf8(u8"\u0301");
    Check(edge.At(0, 0).text.size() == 33 && edge.Cursor().column == 1, "zero-width storage is bounded to 32 suffixes per glyph");
    edge.MoveTo(0, 2); edge.WriteUtf8(u8"\u0301");
    Check(edge.At(0, 1).text == U" ", "cursor movement ends zero-width attachment");
    Screen emoji(12, 2); emoji.WriteUtf8(u8"\U0001f469\u200d\U0001f4bb");
    Check(emoji.Cursor().column == 4 && emoji.At(0, 0).text == U"\U0001f469\u200d",
        "ZWJ preserves PTT scalar widths, not browser grapheme aggregation");
    CheckInvariant(screen); CheckInvariant(edge); CheckInvariant(single); CheckInvariant(emoji);
}
static void CheckRowsAndBounds()
{
    Screen screen(6, 4);
    for (int row = 0; row < 4; ++row) { screen.MoveTo(row, 0); screen.WriteScalar(U'A' + row, {7, static_cast<std::uint32_t>(row + 1)}); }
    Check(screen.SetScrollRegion(1, 2), "valid region accepted");
    screen.MoveTo(2, 0); screen.LineFeed();
    Check(screen.At(0, 0).text == U"A" && screen.At(1, 0).text == U"C" && screen.At(1, 0).style.link == 3 &&
        screen.At(2, 0).text == U" " && screen.At(3, 0).text == U"D", "regional scrolling carries glyphs/styles and protects outer rows");
    screen.MoveTo(0, 1); screen.LineFeed();
    Check(screen.Cursor().row == 1 && screen.At(1, 0).text == U"C", "LF above region does not scroll it");
    screen.MoveTo(3, 1); screen.LineFeed();
    Check(screen.Cursor().row == 3 && screen.At(1, 0).text == U"C", "LF below region is bounded without scrolling");
    Check(!screen.SetScrollRegion(2, 2) && !screen.SetScrollRegion(-1, 2) && !screen.SetScrollRegion(1, 4), "invalid regions rejected");
    screen.MoveTo(0, 0); screen.WriteUtf8(u8"ABC中"); screen.Resize(4, 3);
    Check(screen.Text(0, 0, 4) == U"ABC " && !screen.At(0, 3).continuation, "resize never preserves half a cut wide glyph");
    screen.MoveTo(2, 0); screen.LineFeed();
    Check(screen.At(0, 0).text == U"C", "resize resets scrolling to the full viewport");
    screen.WriteUtf8("\xe4"); screen.Resize(8, 4); screen.WriteUtf8("\xb8\xad");
    Check(!screen.PendingUtf8(), "local resize preserves unfinished UTF8 text until completion");
    screen.Reset(); screen.WriteUtf8("\xe4"); screen.Reset(); screen.WriteUtf8("X");
    Check(screen.At(0, 0).text == U"X" && screen.Cursor().column == 1 && !screen.PendingUtf8(), "reset clears bytes, glyphs and transient cursor state");
    screen.WriteUtf8("\xe4"); screen.MoveTo(1, 1);
    Check(screen.At(0, 1).text == U"\ufffd" && !screen.PendingUtf8(), "real text/control boundary flushes incomplete scalar before movement");
    for (const int bad : {INT_MIN, -1, 0, 161, INT_MAX})
    {
        bool rejected = false;
        try { Screen invalid(bad, 2); } catch (const std::invalid_argument&) { rejected = true; }
        Check(rejected, "invalid column dimensions rejected before arithmetic/allocation");
    }
    for (const int bad : {INT_MIN, -1, 0, 65, INT_MAX})
    {
        bool rejected = false;
        try { Screen invalid(2, bad); } catch (const std::invalid_argument&) { rejected = true; }
        Check(rejected, "invalid row dimensions rejected before arithmetic/allocation");
    }
    const auto before = screen.Capture();
    try { screen.Resize(0, 2); Check(false, "invalid resize must throw"); } catch (const std::invalid_argument&) {}
    Check(screen.Rows() == 4 && screen.Columns() == 8 && screen.At(0, 0).text == before.rows[0][0].text, "invalid resize leaves model intact");
    Check(!screen.MoveTo(-1, 0) && !screen.MoveTo(0, 8), "invalid positions do not mutate the model");
    bool rejected = false;
    try { screen.Text(0, 2, 1); } catch (const std::out_of_range&) { rejected = true; }
    Check(rejected, "reversed text range rejected");
    rejected = false;
    try { screen.Erase(0, -1, 1); } catch (const std::out_of_range&) { rejected = true; }
    Check(rejected, "negative erase range rejected");
    CheckInvariant(screen);
}
static void CheckOperationSequences()
{
    Screen screen(8, 4);
    std::uint32_t state = 0x34f91081;
    for (int i = 0; i < 2500; ++i)
    {
        state = state * 1664525u + 1013904223u;
        const int row = static_cast<int>((state >> 8) % static_cast<unsigned int>(screen.Rows()));
        const int col = static_cast<int>((state >> 16) % static_cast<unsigned int>(screen.Columns()));
        switch (state % 9)
        {
        case 0: screen.MoveTo(row, col); break;
        case 1: screen.WriteUtf8(u8"中"); break;
        case 2: screen.WriteUtf8("A"); break;
        case 3: screen.WriteUtf8(u8"\u0301"); break;
        case 4: screen.Erase(row, col, col + 1); break;
        case 5: screen.LineFeed(); break;
        case 6: screen.Resize(static_cast<int>((state >> 20) % 16) + 1, static_cast<int>((state >> 24) % 4) + 1); break;
        case 7: screen.SetAutoWrap((state & 0x1000) != 0); break;
        case 8: screen.CarriageReturn(); break;
        }
        CheckInvariant(screen);
    }
}
static void CheckDecoderSourceLengths()
{
    const std::vector<std::string> cases = {"", "A", u8"A中Bġ😀e\u0301",
        "\xe2\x82" "A", "\xe2\xc4\xa1", "\xed\xa0\x80", "\xf4\x90\x80\x80",
        "\xf0\x9f\x98", "\xc0\xaf", "\x80\xff", std::string("A\0B", 3)};
    for (const auto& bytes : cases)
        for (std::size_t split = 0; split <= bytes.size(); ++split)
        {
            Utf8Decoder decoder;
            std::u32string text;
            std::vector<unsigned int> lengths;
            const auto emit = [&](char32_t scalar, unsigned int length) {
                text += scalar; lengths.push_back(length);
            };
            for (std::size_t byte = 0; byte < split; ++byte)
                decoder.FeedWithLength(static_cast<unsigned char>(bytes[byte]), emit);
            // Packet ends must not finish a pending prefix.
            for (std::size_t byte = split; byte < bytes.size(); ++byte)
                decoder.FeedWithLength(static_cast<unsigned char>(bytes[byte]), emit);
            decoder.FinishWithLength(emit);
            Check(text == Decode(bytes), "source-length decoder preserves scalar-only decoder behavior");
            std::size_t used = 0;
            for (const auto length : lengths)
            {
                Check(length >= 1 && length <= 4 && used + length <= bytes.size(), "source lengths partition input into bounded subparts");
                used += length;
            }
            Check(used == bytes.size() && !decoder.Pending(), "every original byte is accounted for including malformed EOF");
        }
    Utf8Decoder decoder;
    std::vector<unsigned int> lengths;
    const auto emit = [&](char32_t, unsigned int length) { lengths.push_back(length); };
    decoder.FeedWithLength(0xe2, emit); decoder.FeedWithLength(0x82, emit);
    decoder.FeedWithLength('A', emit);
    Check(lengths == std::vector<unsigned int>{2, 1}, "invalid prefix and reprocessed ASCII keep separate source ranges");
    decoder.FeedWithLength(0xf0, emit); decoder.Reset(); decoder.FinishWithLength(emit);
    Check(lengths.size() == 2, "reset discards pending source length as well as scalar state");
}
// A deterministic test adapter, NOT a claim to exercise Windows or UAO tables.
static EncodedRow::LegacyGlyph TestLegacyGlyph(const EncodedRow::Byte* bytes, std::size_t available)
{
    if (bytes[0].value >= 0x80 && available >= 2)
        return {bytes[0].value == 0xa4 && bytes[1].value == 0xa4 ? U'中' : U'\ufffd', 2, 2};
    return {static_cast<char32_t>(bytes[0].value), 1, 1};
}
static std::string OriginalBytes(const EncodedRow& row)
{
    std::string result;
    for (const auto& byte : row.Bytes()) result += static_cast<char>(byte.value);
    return result;
}
static void CheckProjectionInvariant(const EncodedRow::Projection& view, std::size_t sourceLength)
{
    for (int column = 0; column < view.Columns(); ++column)
    {
        const auto& cell = view.At(column);
        const auto range = view.Source(column);
        Check(range.begin <= range.end && range.end <= sourceLength, "projected source range always within owned bytes");
        if (cell.continuation)
            Check(column > 0 && view.At(column - 1).width == 2 && cell.text.empty() && cell.width == 0,
                "projected continuation belongs to complete glyph");
        else
        {
            Check(cell.width >= 1 && cell.width <= 2 && !cell.text.empty() && cell.text.size() <= 33,
                "projected lead has bounded complete scalar text");
            if (cell.width == 2)
                Check(column + 1 < view.Columns() && view.At(column + 1).continuation,
                    "projected wide glyph cannot straddle right edge");
        }
    }
    for (std::size_t byte = 0; byte <= sourceLength; ++byte)
        Check(view.ColumnForByte(byte) >= 0 && view.ColumnForByte(byte) <= view.Columns(), "byte-to-cell map is bounded including EOF");
}
static void CheckEncodedRows()
{
    EncodedRow probe("\xc4\xa1", {10, 123});
    const auto utf8 = probe.ReadUtf8(8);
    const auto legacy = probe.ReadLegacy(8, TestLegacyGlyph);
    Check(utf8.At(0).text == U"\u0121" && utf8.At(0).width == 1 && utf8.ColumnForByte(2) == 1,
        "PTT probe UTF8 projection uses one cell without mutating a live cursor");
    Check(legacy.At(0).width == 2 && legacy.At(1).continuation && legacy.ColumnForByte(2) == 2,
        "legacy adapter controls width even for an unmapped two-byte glyph");
    Check(probe.ReadUtf8(8).RawText(0, 1) == "\xc4\xa1" && OriginalBytes(probe) == "\xc4\xa1",
        "UTF8 to legacy to UTF8 never transcodes the original source");
    Check(utf8.At(0).style.attribute == 10 && utf8.At(0).style.link == 123, "row projection retains opaque first-byte style");
    const std::string bytes = u8"A中B😀e\u0301";
    EncodedRow row(bytes);
    const auto full = row.ReadUtf8(12);
    Check(full.Text(0, 7) == U"A中B😀e\u0301" && full.RawText(0, 7) == bytes,
        "Unicode and exact byte exports use cell ranges not byte counts");
    Check(full.RawText(1, 2).empty() && full.RawText(2, 3).empty() && full.RawText(1, 3) == u8"中",
        "copying either wide half cannot leak a partial UTF8 glyph");
    Check(full.Text(4, 5).empty() && full.Text(4, 6) == U"😀", "supplementary scalar is one complete wide glyph");
    Check(full.Source(1).begin == 1 && full.Source(1).end == 4 && full.Source(2).end == 4,
        "both wide halves point to one exact byte source range");
    Check(full.ColumnForByte(2) == 1 && full.ColumnForByte(4) == 3 && full.ColumnForByte(bytes.size() - 1) == 6,
        "search byte offsets and combining suffix map back to lead cells");
    Check(full.RawText(7, 12) == "     " && full.Text(7, 12) == U"     ", "virtual right padding has consistent selection semantics");
    const auto clipped = row.ReadUtf8(2);
    Check(clipped.Clipped() && clipped.Text(0, 2) == U"A " && clipped.ColumnForByte(1) == 2,
        "narrow projection clips complete glyph and suffix without wrapping or losing source");
    Check(row.ReadUtf8(12).RawText(0, 7) == bytes, "wider read restores bytes hidden by earlier clipped projection");
    row.ReadLegacy(2, TestLegacyGlyph);
    Check(row.ReadUtf8(12).Text(0, 7) == full.Text(0, 7), "wrong encoding and viewport cannot destroy historical source");

    EncodedRow styled(std::vector<EncodedRow::Byte>{{0xa4, {9, 1}}, {0xa4, {12, 2}}});
    const auto halves = styled.ReadLegacy(4, TestLegacyGlyph);
    Check(halves.At(0).text == U"中" && halves.At(0).style.attribute == 9 &&
        halves.At(1).style.attribute == 12 && halves.At(1).style.link == 2, "legacy projection preserves independent half attributes and targets");
    EncodedRow styledUtf8(std::vector<EncodedRow::Byte>{{0xe4, {9, 1}}, {0xb8, {10, 2}}, {0xad, {12, 3}}});
    const auto uniform = styledUtf8.ReadUtf8(4);
    Check(uniform.At(0).style.attribute == 9 && uniform.At(1).style.attribute == 9 &&
        uniform.At(1).style.link == 1 && styledUtf8.Bytes()[2].style.attribute == 12,
        "UTF8 uses first-byte style on both cells but retains all original byte attributes");
    EncodedRow invalid("\xe2\x82" "A\xff");
    const auto recovered = invalid.ReadUtf8(8);
    Check(recovered.Text(0, 5) == U"\ufffdA\ufffd" && recovered.RawText(0, 5) == "\xe2\x82" "A\xff",
        "malformed UTF8 renders replacements without replacing original bytes");
    Check(recovered.Source(0).end == 2 && recovered.Source(2).begin == 2 && recovered.Source(3).begin == 3,
        "malformed-prefix reprocessing keeps exact source offsets");
    invalid.ReadLegacy(8, TestLegacyGlyph);
    Check(invalid.ReadUtf8(8).RawText(0, 5) == "\xe2\x82" "A\xff", "invalid source also survives encoding round trip");
    EncodedRow truncated("\xf0\x9f\x98");
    const auto eof = truncated.ReadUtf8(4);
    Check(eof.Text(0, 2) == U"\ufffd" && eof.RawText(0, 2) == "\xf0\x9f\x98", "stored row EOF uses replacement while keeping truncated prefix");

    std::string marks = "e";
    for (int i = 0; i < 40; ++i) marks += u8"\u0301";
    EncodedRow bounded(marks);
    const auto boundedView = bounded.ReadUtf8(4);
    Check(boundedView.At(0).text.size() == 33 && boundedView.RawText(0, 1) == marks,
        "visible combining limit never rewrites original historical bytes");
    EncodedRow orphan(u8"\u0301A");
    Check(orphan.ReadUtf8(4).Text(0, 1) == U"A" && OriginalBytes(orphan) == u8"\u0301A", "orphan marks ignored by presentation but retained by source");
    EncodedRow empty;
    Check(empty.Bytes().empty() && empty.ReadUtf8(4).Text(0, 4) == U"    " &&
        empty.ReadUtf8(4).ColumnForByte(0) == 0, "empty source needs no allocated raw padding");

    const auto snapshot = styled.ReadLegacy(4, TestLegacyGlyph);
    styled = EncodedRow("X");
    Check(snapshot.RawText(0, 2) == "\xa4\xa4" && snapshot.At(1).style.link == 2,
        "projection snapshot owns its original data independently of row replacement");
    for (const int columns : {INT_MIN, 0, 161, INT_MAX})
    {
        bool rejected = false;
        try { empty.ReadUtf8(columns); } catch (const std::invalid_argument&) { rejected = true; }
        Check(rejected, "invalid projection dimensions rejected before allocation");
    }
    bool rejected = false;
    try { EncodedRow tooLong(std::string(160u * 33u * 4u + 1u, 'x')); }
    catch (const std::length_error&) { rejected = true; }
    Check(rejected, "row storage has an explicit source memory bound");
    EncodedRow maximum(std::string(160u * 33u * 4u, 'x'));
    Check(maximum.Bytes().size() == 21120 && maximum.ReadUtf8(1).Clipped() &&
        maximum.ReadUtf8(160).Text(0, 160) == std::u32string(160, U'x'),
        "maximum bounded source can be projected without truncating its owned bytes");
    rejected = false;
    try { EncodedRow tooLong(std::vector<EncodedRow::Byte>(21121)); }
    catch (const std::length_error&) { rejected = true; }
    Check(rejected, "styled-byte constructor enforces the same memory bound");
    rejected = false;
    try { full.RawText(2, 1); } catch (const std::out_of_range&) { rejected = true; }
    Check(rejected, "reversed source selection rejected");
    rejected = false;
    try { full.Text(-1, 1); } catch (const std::out_of_range&) { rejected = true; }
    Check(rejected, "negative projected selection rejected");
    for (const auto bad : std::vector<EncodedRow::LegacyGlyph>{{U'A', 0, 1}, {U'A', 3, 1},
        {U'A', 1, 0}, {U'A', 1, 3}, {0xd800, 1, 1}, {0x110000, 1, 1}})
    {
        rejected = false;
        try { row.ReadLegacy(8, [&](const EncodedRow::Byte*, std::size_t) { return bad; }); }
        catch (const std::invalid_argument&) { rejected = true; }
        Check(rejected, "invalid legacy adapter output rejected rather than looping or indexing outside row");
    }
    rejected = false;
    try { styled.ReadLegacy(8, [](const EncodedRow::Byte*, std::size_t) { return EncodedRow::LegacyGlyph{U'A', 2, 2}; }); }
    catch (const std::invalid_argument&) { rejected = true; }
    Check(rejected, "legacy adapter cannot consume beyond final source byte");

    std::uint32_t random = 0x8129bc45;
    for (int run = 0; run < 1000; ++run)
    {
        std::vector<EncodedRow::Byte> source;
        const auto count = static_cast<std::size_t>(run % 64);
        for (std::size_t byte = 0; byte < count; ++byte)
        {
            random = random * 1664525u + 1013904223u;
            source.push_back({static_cast<unsigned char>(random >> 16),
                {static_cast<std::uint8_t>(random >> 24), random & 255u}});
        }
        EncodedRow fuzz(source);
        const auto width = run % 16 + 1;
        const auto a = fuzz.ReadUtf8(width);
        const auto b = fuzz.ReadLegacy(width, TestLegacyGlyph);
        CheckProjectionInvariant(a, count); CheckProjectionInvariant(b, count);
        const auto again = fuzz.ReadUtf8(width);
        Check(again.Text(0, width) == a.Text(0, width) && fuzz.Bytes().size() == count,
            "repeated encoding reads are deterministic and do not mutate source");
        for (std::size_t byte = 0; byte < count; ++byte)
            Check(fuzz.Bytes()[byte].value == source[byte].value &&
                fuzz.Bytes()[byte].style.attribute == source[byte].style.attribute &&
                fuzz.Bytes()[byte].style.link == source[byte].style.link, "all raw bytes and attributes survive mixed projections");
    }
}
int main()
{
    TerminalCells::Screen screen(8, 2);
    screen.WriteUtf8(u8"A中B");
    Check(screen.Text(0, 0, 4) == U"A中B", "UTF8 writes complete characters into four cells");
    Check(screen.Cursor().column == 4, "cursor advances four cells, not five UTF8 bytes");
    TerminalCells::Screen edge(2, 2);
    edge.SetAutoWrap(false);
    edge.WriteUtf8(u8"AB\u0301");
    Check(edge.At(0, 1).text == U"B\u0301", "zero-width mark attaches to last glyph even with wrap disabled at margin");
    CheckDecoderAndWidths();
    CheckStreaming();
    CheckGlyphsAndMargins();
    CheckRowsAndBounds();
    CheckOperationSequences();
    CheckDecoderSourceLengths();
    CheckEncodedRows();
    std::printf("PASS: %u terminal cell model checks\n", checks);
}
