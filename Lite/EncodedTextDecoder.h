#pragma once
#include "EncodedTerminalRow.h"
#include <array>
#include <functional>

namespace TerminalCells
{
    // Text-only receive adapter. Feed AFTER ANSI/Telnet framing. Packet ends do
    // nothing; FinishText marks a real control/position/EOF boundary. No cursor,
    // global charset, transport, renderer, or clipboard is owned here.
    class EncodedTextDecoder
    {
    public:
        struct Glyph
        {
            char32_t scalar = 0;
            int width = 0;
            unsigned int codePage = 65001;
            EncodedRow source;
        };
        struct Batch
        {
            std::array<Glyph, 2> glyphs;
            std::size_t count = 0;
            const Glyph* begin() const { return glyphs.data(); }
            const Glyph* end() const { return glyphs.data() + count; }
        };
    private:
        unsigned int codePage_ = 65001;
        Utf8Decoder utf8_;
        std::array<EncodedRow::Byte, 4> pending_{};
        std::size_t count_ = 0;
        bool boundaryNext_ = true;
        using Read = std::function<EncodedRow::LegacyGlyph(const EncodedRow::Byte*, std::size_t)>;
        using Lead = std::function<bool(unsigned char)>;
        Read read_;
        Lead lead_;

        void Emit(Batch& batch, char32_t scalar, int width, unsigned int length)
        {
            if (!length || length > count_ || batch.count >= batch.glyphs.size() ||
                scalar > 0x10ffff || (scalar >= 0xd800 && scalar <= 0xdfff) || width < 0 || width > 2)
                throw std::invalid_argument("invalid decoded text glyph");
            Glyph next{scalar, width, codePage_, EncodedRow(std::vector<EncodedRow::Byte>(
                pending_.begin(), pending_.begin() + length))};
            batch.glyphs[batch.count++] = std::move(next);
            std::move(pending_.begin() + length, pending_.begin() + count_, pending_.begin());
            count_ -= length;
        }
        void EmitLegacy(Batch& batch)
        {
            const auto glyph = read_(pending_.data(), count_);
            if (glyph.bytes < 1 || glyph.bytes > 2 || glyph.width < 1 || glyph.width > 2)
                throw std::invalid_argument("invalid legacy text glyph");
            Emit(batch, glyph.scalar, static_cast<int>(glyph.width), glyph.bytes);
        }
        // Only primitive decoding state needs rollback; configured adapters are
        // immutable during Feed/Finish. Output allocation/adapter failure must
        // not discard a pending source prefix.
        template<class Operation> Batch Transaction(Operation operation)
        {
            const auto decoder = utf8_;
            const auto pending = pending_;
            const auto count = count_;
            const auto boundary = boundaryNext_;
            try { return operation(); }
            catch (...)
            {
                utf8_ = decoder; pending_ = pending; count_ = count; boundaryNext_ = boundary;
                throw;
            }
        }
    public:
        unsigned int CodePage() const { return codePage_; }
        bool Pending() const { return count_ != 0; }
        Batch Feed(unsigned char byte, Style style = {})
        {
            return Transaction([&] {
                Batch batch;
                if (count_ >= pending_.size()) throw std::logic_error("pending text source overflow");
                pending_[count_++] = {byte, style, boundaryNext_};
                boundaryNext_ = false;
                if (codePage_ == 65001)
                    utf8_.FeedWithLength(byte, [&](char32_t scalar, unsigned int length) {
                        Emit(batch, scalar, Width(scalar), length);
                    });
                else
                {
                    // A single lead is held, including across SGR/OSC/packets.
                    // On a non-pair, emit the old byte then reconsider the new
                    // byte as a possible lead, just like strict UTF-8 recovery.
                    if (count_ == 2) EmitLegacy(batch);
                    if (count_ == 1 && !lead_(pending_[0].value)) EmitLegacy(batch);
                }
                return batch;
            });
        }
        Batch FinishText()
        {
            return Transaction([&] {
                Batch batch;
                if (codePage_ == 65001)
                    utf8_.FinishWithLength([&](char32_t scalar, unsigned int length) {
                        Emit(batch, scalar, Width(scalar), length);
                    });
                else if (count_) EmitLegacy(batch);
                if (count_) throw std::logic_error("unfinished legacy text prefix");
                boundaryNext_ = true;
                return batch;
            });
        }
        Batch UseUtf8()
        {
            if (codePage_ == 65001) return {};
            auto old = FinishText();
            codePage_ = 65001; read_ = {}; lead_ = {};
            return old; // Caller commits these OLD-encoding glyphs first.
        }
        template<class Decode> Batch UseLegacy(Decode decode)
        {
            const unsigned int codePage = decode.CodePage();
            if (!codePage || codePage == 65001) throw std::invalid_argument("legacy codepage must be explicit");
            if (codePage == codePage_) return {};
            // Build both closures before consuming an old prefix. A failed
            // allocation leaves the old encoding and its pending bytes intact.
            Read read = decode;
            Lead lead = [decode](unsigned char byte) { return decode.IsLead(byte); };
            auto old = FinishText();
            codePage_ = codePage; read_ = std::move(read); lead_ = std::move(lead);
            return old;
        }
        void Reset()
        {
            utf8_.Reset(); count_ = 0; boundaryNext_ = true;
            // Keep the explicitly selected encoding, discard old-session bytes.
        }
    };
}
