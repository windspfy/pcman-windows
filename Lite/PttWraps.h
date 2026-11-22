#pragma once
#include <map>

namespace PttFilter
{
	enum class Wrap { Separate, AtMargin, BeforeWideGlyph };

	// Row allocations move with their text when the terminal scrolls. Keep
	// both endpoints so a recycled row can drop its two links in O(log n).
	class Wraps
	{
		struct Link { const char* left; Wrap kind; };
		std::map<const char*, Link> previous;
		std::map<const char*, const char*> next;
	public:
		Wrap Between(const char* left, const char* right) const
		{
			const auto it = previous.find(right);
			return it != previous.end() && it->second.left == left ? it->second.kind : Wrap::Separate;
		}
		void Forget(const char* row)
		{
			const auto before = previous.find(row);
			if (before != previous.end()) { next.erase(before->second.left); previous.erase(before); }
			const auto after = next.find(row);
			if (after != next.end()) { previous.erase(after->second); next.erase(after); }
		}
		void Start(const char* row, const char* source, bool clearEdge)
		{
			Forget(row);
			if (!source || source == row) return;
			const auto old = next.find(source);
			if (old != next.end()) previous.erase(old->second);
			next[source] = row;
			previous[row] = {source, clearEdge ? Wrap::BeforeWideGlyph : Wrap::AtMargin};
		}
		void Reset() { previous.clear(); next.clear(); }
	};
}
