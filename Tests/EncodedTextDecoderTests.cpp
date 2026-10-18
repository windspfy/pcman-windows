#include "../Lite/EncodedTextDecoder.h"
#include "../Lite/AnsiSequenceParser.h"
#include "../Lite/Sgr66.h"
#include <cstdio>
#include <cstdlib>
#include <memory>

using namespace TerminalCells;
static unsigned int checks = 0;
static void Check(bool ok, const char* why)
{
    ++checks;
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", why); std::exit(1); }
}
struct Legacy
{
    unsigned int cp = 950;
    std::shared_ptr<bool> fail;
    unsigned int CodePage() const { return cp; }
    bool IsLead(unsigned char byte) const { return byte >= 0x81 && byte <= 0xfe; }
    EncodedRow::LegacyGlyph operator()(const EncodedRow::Byte* bytes, std::size_t size) const
    {
        if (fail && *fail) throw std::runtime_error("test adapter failure");
        if (size >= 2 && IsLead(bytes[0].value) && bytes[1].value >= 0x40 && bytes[1].value <= 0xfe)
            return {bytes[0].value == 0xa4 && bytes[1].value == 0xa4 ? U'中' : U'\ufffd', 2, 2};
        return {bytes[0].value >= 0x81 ? U'?' : static_cast<char32_t>(bytes[0].value), 1, 1};
    }
};
static void Append(std::vector<EncodedTextDecoder::Glyph>& out, const EncodedTextDecoder::Batch& batch)
{
    for (const auto& glyph : batch)
    {
        Check(!glyph.source.Bytes().empty() && glyph.source.Bytes().size() <= 4 &&
            glyph.width >= 0 && glyph.width <= 2, "emitted source glyph is bounded and complete");
        out.push_back(glyph);
    }
}
static std::string Raw(const std::vector<EncodedTextDecoder::Glyph>& glyphs)
{
    std::string result;
    for (const auto& glyph : glyphs)
        for (const auto& byte : glyph.source.Bytes()) result += static_cast<char>(byte.value);
    return result;
}
static EncodedRow Source(const std::vector<EncodedTextDecoder::Glyph>& glyphs)
{
    std::vector<EncodedRow::Byte> bytes;
    for (const auto& glyph : glyphs)
        bytes.insert(bytes.end(), glyph.source.Bytes().begin(), glyph.source.Bytes().end());
    return EncodedRow(std::move(bytes));
}
static std::u32string Scalars(const std::vector<EncodedTextDecoder::Glyph>& glyphs)
{
    std::u32string result;
    for (const auto& glyph : glyphs) result += glyph.scalar;
    return result;
}
static void Utf8Cases()
{
    struct Golden { std::string input; std::u32string text; };
    const std::vector<Golden> golden = {{"", U""}, {u8"A中Bġ😀e\u0301", U"A中Bġ😀e\u0301"},
        {"\xe2\x82" "A", U"\ufffdA"}, {"\xe2\xc4\xa1", U"\ufffd\u0121"},
        {"\xed\xa0\x80", U"\ufffd\ufffd\ufffd"}, {"\xf4\x90\x80\x80", U"\ufffd\ufffd\ufffd\ufffd"},
        {"\xc0\xaf", U"\ufffd\ufffd"}, {"\xff\xfe", U"\ufffd\ufffd"},
        {"\xe4", U"\ufffd"}, {"\xf0\x9f\x98", U"\ufffd"}, {"\xc4\xa1", U"\u0121"}};
    for (const auto& test : golden)
        for (std::size_t split = 0; split <= test.input.size(); ++split)
        {
            EncodedTextDecoder decoder;
            std::vector<EncodedTextDecoder::Glyph> out;
            for (std::size_t i = 0; i < split; ++i) Append(out, decoder.Feed(static_cast<unsigned char>(test.input[i]), {9, 11}));
            // A packet boundary has deliberately no decoder operation.
            const auto before = out.size();
            Check(decoder.UseUtf8().count == 0 && out.size() == before, "reselecting current encoding cannot flush a partial packet");
            for (std::size_t i = split; i < test.input.size(); ++i) Append(out, decoder.Feed(static_cast<unsigned char>(test.input[i]), {12, 22}));
            Append(out, decoder.FinishText());
            Check(Scalars(out) == test.text && Raw(out) == test.input && !decoder.Pending(), "all packet cuts preserve scalars and exact malformed source");
            Check(decoder.FinishText().count == 0, "repeated real text finish is idempotent");
            for (const auto& glyph : out) Check(glyph.width == Width(glyph.scalar), "UTF8 event width follows the shared PTT policy");
        }
    EncodedTextDecoder decoder;
    Check(decoder.Feed(0xe4, {9, 1}).count == 0 && decoder.Pending(), "UTF8 lead is retained rather than written as a byte cell");
    Check(decoder.Feed(0xb8, {10, 2}).count == 0, "UTF8 middle byte is still pending");
    const auto chinese = decoder.Feed(0xad, {12, 3});
    Check(chinese.count == 1 && chinese.glyphs[0].scalar == U'中' && chinese.glyphs[0].width == 2 &&
        chinese.glyphs[0].source.Bytes()[0].style.attribute == 9 &&
        chinese.glyphs[0].source.Bytes()[2].style.link == 3, "complete glyph retains every byte's style but advances only two cells");
    const auto read = chinese.glyphs[0].source.ReadUtf8(4);
    Check(read.At(0).style.attribute == 9 && read.At(1).style.attribute == 9 && read.At(1).style.link == 1,
        "UTF8 cell projection latches first-byte style without destroying later source styles");
    decoder.Feed(0xe2, {9,1}); decoder.Feed(0x82, {10,2});
    const auto recovered = decoder.Feed('A', {12,3});
    Check(recovered.count == 2 && recovered.glyphs[0].source.Bytes().size() == 2 &&
        recovered.glyphs[0].source.Bytes()[0].style.link == 1 && recovered.glyphs[1].scalar == U'A' &&
        recovered.glyphs[1].source.Bytes()[0].style.link == 3, "malformed prefix and reprocessed ASCII retain distinct source/style ownership");
    decoder.Feed(0xf0); decoder.Reset();
    const auto next = decoder.Feed('X');
    Check(next.count == 1 && next.glyphs[0].scalar == U'X' && next.glyphs[0].source.Bytes()[0].boundaryBefore &&
        !decoder.Pending(), "reset drops old-session prefixes and starts a real new run");
}
static void LegacyCases()
{
    EncodedTextDecoder decoder;
    Check(decoder.UseLegacy(Legacy{}).count == 0 && decoder.CodePage() == 950, "explicit legacy adapter selected");
    Check(decoder.Feed(0xa4, {9,11}).count == 0 && decoder.Pending(), "Big5 lead survives packet/SGR boundaries");
    Check(decoder.UseLegacy(Legacy{}).count == 0 && decoder.Pending(), "same legacy codepage selection preserves pending lead");
    const auto chinese = decoder.Feed(0xa4, {12,22});
    const auto cells = chinese.glyphs[0].source.ReadLegacy(4, Legacy{});
    Check(chinese.count == 1 && chinese.glyphs[0].scalar == U'中' && chinese.glyphs[0].width == 2 &&
        cells.At(0).style.attribute == 9 && cells.At(1).style.attribute == 12 && cells.At(1).style.link == 22,
        "Big5 event and projection preserve independent half attributes");
    decoder.Feed(0xc4);
    const auto probe = decoder.Feed(0xa1);
    Check(probe.count == 1 && probe.glyphs[0].width == 2, "unmapped PTT encoding probe still takes two legacy cells");
    decoder.Feed(0xa4);
    const auto invalid = decoder.Feed(' ');
    Check(invalid.count == 2 && invalid.glyphs[0].source.Bytes().size() == 1 && invalid.glyphs[1].scalar == U' ',
        "non-pair emits dangling legacy byte then reprocesses the next byte");
    decoder.Feed(0xa4);
    const auto leadAgain = decoder.Feed(0xff);
    Check(leadAgain.count == 2 && !decoder.Pending(), "unpaired high byte is not lost or held indefinitely");
    decoder.Feed(0xa4, {10,25});
    const auto eof = decoder.FinishText();
    Check(eof.count == 1 && eof.glyphs[0].width == 1 && eof.glyphs[0].source.Bytes()[0].value == 0xa4,
        "legacy EOF keeps original dangling byte instead of transcoding question mark back into source");
    decoder.Feed(0xa4); decoder.Reset();
    Check(decoder.CodePage() == 950 && !decoder.Pending() && decoder.FinishText().count == 0,
        "session reset preserves selected encoding but not a legacy prefix");
    EncodedTextDecoder isolated;
    decoder.Feed(0xa4);
    Check(!isolated.Pending() && isolated.CodePage() == 65001, "connection-local prefixes and codepages are isolated");
}
static void ReceiveSourceWrites()
{
    EncodedTextDecoder decoder;
    decoder.Feed(0xe4); decoder.Feed(0xb8);
    const auto chinese = decoder.Feed(0xad);
    const auto ascii = decoder.Feed('A');
    auto row = EncodedRow{}.WriteUtf8Glyph(8, 0, chinese.glyphs[0].source);
    row = row.WriteUtf8Glyph(8, 2, ascii.glyphs[0].source, true);
    Check(row.ReadUtf8(8).Text(0, 3) == U"中A" && !row.Bytes()[3].boundaryBefore,
        "sequential receive writes retain uninterrupted raw run across Unicode glyph boundaries");
    Check(row.ReadLegacy(8, Legacy{}).At(3).continuation,
        "UTF8 history reinterpreted as legacy can pair original bytes across old Unicode scalar boundary");
    const auto edited = row.WriteUtf8Glyph(8, 2, ascii.glyphs[0].source);
    Check(edited.Bytes()[3].boundaryBefore && !edited.ReadLegacy(8, Legacy{}).At(3).continuation,
        "real overwrite still introduces an edit seam and cannot falsely pair original bytes");
    decoder.FinishText();
    const auto afterControl = decoder.Feed('A');
    const auto separated = row.WriteUtf8Glyph(8, 2, afterControl.glyphs[0].source, true);
    Check(separated.Bytes()[3].boundaryBefore, "continued-run hint cannot override a real decoder text/control boundary");
    decoder.UseLegacy(Legacy{}); decoder.Feed(0xa4, {9,11});
    const auto pair = decoder.Feed(0xa4, {12,22});
    const auto legacy = EncodedRow{}.WriteLegacyGlyph(8, 0, pair.glyphs[0].source, Legacy{});
    Check(legacy.ReadLegacy(8, Legacy{}).Text(0,2) == U"中" && legacy.ReadLegacy(8, Legacy{}).At(1).style.link == 22,
        "legacy decoded source glyph writes through shared row update with both half styles");
    bool rejected = false;
    try { row.WriteUtf8Glyph(8, 3, EncodedRow("AB")); } catch (const std::invalid_argument&) { rejected = true; }
    Check(rejected, "stream glyph write rejects multiple scalar glyphs");
    rejected = false;
    try { row.WriteUtf8Glyph(8, 7, chinese.glyphs[0].source); } catch (const std::invalid_argument&) { rejected = true; }
    Check(rejected, "stream glyph writer leaves wrap decision to the unique caller cursor");
    rejected = false;
    try { row.WriteUtf8Glyph(8, 2, EncodedRow(u8"\u0301")); } catch (const std::invalid_argument&) { rejected = true; }
    Check(rejected, "zero-width stream output uses attachment not a non-advancing replacement");
}
static void EncodingChangesAndFailures()
{
    EncodedTextDecoder decoder;
    decoder.Feed(0xe4, {9, 21});
    const auto oldUtf8 = decoder.UseLegacy(Legacy{});
    Check(oldUtf8.count == 1 && oldUtf8.glyphs[0].codePage == 65001 && oldUtf8.glyphs[0].width == 2 &&
        oldUtf8.glyphs[0].source.Bytes()[0].value == 0xe4 && decoder.CodePage() == 950,
        "encoding change returns unfinished old UTF8 in its old encoding before future legacy input");
    decoder.Feed(0xa4, {12,22});
    const auto oldBig5 = decoder.UseUtf8();
    Check(oldBig5.count == 1 && oldBig5.glyphs[0].codePage == 950 && oldBig5.glyphs[0].width == 1 &&
        oldBig5.glyphs[0].source.Bytes()[0].value == 0xa4 && decoder.CodePage() == 65001,
        "encoding change does not decode a pending Big5 lead using the new UTF8 policy");
    const auto ascii = decoder.Feed('A');
    Check(ascii.glyphs[0].source.Bytes()[0].boundaryBefore, "new encoding begins a separate source run");
    decoder.Feed(0xc4);
    bool rejected = false;
    try { decoder.UseLegacy(Legacy{0,{}}); } catch (const std::invalid_argument&) { rejected = true; }
    Check(rejected && decoder.CodePage() == 65001 && decoder.Pending(), "invalid adapter token cannot consume old pending UTF8");
    Check(decoder.Feed(0xa1).glyphs[0].scalar == U'\u0121', "old UTF8 prefix still completes after rejected encoding change");
    auto failure = std::make_shared<bool>(false);
    decoder.UseLegacy(Legacy{950,failure}); decoder.Feed(0xa4, {9,11});
    *failure = true;
    rejected = false;
    try { decoder.Feed(0xa4, {12,22}); } catch (const std::runtime_error&) { rejected = true; }
    Check(rejected && decoder.Pending(), "adapter failure rolls back pending bytes rather than discarding the old prefix");
    rejected = false;
    try { decoder.UseUtf8(); } catch (const std::runtime_error&) { rejected = true; }
    Check(rejected && decoder.CodePage() == 950 && decoder.Pending(), "failed old-prefix finish cannot silently switch encoding");
    *failure = false;
    const auto retried = decoder.Feed(0xa4, {12,22});
    Check(retried.count == 1 && retried.glyphs[0].scalar == U'中' && retried.glyphs[0].source.Bytes()[0].style.link == 11 &&
        retried.glyphs[0].source.Bytes()[1].style.link == 22, "retry of rejected byte preserves exactly one original pair and both styles");
    struct Broken : Legacy
    {
        EncodedRow::LegacyGlyph operator()(const EncodedRow::Byte*, std::size_t) const { return {U'A',0,1}; }
    };
    EncodedTextDecoder broken;
    broken.UseLegacy(Broken{}); broken.Feed(0xa4);
    rejected = false;
    try { broken.Feed(0xa4); } catch (const std::invalid_argument&) { rejected = true; }
    Check(rejected && broken.Pending(), "zero-length adapter result cannot loop or consume pending source");
    struct FailingLead : Legacy
    {
        std::shared_ptr<bool> leadFailure;
        bool IsLead(unsigned char byte) const
        {
            if (leadFailure && *leadFailure) throw std::runtime_error("test lead failure");
            return Legacy::IsLead(byte);
        }
    };
    auto leadFailure = std::make_shared<bool>(false);
    FailingLead lead; lead.leadFailure = leadFailure;
    EncodedTextDecoder lateFailure; lateFailure.UseLegacy(lead); lateFailure.Feed(0xa4);
    *leadFailure = true;
    rejected = false;
    try { lateFailure.Feed(' '); } catch (const std::runtime_error&) { rejected = true; }
    Check(rejected && lateFailure.Pending(), "failure after building first output rolls back the whole input-byte transaction");
    *leadFailure = false;
    const auto recoveredSpace = lateFailure.Feed(' ');
    Check(recoveredSpace.count == 2 && recoveredSpace.glyphs[0].source.Bytes()[0].value == 0xa4 &&
        recoveredSpace.glyphs[1].scalar == U' ', "retry cannot duplicate the discarded provisional output");
}
static void BytePairCoverage()
{
    // Differential scalar check against the shared strict decoder, plus
    // independent exact byte/style partition checks for every two-byte input.
    for (unsigned int first = 0; first < 256; ++first)
        for (unsigned int second = 0; second < 256; ++second)
        {
            EncodedTextDecoder decoder;
            std::vector<EncodedTextDecoder::Glyph> out;
            Append(out, decoder.Feed(static_cast<unsigned char>(first), {9,11}));
            Append(out, decoder.Feed(static_cast<unsigned char>(second), {12,22}));
            Append(out, decoder.FinishText());
            const char bytes[2] = {static_cast<char>(first), static_cast<char>(second)};
            Check(Raw(out) == std::string(bytes,2), "all two-byte inputs preserve original payload including NUL and invalid UTF8");
            Utf8Decoder expected;
            std::u32string text;
            expected.Feed(static_cast<unsigned char>(first), [&](char32_t scalar) { text += scalar; });
            expected.Feed(static_cast<unsigned char>(second), [&](char32_t scalar) { text += scalar; });
            expected.Finish([&](char32_t scalar) { text += scalar; });
            Check(Scalars(out) == text, "stream adapter does not change underlying strict decoder recovery");
            const auto source = Source(out);
            Check(source.Bytes().size() == 2 && source.Bytes()[0].style.link == 11 &&
                source.Bytes()[1].style.link == 22 && source.Bytes()[0].boundaryBefore && !source.Bytes()[1].boundaryBefore,
                "every byte pair retains its styles and uninterrupted-run boundary");
        }
}

// Framing fixture uses the actual shared parser and SGR66 helper. It models
// only the stated test controls, not CTelnetConn or a second terminal cursor.
struct Framed
{
    AnsiSequenceParser parser;
    EncodedTextDecoder decoder;
    Sgr66::PendingAttribute half;
    unsigned char attr = 7;
    unsigned int queries = 0;
    std::vector<EncodedTextDecoder::Glyph> out;
    void Feed(std::string_view input)
    {
        for (unsigned char byte : input)
        {
            const auto parsed = parser.Feed(byte);
            if (parsed == AnsiSequenceParser::Text)
            {
                if (byte >= ' ') Append(out, decoder.Feed(byte, {half.Take(attr),0}));
                else { Append(out, decoder.FinishText()); half.Reset(); }
            }
            else if (parsed == AnsiSequenceParser::Dispatch)
            {
                const std::string sequence(parser.Sequence());
                if (sequence == "[31m") attr = 1;
                else if (sequence == "[66;32m") { half.Capture(attr); attr = 2; }
                else if (sequence == "[6n") ++queries; // No outgoing CPR in this fixture.
                else { Append(out, decoder.FinishText()); half.Reset(); }
            }
            else if (parsed == AnsiSequenceParser::AutoWrapOn || parsed == AnsiSequenceParser::AutoWrapOff)
                Append(out, decoder.FinishText());
            // OSC, sync barriers, and ignored controls do not become text.
        }
    }
};
static void FramingCases()
{
    for (char kind : std::string("PX^_"))
    {
        const std::string opaque = std::string("\x1b") + kind +
            "\x1b[6n\x1b[31m\x1b[66;32m\x1b[?7l\x1b[?2026h\r\n\a\x1b\\";
        for (const bool utf8 : {false, true})
        {
            const std::string text = utf8 ? std::string("\xe4") + opaque + "\xb8\xad" :
                std::string("\xa4") + opaque + "\xa4";
            const std::string input = "\x1b[31m\x1b[66;32m" + text + "\x1b[6n";
            for (std::size_t cut = 0; cut <= input.size(); ++cut)
            {
                Framed receiver;
                if (!utf8) receiver.decoder.UseLegacy(Legacy{});
                receiver.Feed(std::string_view(input).substr(0, cut));
                receiver.Feed(std::string_view(input).substr(cut));
                Check(receiver.out.size() == 1 && receiver.out[0].scalar == U'中' && receiver.queries == 1 &&
                    !receiver.decoder.Pending() && Raw(receiver.out) == (utf8 ? u8"中" : "\xa4\xa4"),
                    "opaque framing preserves split UTF8/Big5 text without injecting queries or payload");
                Check(receiver.out[0].source.Bytes()[0].style.attribute == 1 &&
                    receiver.out[0].source.Bytes()[1].style.attribute == 2,
                    "opaque payload cannot consume or replace the pending SGR66 style");
            }
        }
    }
    const std::string input = "\x1b[31m\x1b[66;32m" + std::string(u8"A中B😀") +
        "\x1b]8;;https://example.com\x1b\\" + std::string(u8"ġ") + "\x1b]8;;\x1b\\\x1b[6n";
    for (std::size_t cut = 0; cut <= input.size(); ++cut)
    {
        Framed receiver;
        receiver.Feed(std::string_view(input).substr(0, cut));
        receiver.Feed(std::string_view(input).substr(cut));
        Append(receiver.out, receiver.decoder.FinishText());
        Check(Scalars(receiver.out) == U"A中B😀ġ" && Raw(receiver.out) == u8"A中B😀ġ" && receiver.queries == 1,
            "actual parser plus decoder keeps all packet cuts and strips hidden OSC/control bytes");
    }
    Framed styled;
    styled.Feed("\x1b[31m\xe4");
    styled.Feed("\x1b[66;32m\xb8\xad");
    Check(styled.out.size() == 1 && styled.out[0].source.ReadUtf8(4).At(0).style.attribute == 1 &&
        styled.out[0].source.Bytes()[1].style.attribute == 1 && styled.out[0].source.Bytes()[2].style.attribute == 2,
        "UTF8 prefix spanning SGR retains per-byte colors and first-byte presentation policy");
    Framed legacy; legacy.decoder.UseLegacy(Legacy{});
    legacy.Feed("\x1b[31m\x1b[66;32m\xa4"); legacy.Feed("\xa4");
    Check(legacy.out.size() == 1 && legacy.out[0].source.ReadLegacy(4, Legacy{}).At(0).style.attribute == 1 &&
        legacy.out[0].source.ReadLegacy(4, Legacy{}).At(1).style.attribute == 2, "SGR66 left/right colors survive split Big5 reception");
    Framed control;
    control.Feed("\xe4\r\xb8\xad");
    Check(Scalars(control.out) == U"\ufffd\ufffd\ufffd" && Raw(control.out) == u8"中" &&
        Source(control.out).ReadUtf8(8).Text(0, 6) == U"\ufffd\ufffd\ufffd",
        "real text/control finish preserves boundary so adjacent bytes cannot falsely recombine");
    Framed query;
    query.Feed("\xc4\x1b[6n");
    Check(query.out.empty() && query.decoder.Pending(), "pure query leaves undecoded prefix untouched");
    query.Feed("\xa1");
    Check(Scalars(query.out) == U"\u0121" && query.out[0].width == 1 && query.queries == 1,
        "PTT probe completes in one UTF8 cell after non-mutating query");
    Framed hidden;
    hidden.Feed("A\x1b]0;\xc4\xa1\x1b[6n\x07" "B");
    Check(Scalars(hidden.out) == U"AB" && hidden.queries == 0, "OSC payload cannot enter text decoder or execute embedded query");
    Framed cancelled;
    cancelled.Feed("\x1b]0;ignored\x18" "A");
    Check(Scalars(cancelled.out) == U"A", "cancelled OSC does not leak payload to decoder");
    Framed sync;
    sync.Feed("\xe4\x1b[?2026h\xb8\xad\x1b[?2026l");
    Check(Scalars(sync.out) == U"中", "presentation sync events do not flush a text prefix");
}
int main()
{
    Utf8Cases(); LegacyCases(); ReceiveSourceWrites(); EncodingChangesAndFailures(); FramingCases(); BytePairCoverage();
    std::printf("PASS: %u encoded text decoder checks\n", checks);
}
