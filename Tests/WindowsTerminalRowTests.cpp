#include <afxwin.h>
#include "../Lite/WindowsTerminalRowDecoder.h"
#include <cstdio>
#include <cstdlib>

using namespace TerminalCells;
static unsigned int checks = 0;
static void Check(bool ok, const char* why)
{
    ++checks;
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", why); std::exit(1); }
}
static char32_t ExpectedBig5(CUcs2Conv& converter, unsigned char first, unsigned char second, bool pair)
{
    const char bytes[3] = {static_cast<char>(first), pair ? static_cast<char>(second) : '\0', '\0'};
    wchar_t text[3] = {};
    const auto count = converter.Big52Ucs2(bytes, text, pair ? 2 : 1);
    return count == 1 && !(text[0] >= 0xd800 && text[0] <= 0xdfff)
        ? static_cast<char32_t>(text[0]) : U'\ufffd';
}
int main(int argc, char** argv)
{
    if (!AfxWinInit(::GetModuleHandle(nullptr), nullptr, ::GetCommandLine(), 0)) return 2;
    if (argc != 2)
    {
        std::fprintf(stderr, "Usage: WindowsTerminalRowTests <directory containing tracked B2U>\n");
        return 2;
    }
    CString tableDirectory(argv[1]);
    if (tableDirectory.IsEmpty() || (tableDirectory.Right(1) != "\\" && tableDirectory.Right(1) != "/"))
        tableDirectory += "\\";
    CUcs2Conv converter;
    converter.SetTablePath(tableDirectory);
    Check(converter.InitBig52Ucs2Tab() != FALSE, "load repository B2U table through the production UAO converter");
    WindowsRowDecoder big5(950, converter);
    EncodedRow chinese("A\xa4\xa4" "B", {11, 51});
    const auto view = chinese.ReadLegacy(8, big5);
    Check(view.Text(0, 4) == U"A中B" && view.At(2).continuation, "actual UAO adapter produces four-cell A Chinese B");
    Check(view.RawText(0, 4) == "A\xa4\xa4" "B" && view.At(2).style.link == 51,
        "actual legacy projection retains source bytes and links");
    EncodedRow probe("\xc4\xa1");
    Check(probe.ReadLegacy(8, big5).ColumnForByte(2) == 2 &&
        probe.ReadUtf8(8).ColumnForByte(2) == 1 &&
        probe.ReadLegacy(8, big5).RawText(0, 2) == "\xc4\xa1", "real Big5 UTF8 Big5 probe round trip keeps two-byte source and different cell widths");

    // Differential regression against the existing, real converter, including
    // the table's extension ranges. This does not independently prove UAO data.
    for (unsigned int first = 0; first < 256; ++first)
    {
        for (unsigned int second = 0; second < 256; ++second)
        {
            const EncodedRow::Byte bytes[2] = {{static_cast<unsigned char>(first), {}},
                {static_cast<unsigned char>(second), {}}};
            const bool pair = first >= 0x81 && first <= 0xfe && second >= 0x40 && second <= 0xfe;
            const auto glyph = big5(bytes, 2);
            Check(glyph.bytes == (pair ? 2u : 1u) && glyph.width == glyph.bytes, "Big5 pair classification and protocol columns match existing converter");
            Check(glyph.scalar == ExpectedBig5(converter, bytes[0].value, bytes[1].value, pair),
                "all two-byte inputs reuse actual UAO mapping without a replacement table");
        }
        const EncodedRow::Byte byte = {static_cast<unsigned char>(first), {}};
        const auto glyph = big5(&byte, 1);
        Check(glyph.bytes == 1 && glyph.width == 1 && glyph.scalar == ExpectedBig5(converter, byte.value, 0, false),
            "one-byte input needs no caller-provided lookahead, including a dangling Big5 lead");
    }
    // Fixed, independently known golden strings for the other existing menus.
    const WindowsRowDecoder gbk(936, converter), sjis(932, converter), western(1252, converter);
    Check(EncodedRow("A\xd6\xd0" "B").ReadLegacy(8, gbk).Text(0, 4) == U"A中B", "CP936 retains DBCS two-cell glyph");
    Check(EncodedRow("\x82\xa0\xb1").ReadLegacy(8, sjis).Text(0, 3) == U"あｱ", "CP932 distinguishes fullwidth DBCS and halfwidth single byte");
    Check(EncodedRow("\xe9").ReadLegacy(4, western).Text(0, 1) == U"é", "single-byte codepage does not mistake high byte for DBCS");
    if (::GetACP() == CP_UTF8)
    {
        bool rejected = false;
        try { WindowsRowDecoder system(CP_ACP, converter); } catch (const std::invalid_argument&) { rejected = true; }
        Check(rejected, "UTF8 system ACP must select the UTF8 projection instead of legacy decoding");
    }
    else
    {
        WindowsRowDecoder system(CP_ACP, converter);
        Check(EncodedRow("ABC").ReadLegacy(4, system).Text(0, 3) == U"ABC", "default codepage resolves the current Windows ACP");
    }
    bool rejected = false;
    try { WindowsRowDecoder utf8(CP_UTF8, converter); } catch (const std::invalid_argument&) { rejected = true; }
    Check(rejected, "UTF8 cannot accidentally use the legacy decoder");
    rejected = false;
    try { WindowsRowDecoder unknown(99999, converter); } catch (const std::invalid_argument&) { rejected = true; }
    Check(rejected, "invalid codepage is rejected");
    rejected = false;
    try { big5(nullptr, 0); } catch (const std::invalid_argument&) { rejected = true; }
    Check(rejected, "empty legacy input rejected without dereference");
    std::printf("PASS: %u Windows terminal row checks\n", checks);
}
