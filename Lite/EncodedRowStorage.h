#pragma once
#include "EncodedTerminalRow.h"
#include <map>

namespace TerminalCells
{
    // Sparse source rows keyed by the existing row allocation's identity.
    // Pointer rotation needs no source copying. Recycle/release must Erase the
    // identity; allocation replacement uses Move. No cursor or global codepage.
    class EncodedRowStorage
    {
        std::map<const void*, EncodedRow> rows_;
        std::size_t rowLimit_, sourceLimit_, sourceBytes_ = 0;
        static void CheckIdentity(const void* row)
        {
            if (!row) throw std::invalid_argument("null row identity");
        }
    public:
        // Limits must be chosen explicitly by the adapter, not hidden defaults
        // or an allocation for every scrollback slot. sourceLimit counts payload
        // bytes, not vector/style/map overhead. Quota failure never drops rows.
        EncodedRowStorage(std::size_t rowLimit, std::size_t sourceLimit)
            : rowLimit_(rowLimit), sourceLimit_(sourceLimit)
        {
            if (rowLimit < 1 || rowLimit > 32767 || !sourceLimit)
                throw std::invalid_argument("invalid encoded row storage limits");
        }
        std::size_t StoredRows() const { return rows_.size(); }
        std::size_t SourceBytes() const { return sourceBytes_; }
        const EncodedRow& At(const void* row) const
        {
            CheckIdentity(row);
            const auto found = rows_.find(row);
            static const EncodedRow blank;
            return found == rows_.end() ? blank : found->second;
        }
        bool TrySet(const void* row, EncodedRow value)
        {
            CheckIdentity(row);
            const auto found = rows_.find(row);
            const auto oldSize = found == rows_.end() ? 0 : found->second.Bytes().size();
            const auto nextSize = value.Bytes().size();
            if (!nextSize) { Erase(row); return true; }
            if ((found == rows_.end() && rows_.size() >= rowLimit_) ||
                nextSize > sourceLimit_ - (sourceBytes_ - oldSize)) return false;
            rows_.insert_or_assign(row, std::move(value));
            sourceBytes_ = sourceBytes_ - oldSize + nextSize;
            return true;
        }
        void Erase(const void* row)
        {
            CheckIdentity(row);
            const auto found = rows_.find(row);
            if (found == rows_.end()) return;
            sourceBytes_ -= found->second.Bytes().size();
            rows_.erase(found);
        }
        // Candidates own their map nodes before any screen row is recycled.
        // The callback rotates/clears existing rows without allocating source.
        template<class Rotate> bool CommitRows(std::map<const void*, EncodedRow>& candidates, Rotate rotate)
        {
            auto count = rows_.size();
            auto bytes = sourceBytes_;
            for (const auto& [row, value] : candidates)
            {
                const auto old = At(row).Bytes().size();
                count -= old != 0;
                bytes -= old;
            }
            for (const auto& [row, value] : candidates)
            {
                count += !value.Bytes().empty();
                if (count > rowLimit_ || value.Bytes().size() > sourceLimit_ - bytes) return false;
                bytes += value.Bytes().size();
            }
            for (const auto& [row, value] : candidates) Erase(row);
            rotate();
            for (auto it = candidates.begin(); it != candidates.end(); )
                if (it->second.Bytes().empty()) it = candidates.erase(it);
                else ++it;
            rows_.merge(candidates);
            sourceBytes_ = bytes;
            return true;
        }
        bool Copy(const void* destination, const void* source)
        {
            CheckIdentity(destination);
            return TrySet(destination, At(source));
        }
        // Transform a detached candidate. Only publish it (and update accounting)
        // after the row and connection payload limits have both been checked.
        template<class Edit> bool TryEdit(const void* row, Edit edit)
        {
            try { return TrySet(row, edit(At(row))); }
            catch (const std::length_error&) { return false; }
            catch (const std::bad_alloc&) { return false; }
        }
        // Physical width reduction is a multi-row operation. Stage only
        // identities and byte counts; no row changes until all projections fit.
        template<class Project> bool TryCropAll(Project project)
        {
            using It = decltype(rows_)::iterator;
            std::vector<std::pair<It, std::size_t>> cuts;
            try
            {
                for (auto it = rows_.begin(); it != rows_.end(); ++it)
                {
                    const auto length = EncodedRow::CropLength(project(it->second));
                    if (length < it->second.Bytes().size()) cuts.emplace_back(it, length);
                }
            }
            catch (const std::length_error&) { return false; }
            catch (const std::bad_alloc&) { return false; }
            for (auto [it, length] : cuts)
            {
                sourceBytes_ -= it->second.Bytes().size() - length;
                if (length) it->second.bytes_.resize(length); // shrink only
                else rows_.erase(it);
            }
            return true;
        }
        void Move(const void* destination, const void* source)
        {
            CheckIdentity(destination); CheckIdentity(source);
            if (destination == source) return;
            // Node transfer needs no duplicated source budget or allocation.
            Erase(destination);
            auto node = rows_.extract(source);
            if (node.empty()) return; // Moving a virtual blank clears destination.
            node.key() = destination;
            rows_.insert(std::move(node));
        }
        std::vector<EncodedRow> Capture(const std::vector<const void*>& order) const
        {
            if (order.size() > 64) throw std::invalid_argument("snapshot exceeds visible row limit");
            std::vector<EncodedRow> snapshot;
            snapshot.reserve(order.size());
            for (const auto row : order) snapshot.push_back(At(row));
            return snapshot;
        }
        void Reset() { rows_.clear(); sourceBytes_ = 0; }
    };
}
