#pragma once
#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include "PttWidthProfile.h"

// Text-only stage-one model. ANSI/Telnet parsing belongs to the caller.
// No Windows, transport, clipboard, codepage globals or CPR side effects.
namespace TerminalCells
{
    class Utf8Decoder
    {
        std::uint32_t value_ = 0;
        unsigned int remaining_ = 0;
        unsigned int length_ = 0;
        unsigned char minimum_ = 0x80, maximum_ = 0xbf;
    public:
        bool Pending() const { return remaining_ != 0; }
        void Reset() { value_ = remaining_ = length_ = 0; minimum_ = 0x80; maximum_ = 0xbf; }
        // The length belongs to the emitted maximal subpart, including invalid
        // prefixes. This lets stored rows retain original bytes without decoding
        // errors being irreversibly replaced by the UTF-8 encoding of U+FFFD.
        template<class Emit> void FeedWithLength(unsigned char byte, Emit emit)
        {
            if (remaining_)
            {
                if (byte < minimum_ || byte > maximum_)
                {
                    const auto length = length_;
                    Reset(); emit(U'\ufffd', length);
                    FeedWithLength(byte, emit); // Do not lose the offending ASCII/new lead byte.
                    return;
                }
                value_ = (value_ << 6) | (byte & 0x3f);
                ++length_;
                minimum_ = 0x80; maximum_ = 0xbf;
                if (--remaining_ == 0)
                {
                    const char32_t scalar = static_cast<char32_t>(value_);
                    const auto length = length_;
                    Reset(); emit(scalar, length);
                }
                return;
            }
            if (byte < 0x80) { emit(static_cast<char32_t>(byte), 1u); return; }
            if (byte >= 0xc2 && byte <= 0xdf) { value_ = byte & 0x1f; remaining_ = 1; }
            else if (byte >= 0xe0 && byte <= 0xef)
            {
                value_ = byte & 0x0f; remaining_ = 2;
                if (byte == 0xe0) minimum_ = 0xa0;
                if (byte == 0xed) maximum_ = 0x9f;
            }
            else if (byte >= 0xf0 && byte <= 0xf4)
            {
                value_ = byte & 0x07; remaining_ = 3;
                if (byte == 0xf0) minimum_ = 0x90;
                if (byte == 0xf4) maximum_ = 0x8f;
            }
            else { emit(U'\ufffd', 1u); return; }
            length_ = 1;
        }
        template<class Emit> void FinishWithLength(Emit emit)
        {
            const auto length = length_; Reset();
            if (length) emit(U'\ufffd', length);
        }
        template<class Emit> void Feed(unsigned char byte, Emit emit)
        {
            FeedWithLength(byte, [&](char32_t scalar, unsigned int) { emit(scalar); });
        }
        template<class Emit> void Finish(Emit emit)
        {
            FinishWithLength([&](char32_t scalar, unsigned int) { emit(scalar); });
        }
    };

    struct Position { int row = 0, column = 0; bool wrapPending = false; };
    struct Style
    {
        std::uint8_t attribute = 7;
        std::uint32_t link = 0; // Opaque target ID, not a URL owned by this module.
    };
    struct Cell
    {
        std::u32string text = U" ";
        Style style;
        unsigned int width = 1; // Lead: 1/2; continuation: 0, empty text.
        bool continuation = false;
    };
    struct Snapshot
    {
        std::vector<std::vector<Cell>> rows;
        Position cursor;
    };

    class Screen
    {
        int columns_, rows_, top_ = 0, bottom_;
        std::vector<std::vector<Cell>> grid_;
        Position cursor_;
        bool autoWrap_ = true;
        Utf8Decoder decoder_;
        Style decodeStyle_;
        int markRow_ = -1, markColumn_ = -1;

        void EndAttachment() { markRow_ = markColumn_ = -1; }

        static void CheckDimensions(int columns, int rows)
        {
            if (columns < 1 || columns > 160 || rows < 1 || rows > 64)
                throw std::invalid_argument("dimensions must be 1..160 by 1..64");
        }
        void ClearGlyph(int row, int column)
        {
            auto& line = grid_[row];
            if (line[column].continuation && column > 0) --column;
            const bool wide = line[column].width == 2;
            line[column] = Cell{};
            if (wide && column + 1 < columns_) line[column + 1] = Cell{};
        }
        void AdvanceRow()
        {
            if (cursor_.row == bottom_)
            {
                std::rotate(grid_.begin() + top_, grid_.begin() + top_ + 1,
                    grid_.begin() + bottom_ + 1);
                std::fill(grid_[bottom_].begin(), grid_[bottom_].end(), Cell{});
            }
            else if (cursor_.row + 1 < rows_) ++cursor_.row;
        }
        void Put(char32_t scalar, Style style)
        {
            if (scalar > 0x10ffff || (scalar >= 0xd800 && scalar <= 0xdfff))
                scalar = U'\ufffd';
            const int width = Width(scalar);
            if (!width)
            {
                if (scalar < 0xa0) return; // Never store C0/C1 controls.
                if (markRow_ < 0) return; // No orphan/cross-control attachment.
                auto& text = grid_[markRow_][markColumn_].text;
                if (text.size() < 33) text += scalar; // Bound hostile zero-width runs.
                return;
            }
            if (width > columns_) return; // A one-column viewport cannot fit a wide glyph.
            if (cursor_.wrapPending)
            {
                cursor_.wrapPending = false;
                if (autoWrap_) { cursor_.column = 0; AdvanceRow(); }
            }
            if (width == 2 && cursor_.column == columns_ - 1)
            {
                // Atomic wide glyph: blank the edge and wrap, or omit if disabled.
                if (!autoWrap_ || columns_ < 2) return;
                ClearGlyph(cursor_.row, cursor_.column);
                cursor_.column = 0; AdvanceRow();
            }
            const int column = cursor_.column;
            ClearGlyph(cursor_.row, column);
            if (width == 2) ClearGlyph(cursor_.row, column + 1);
            auto& lead = grid_[cursor_.row][column];
            lead.text.assign(1, scalar); lead.style = style;
            lead.width = static_cast<unsigned int>(width);
            markRow_ = cursor_.row; markColumn_ = column;
            if (width == 2)
            {
                auto& trail = grid_[cursor_.row][column + 1];
                trail.text.clear(); trail.style = style; trail.width = 0;
                trail.continuation = true;
            }
            if (column + width == columns_)
            {
                cursor_.column = columns_ - 1;
                cursor_.wrapPending = autoWrap_;
            }
            else cursor_.column += width;
        }
    public:
        Screen(int columns, int rows) : columns_(columns), rows_(rows), bottom_(0)
        {
            CheckDimensions(columns, rows);
            bottom_ = rows - 1;
            grid_.assign(rows, std::vector<Cell>(columns));
        }
        int Columns() const { return columns_; }
        int Rows() const { return rows_; }
        Position Cursor() const { return cursor_; } // Pure query; preserves pending wrap.
        bool PendingUtf8() const { return decoder_.Pending(); }
        const Cell& At(int row, int column) const { return grid_.at(row).at(column); }
        Snapshot Capture() const { return { grid_, cursor_ }; }
        void WriteUtf8(std::string_view bytes, Style style = {})
        {
            for (unsigned char byte : bytes)
            {
                if (!decoder_.Pending()) decodeStyle_ = style;
                decoder_.Feed(byte, [&](char32_t scalar) {
                    Put(scalar, decodeStyle_);
                    decodeStyle_ = style; // Reprocessed input uses the new byte's style.
                });
            }
        }
        // Only at EOF/real text-control or encoding boundaries, NOT packet ends.
        void FinishText() { decoder_.Finish([&](char32_t scalar) { Put(scalar, decodeStyle_); }); }
        void WriteScalar(char32_t scalar, Style style = {}) { FinishText(); Put(scalar, style); }
        bool MoveTo(int row, int column)
        {
            if (row < 0 || row >= rows_ || column < 0 || column >= columns_) return false;
            FinishText(); cursor_ = { row, column, false }; EndAttachment(); return true;
        }
        void CarriageReturn() { FinishText(); cursor_.column = 0; cursor_.wrapPending = false; EndAttachment(); }
        void LineFeed() { FinishText(); cursor_.wrapPending = false; AdvanceRow(); EndAttachment(); }
        void SetAutoWrap(bool enabled) { FinishText(); autoWrap_ = enabled; cursor_.wrapPending = false; EndAttachment(); }
        bool SetScrollRegion(int top, int bottom)
        {
            if (top < 0 || bottom >= rows_ || top >= bottom) return false;
            FinishText(); top_ = top; bottom_ = bottom; cursor_ = {}; EndAttachment(); return true;
        }
        void SetStyle(int row, int column, Style style) { grid_.at(row).at(column).style = style; }
        void Erase(int row, int begin, int end)
        {
            if (row < 0 || row >= rows_ || begin < 0 || end > columns_ || begin > end)
                throw std::out_of_range("invalid erase range");
            FinishText();
            for (int column = begin; column < end; ++column) ClearGlyph(row, column);
            EndAttachment();
        }
        std::u32string Text(int row, int begin, int end) const
        {
            if (row < 0 || row >= rows_ || begin < 0 || end > columns_ || begin > end)
                throw std::out_of_range("invalid text range");
            std::u32string text;
            for (int column = begin; column < end; ++column)
            {
                const auto& cell = At(row, column);
                // Omit a cut glyph rather than returning an unselected half.
                if (!cell.continuation && column + static_cast<int>(cell.width) <= end)
                    text += cell.text;
            }
            return text;
        }
        void Resize(int columns, int rows)
        {
            CheckDimensions(columns, rows);
            std::vector<std::vector<Cell>> next(rows, std::vector<Cell>(columns));
            for (int row = 0; row < (std::min)(rows, rows_); ++row)
                for (int column = 0; column < (std::min)(columns, columns_); ++column)
                {
                    const auto& cell = grid_[row][column];
                    if (cell.continuation || column + static_cast<int>(cell.width) > columns) continue;
                    next[row][column] = cell;
                    if (cell.width == 2) next[row][column + 1] = grid_[row][column + 1];
                }
            grid_.swap(next); columns_ = columns; rows_ = rows;
            top_ = 0; bottom_ = rows - 1;
            cursor_.row = (std::min)(cursor_.row, rows - 1);
            cursor_.column = (std::min)(cursor_.column, columns - 1);
            cursor_.wrapPending = false;
            EndAttachment();
        }
        void Reset()
        {
            for (auto& row : grid_) std::fill(row.begin(), row.end(), Cell{});
            cursor_ = {}; top_ = 0; bottom_ = rows_ - 1; autoWrap_ = true;
            decoder_.Reset(); decodeStyle_ = {};
            EndAttachment();
        }
    };
}
