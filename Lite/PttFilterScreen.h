#pragma once
#include "PttFilter.h"
#include "PttWraps.h"
#include <regex>
#include <string_view>

namespace PttFilter
{
	// Ranges use UTF-16 offsets in decoded rows. The view maps them back to
	// terminal columns, preserving wide glyphs and both halves of Big5 cells.
	struct Span
	{
		size_t begin, end;
		Effect effect;
	};
	using Masks = std::vector<std::vector<Span>>;

	inline void Mark(std::vector<Span>& spans, size_t begin, size_t end, Effect effect)
	{
		if (effect != Effect::None && begin < end) spans.push_back({begin, end, effect});
	}

	inline Effect Strongest(const Settings& settings, Field field, Scope scope, const std::wstring& value)
	{
		Effect effect = Effect::None;
		settings.Match(field, scope, value, [&](size_t, size_t, Effect match) {
			if (match > effect) effect = match;
		});
		return effect;
	}

	inline size_t SkipPadding(const std::wstring& row, size_t start)
	{
		const auto begin = row.find_first_not_of(L' ', start);
		return begin == std::wstring::npos ? row.size() : begin;
	}

	inline size_t EndOfText(const std::wstring& row)
	{
		return row.find_last_not_of(L' ') + 1;
	}

	inline size_t TitleStart(const std::wstring& row, size_t start)
	{
		// A reply/forward prefix precedes the optional board category.
		size_t begin = SkipPadding(row, start);
		if (row.compare(begin, 3, L"Re:") == 0 || row.compare(begin, 3, L"Fw:") == 0)
			begin = SkipPadding(row, begin+3);
		if (begin < row.size() && row[begin] == L'[')
		{
			const auto close = row.find(L']', begin+1);
			if (close != row.npos) begin = SkipPadding(row, close+1);
		}
		return begin;
	}

	inline bool ArticleListHeading(const std::wstring& row)
	{
		std::wstring compact;
		for (auto c : row) if (c != L' ') compact += c;
		return compact.find(L"編號") != compact.npos &&
			compact.find(L"日期作者") != compact.npos && compact.find(L"標題") != compact.npos;
	}

	inline void MaskListRow(const Settings& settings, const std::wstring& row, std::vector<Span>& spans)
	{
		// PTT places flags and push counts between the number and date.
		static const std::wregex entry(LR"(^[ >●]*(?:[0-9]+|★)[^/]{0,14} [0-9]{1,2}/[0-9]{1,2} +([A-Za-z0-9]+) +(?:[□◇◆轉]|R:|Re:|Fw:) *(.*)$)");
		std::wsmatch m;
		if (!std::regex_match(row, m, entry)) return;
		const Effect author = Strongest(settings, Field::Author, Scope::Article, m[1]);
		Mark(spans, m.position(1), m.position(1) + m.length(1), author);
		const size_t begin = TitleStart(row, m.position(2)), end = EndOfText(row);
		if (begin >= end) return;
		auto keyword = Strongest(settings, Field::Text, Scope::Article, row.substr(begin, end - begin));
		Mark(spans, begin, end, (std::max)(author, keyword));
	}

	inline bool MaskComment(const Settings& settings, const std::wstring& row, std::vector<Span>& spans)
	{
		if (row.size() < 2 || row[1] != L' ' || (row[0] != L'推' && row[0] != L'噓' && row[0] != L'→')) return false;
		static const std::wregex comment(LR"(^(?:推|噓|→) +([A-Za-z0-9]+) *[:：] *(.*)$)");
		std::wsmatch m;
		if (!std::regex_match(row, m, comment)) return true; // Unrecognized comment fields stay visible.
		const Effect author = Strongest(settings, Field::Author, Scope::Comment, m[1]);
		Mark(spans, m.position(1), m.position(1) + m.length(1), author);
		const size_t begin = m.position(2);
		size_t end = EndOfText(row);
		static const std::wregex stamp(LR"( +[0-9]{2}/[0-9]{2} +[0-9]{2}:[0-9]{2} *$)");
		std::wsmatch time;
		if (std::regex_search(row, time, stamp)) end = time.position();
		if (begin < end)
		{
			auto keyword = Strongest(settings, Field::Text, Scope::Comment, row.substr(begin, end - begin));
			Mark(spans, begin, end, (std::max)(author, keyword));
		}
		return true;
	}

	inline bool ArticleMetadata(const std::wstring& row)
	{
		if (row.empty()) return false;
		if (row.front() == L'─') return row.find_first_not_of(L"─ ") == row.npos;
		if (row.front() != L'※') return false;
		const auto begin = SkipPadding(row, 1);
		for (const std::wstring_view label : {L"發信站:", L"文章網址:", L"編輯:", L"轉錄者:"})
			if (row.compare(begin, label.size(), label) == 0) return true;
		return false;
	}

	inline Masks Analyze(const Settings& settings, const std::vector<std::wstring>& rows,
		const std::vector<Wrap>& wrapped, int first, int last, Article& article)
	{
		Masks masks(rows.size());
		if (rows.size() < 4) { article = {}; return masks; }
		const size_t footer = rows.size() - 1;
		if (!first || rows.back().find(L"瀏覽") == std::wstring::npos)
		{
			article = {};
			for (size_t y = 0; y < 4 && y < footer; ++y)
				if (ArticleListHeading(rows[y]) && (rows.front().find(L"看板") != std::wstring::npos ||
					rows.back().find(L"文章列表") != std::wstring::npos || rows.back().find(L"文章選讀") != std::wstring::npos ||
					rows.back().find(L"系列文章") != std::wstring::npos || rows.back().find(L"文摘列表") != std::wstring::npos))
				{
					for (++y; y < footer; ++y) MaskListRow(settings, rows[y], masks[y]);
					break;
				}
			return masks;
		}
		static const std::wregex byline(LR"(^ *作者 +([A-Za-z0-9]+) +(.*)$)");
		std::wsmatch by;
		const auto board = rows[0].rfind(L" 看板 ");
		const bool header = first == 1 && board != rows[0].npos && SkipPadding(rows[0], board+4) < rows[0].size() &&
			std::regex_match(rows[0], by, byline) &&
			rows[1].find(L"標題") == SkipPadding(rows[1], 0) && rows[2].find(L"時間") == SkipPadding(rows[2], 0);
		if (last < first) { article = {}; return masks; }
		if (header) article.author = by[1];
		else if (first == 1 || first - 1 > article.last || last < article.first - 1) article = {};
		if (article.author.empty()) return masks;
		article.first = first;
		article.last = last;
		const auto author = Strongest(settings, Field::Author, Scope::Article, article.author);
		size_t y = 0;
		if (header)
		{
			const auto end = rows[0].find_last_not_of(L' ', board - 1) + 1;
			Mark(masks[0], by.position(1), end, author);
			y = 1;
		}
		// The top of a scrolled reader can still contain the title or time.
		if (first <= 2)
		{
			const auto& title = rows[y];
			if (title.find(L"標題") != SkipPadding(title, 0)) { article = {}; return masks; }
			const auto begin = TitleStart(title, title.find(L"標題") + 2);
			const size_t stop = EndOfText(title);
			if (begin < stop) Mark(masks[y], begin, stop, (std::max)(author,
				Strongest(settings, Field::Text, Scope::Article, title.substr(begin, stop - begin))));
			++y;
		}
		if (first <= 3)
		{
			if (rows[y].find(L"時間") != SkipPadding(rows[y], 0)) { article = {}; return masks; }
			++y;
		}
		while (y < footer)
		{
			if (MaskComment(settings, rows[y], masks[y]) || ArticleMetadata(rows[y])) { ++y; continue; }
			const size_t start = y;
			std::wstring paragraph;
			std::vector<size_t> offsets;
			do {
				offsets.push_back(paragraph.size());
				const auto next = y+1 < footer && y+1 < wrapped.size() ? wrapped[y+1] : Wrap::Separate;
				size_t end = next == Wrap::Separate ? EndOfText(rows[y]) : rows[y].size();
				if (next == Wrap::BeforeWideGlyph && end && rows[y][end-1] == L' ') --end;
				paragraph.append(rows[y], 0, end);
				Mark(masks[y], 0, EndOfText(rows[y]), author);
				++y;
			} while (y < footer && y < wrapped.size() && wrapped[y] != Wrap::Separate);
			offsets.push_back(paragraph.size());
			const auto leading = SkipPadding(paragraph, 0);
			settings.Match(Field::Text, Scope::Article, paragraph.substr(leading),
				[&](size_t pos, size_t length, Effect effect) {
					pos += leading;
					for (size_t n = 0; n + 1 < offsets.size(); ++n)
						if (offsets[n] < pos + length && offsets[n + 1] > pos)
							Mark(masks[start+n], 0, EndOfText(rows[start+n]), effect);
				});
		}
		return masks;
	}
}
