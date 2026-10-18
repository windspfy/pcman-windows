#pragma once
#include "TerminalCellModel.h"
#include <utility>

namespace TerminalCells
{
    // An immutable source row and a value projection. Reinterpreting a row never
    // transcodes its source, including malformed bytes and off-screen suffixes.
    // This is row storage, not another terminal/cursor or an ANSI stream replay.
    class EncodedRow
    {
    public:
        struct Byte
        {
            unsigned char value = ' ';
            Style style;
            // A real text/control/edit seam, NOT a packet or SGR boundary.
            // Adjacent raw prefixes must not join across it on reinterpretation.
            bool boundaryBefore = false;
        };
        struct SourceRange { std::size_t begin = 0, end = 0; };
        struct LegacyGlyph
        {
            char32_t scalar;
            unsigned int bytes; // 1 or 2, for the existing legacy codepages.
            unsigned int width; // 1 or 2 cells; not inferred from UTF-8 width.
        };
        class Projection
        {
            friend class EncodedRow;
            std::vector<Cell> cells_;
            std::vector<SourceRange> ranges_;
            std::vector<int> byteColumns_;
            std::vector<Byte> source_;
            struct Glyph { int column, width; SourceRange source; };
            std::vector<Glyph> glyphs_; // Includes clipped glyphs for correct edits.
            int extent_ = 0;
            bool clipped_ = false;

            explicit Projection(int columns, const std::vector<Byte>& source)
                : cells_(columns), ranges_(columns, {source.size(), source.size()}),
                  byteColumns_(source.size() + 1, columns), source_(source) {}
            void CheckRange(int begin, int end) const
            {
                if (begin < 0 || end < begin || end > Columns())
                    throw std::out_of_range("invalid cell range");
            }
        public:
            int Columns() const { return static_cast<int>(cells_.size()); }
            bool Clipped() const { return clipped_; }
            const Cell& At(int column) const { return cells_.at(column); }
            SourceRange Source(int column) const { return ranges_.at(column); }
            // A byte inside a wide/combining glyph maps to its lead cell. Bytes
            // outside the viewport map to Columns(), never an invalid cell.
            int ColumnForByte(std::size_t byte) const { return byteColumns_.at(byte); }
            std::u32string Text(int begin, int end) const
            {
                CheckRange(begin, end);
                std::u32string text;
                for (int column = begin; column < end; ++column)
                {
                    const auto& cell = cells_[column];
                    if (!cell.continuation && column + static_cast<int>(cell.width) <= end)
                        text += cell.text;
                }
                return text;
            }
            // Raw-byte export is intentionally different from Unicode Text().
            // Cut wide glyphs are omitted; virtual right padding exports spaces.
            // Neither function is a byte-offset substring of the visible grid.
            std::string RawText(int begin, int end) const
            {
                CheckRange(begin, end);
                std::string text;
                for (int column = begin; column < end; ++column)
                {
                    const auto& cell = cells_[column];
                    if (cell.continuation || column + static_cast<int>(cell.width) > end) continue;
                    const auto range = ranges_[column];
                    if (range.begin == range.end) text += ' ';
                    else for (auto byte = range.begin; byte < range.end; ++byte)
                        text += static_cast<char>(source_[byte].value);
                }
                return text;
            }
        };
    private:
        std::vector<Byte> bytes_;
        // 160 cells, each at most one 4-byte scalar + 32 4-byte marks. The
        // source bound is independent of the selected projection's viewport.
        // Do not eagerly allocate one of these rows per scrollback slot.
        static constexpr std::size_t MaxBytes = 160u * 33u * 4u;

        static bool Scalar(char32_t scalar)
        {
            return scalar <= 0x10ffff && !(scalar >= 0xd800 && scalar <= 0xdfff);
        }
        static void CheckColumns(int columns)
        {
            if (columns < 1 || columns > 160)
                throw std::invalid_argument("columns must be 1..160");
        }
        class Builder
        {
            Projection result_;
            int column_ = 0, markColumn_ = -1;
            std::size_t markGlyph_ = 0;
        public:
            Builder(int columns, const std::vector<Byte>& source) : result_(columns, source) {}
            void EndAttachment() { markColumn_ = -1; }
            void Put(char32_t scalar, int width, std::size_t begin, std::size_t end, bool splitStyle)
            {
                int mappedColumn = (std::min)(column_, result_.Columns());
                if (!width)
                {
                    if (scalar >= 0xa0 && markColumn_ >= 0)
                    {
                        auto& glyph = result_.glyphs_[markGlyph_];
                        mappedColumn = glyph.column + glyph.width <= result_.Columns()
                            ? markColumn_ : result_.Columns();
                        glyph.source.end = end; // Retain even excess ignored marks.
                        if (glyph.column + glyph.width <= result_.Columns())
                        {
                            auto& lead = result_.cells_[markColumn_];
                            if (lead.text.size() < 33) lead.text += scalar;
                            result_.ranges_[markColumn_] = glyph.source;
                            if (lead.width == 2) result_.ranges_[markColumn_ + 1] = glyph.source;
                        }
                    }
                    else markColumn_ = -1;
                }
                else if (column_ + width > result_.Columns())
                {
                    result_.clipped_ = true;
                    markColumn_ = column_;
                    markGlyph_ = result_.glyphs_.size();
                    result_.glyphs_.push_back({column_, width, {begin, end}});
                    column_ += width;
                    mappedColumn = result_.Columns();
                }
                else
                {
                    auto& lead = result_.cells_[column_];
                    lead.text.assign(1, scalar);
                    lead.width = static_cast<unsigned int>(width);
                    lead.style = result_.source_[begin].style;
                    result_.ranges_[column_] = {begin, end};
                    markColumn_ = column_;
                    markGlyph_ = result_.glyphs_.size();
                    result_.glyphs_.push_back({column_, width, {begin, end}});
                    if (width == 2)
                    {
                        auto& trail = result_.cells_[column_ + 1];
                        trail.text.clear(); trail.width = 0; trail.continuation = true;
                        trail.style = result_.source_[splitStyle ? end - 1 : begin].style;
                        result_.ranges_[column_ + 1] = {begin, end};
                    }
                    column_ += width;
                }
                for (auto byte = begin; byte < end; ++byte)
                    result_.byteColumns_[byte] = mappedColumn;
            }
            Projection Finish()
            {
                result_.extent_ = column_;
                result_.byteColumns_.back() = (std::min)(column_, result_.Columns());
                return std::move(result_);
            }
        };
        static void Append(std::vector<Byte>& out, const std::vector<Byte>& source,
            std::size_t begin, std::size_t end, bool boundary)
        {
            if (begin == end) return;
            if (end - begin > MaxBytes - out.size()) throw std::length_error("encoded row source limit");
            const auto at = out.size();
            out.insert(out.end(), source.begin() + begin, source.begin() + end);
            if (boundary) out[at].boundaryBefore = true;
        }
        static void Spaces(std::vector<Byte>& out, int count)
        {
            if (static_cast<std::size_t>(count) > MaxBytes - out.size())
                throw std::length_error("encoded row source limit");
            const auto at = out.size();
            out.insert(out.end(), static_cast<std::size_t>(count), Byte{});
            if (count) out[at].boundaryBefore = true;
        }
        static EncodedRow Replace(const Projection& old, int begin, int end, const Projection& replacement)
        {
            old.CheckRange(begin, end);
            if (replacement.clipped_ || replacement.extent_ > end - begin)
                throw std::invalid_argument("replacement does not fit cell range");
            if (begin == end)
            {
                if (!replacement.source_.empty()) throw std::invalid_argument("empty replacement range");
                return EncodedRow(old.source_);
            }
            // Complete erasure intentionally discards hidden/orphan source too.
            if (begin == 0 && end == old.Columns() && replacement.source_.empty()) return {};
            int left = begin, right = end;
            std::size_t first = old.source_.size(), last = first;
            bool touched = false;
            for (const auto& glyph : old.glyphs_)
            {
                if (glyph.column >= end || glyph.column + glyph.width <= begin) continue;
                if (!touched) first = glyph.source.begin;
                last = glyph.source.end; touched = true;
                left = (std::min)(left, glyph.column);
                right = (std::max)(right, (std::min)(old.Columns(), glyph.column + glyph.width));
            }
            std::vector<Byte> next;
            Append(next, old.source_, 0, first, false);
            Spaces(next, (std::max)(0, left - old.extent_)); // Materialize virtual padding.
            Spaces(next, begin - left); // Repair the unselected half of an old glyph.
            Append(next, replacement.source_, 0, replacement.source_.size(), true);
            Spaces(next, end - begin - replacement.extent_);
            Spaces(next, right - end);
            Append(next, old.source_, last, old.source_.size(), true);
            return EncodedRow(std::move(next));
        }
        static EncodedRow Crop(const Projection& view)
        {
            std::size_t end = 0;
            for (const auto& glyph : view.glyphs_)
                if (glyph.column + glyph.width <= view.Columns()) end = glyph.source.end;
                else break;
            return EncodedRow(std::vector<Byte>(view.source_.begin(), view.source_.begin() + end));
        }
        static EncodedRow Slice(const Projection& view, int begin, int end)
        {
            view.CheckRange(begin, end);
            std::vector<Byte> next;
            int column = begin;
            std::size_t previous = view.source_.size();
            for (const auto& glyph : view.glyphs_)
            {
                if (glyph.column < begin || glyph.column + glyph.width > end) continue;
                Spaces(next, glyph.column - column);
                Append(next, view.source_, glyph.source.begin, glyph.source.end, previous != glyph.source.begin);
                previous = glyph.source.end;
                column = glyph.column + glyph.width;
            }
            Spaces(next, end - column);
            return EncodedRow(std::move(next));
        }
    public:
        EncodedRow() = default; // Empty source is represented by virtual spaces.
        explicit EncodedRow(std::vector<Byte> bytes) : bytes_(std::move(bytes))
        {
            if (bytes_.size() > MaxBytes) throw std::length_error("encoded row source limit");
        }
        explicit EncodedRow(std::string_view bytes, Style style = {})
        {
            if (bytes.size() > MaxBytes) throw std::length_error("encoded row source limit");
            bytes_.reserve(bytes.size());
            for (unsigned char byte : bytes) bytes_.push_back({byte, style});
        }
        const std::vector<Byte>& Bytes() const { return bytes_; }
        Projection ReadUtf8(int columns) const
        {
            CheckColumns(columns);
            Builder builder(columns, bytes_);
            Utf8Decoder decoder;
            std::size_t begin = 0;
            const auto emit = [&](char32_t scalar, unsigned int length) {
                const auto end = begin + length;
                builder.Put(scalar, Width(scalar), begin, end, false);
                begin = end;
            };
            for (const auto& byte : bytes_)
            {
                if (byte.boundaryBefore) { decoder.FinishWithLength(emit); builder.EndAttachment(); }
                decoder.FeedWithLength(byte.value, emit);
            }
            // This is a complete stored row, NOT a network packet boundary.
            decoder.FinishWithLength(emit);
            return builder.Finish();
        }
        // Adapter returns one mapped legacy glyph, including consumed bytes and
        // protocol width. It must be pure: rendering must not advance a live
        // decoder/cursor or perform I/O. Windows/UAO conversion stays outside.
        template<class Decode> Projection ReadLegacy(int columns, Decode decode) const
        {
            CheckColumns(columns);
            Builder builder(columns, bytes_);
            std::size_t runEnd = 0;
            for (std::size_t begin = 0; begin < bytes_.size();)
            {
                if (begin == runEnd)
                {
                    runEnd = begin + 1;
                    while (runEnd < bytes_.size() && !bytes_[runEnd].boundaryBefore) ++runEnd;
                }
                const auto glyph = decode(bytes_.data() + begin, runEnd - begin);
                if (glyph.bytes < 1 || glyph.bytes > 2 || glyph.bytes > runEnd - begin ||
                    glyph.width < 1 || glyph.width > 2 || !Scalar(glyph.scalar))
                    throw std::invalid_argument("invalid legacy glyph from adapter");
                const auto end = begin + glyph.bytes;
                builder.Put(glyph.scalar, static_cast<int>(glyph.width), begin, end, true);
                begin = end;
            }
            return builder.Finish();
        }
        // Edits return a new source row: validation/allocation failure leaves the
        // old row and previously captured projections intact. No cursor changes.
        // An empty replacement erases; all ranges are end-exclusive cell ranges.
        EncodedRow ReplaceUtf8(int columns, int begin, int end, const EncodedRow& replacement) const
        {
            const auto old = ReadUtf8(columns);
            old.CheckRange(begin, end);
            return Replace(old, begin, end, replacement.ReadUtf8((std::max)(1, end - begin)));
        }
        template<class Decode> EncodedRow ReplaceLegacy(int columns, int begin, int end,
            const EncodedRow& replacement, Decode decode) const
        {
            const auto old = ReadLegacy(columns, decode);
            old.CheckRange(begin, end);
            return Replace(old, begin, end, replacement.ReadLegacy((std::max)(1, end - begin), decode));
        }
        EncodedRow CropUtf8(int columns) const { return Crop(ReadUtf8(columns)); }
        template<class Decode> EncodedRow CropLegacy(int columns, Decode decode) const { return Crop(ReadLegacy(columns, decode)); }
        EncodedRow SliceUtf8(int columns, int begin, int end) const { return Slice(ReadUtf8(columns), begin, end); }
        template<class Decode> EncodedRow SliceLegacy(int columns, int begin, int end, Decode decode) const
        {
            return Slice(ReadLegacy(columns, decode), begin, end);
        }
    };
}
