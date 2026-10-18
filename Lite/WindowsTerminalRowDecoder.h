#pragma once
#include "Ucs2Conv.h"
#include "EncodedTerminalRow.h"

namespace TerminalCells
{
    // Legacy-codepage adapter for EncodedRow. The caller owns the already
    // initialized CUcs2Conv (the same UAO table used by PCMan's display/copy).
    // It never loads tables, changes the global charset, or owns a cursor.
    class WindowsRowDecoder
    {
        UINT codePage_;
        CUcs2Conv& big5_;
    public:
        UINT CodePage() const { return codePage_; }
        bool IsLead(unsigned char byte) const
        {
            return codePage_ == 950 ? byte >= 0x81 && byte <= 0xfe
                : ::IsDBCSLeadByteEx(codePage_, byte) != FALSE;
        }
        WindowsRowDecoder(UINT codePage, CUcs2Conv& big5)
            : codePage_(codePage == CP_ACP ? ::GetACP() : codePage), big5_(big5)
        {
            CPINFO info = {};
            if (codePage_ == CP_UTF8 || !::GetCPInfo(codePage_, &info) || info.MaxCharSize > 2)
                throw std::invalid_argument("use UTF8 projection or a supported legacy codepage");
        }
        EncodedRow::LegacyGlyph operator()(const EncodedRow::Byte* source, std::size_t available) const
        {
            if (!available || !source) throw std::invalid_argument("empty legacy glyph input");
            const auto first = source[0].value;
            unsigned int length = 1;
            if (available >= 2)
            {
                const auto second = source[1].value;
                // Match CUcs2Conv's existing UAO/Big5 pair classification. Do
                // not discard a complete pair merely because mapping fails:
                // C4 A1 must still occupy two cells for PTT's encoding probe.
                const bool pair = codePage_ == 950
                    ? first >= 0x81 && first <= 0xfe && second >= 0x40 && second <= 0xfe
                    : IsLead(first);
                if (pair) length = 2;
            }
            // The existing UAO converter reads a lookahead even at len==1.
            // Always give it a terminated, padded buffer, never a row pointer.
            const char bytes[3] = {static_cast<char>(first),
                length == 2 ? static_cast<char>(source[1].value) : '\0', '\0'};
            wchar_t text[3] = {};
            const auto count = codePage_ == 950
                ? static_cast<int>(big5_.Big52Ucs2(bytes, text, static_cast<int>(length)))
                : ::MultiByteToWideChar(codePage_, MB_ERR_INVALID_CHARS, bytes,
                    static_cast<int>(length), text, 2);
            char32_t scalar = U'\ufffd';
            if (count == 1 && !(text[0] >= 0xd800 && text[0] <= 0xdfff))
                scalar = static_cast<char32_t>(text[0]);
            // Legacy screen columns are byte cells, including an unmapped pair.
            return {scalar, length, length};
        }
    };
}
