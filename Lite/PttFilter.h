#pragma once

#include <algorithm>
#include <string>
#include <vector>

namespace PttFilter
{
	enum class Effect { None, Color, Stars, Hide };
	enum class Choice { Default, Color, Stars, Hide };
	// Where a rule applies: board list rows, article bodies (incl. the header), comments.
	enum class Scope { List = 1, Body = 2, Comment = 4, All = 7 };
	enum class Field { Author, Text };
	enum class Kind { Author, Keyword };

	struct Rule
	{
		Kind kind = Kind::Keyword;
		std::wstring value;
		Scope scope = Scope::All;  // Any combination of Scope flags.
		Choice choice = Choice::Default;
		// Keywords in article bodies mask the whole row instead of the word.
		// List rows and comments always mask the whole row; authors mask the whole article.
		bool line = true;
	};

	inline bool Covers(Scope scopes, Scope one) { return (static_cast<int>(scopes) & static_cast<int>(one)) != 0; }

	inline wchar_t FoldEnglish(wchar_t ch)
	{
		return ch >= L'A' && ch <= L'Z' ? ch + (L'a' - L'A') : ch;
	}

	inline bool SameText(const std::wstring& a, const std::wstring& b)
	{
		return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(),
			[](wchar_t x, wchar_t y) { return FoldEnglish(x) == FoldEnglish(y); });
	}

	// PTT IDs start with a letter and hold 2-12 ASCII letters or digits.
	inline bool ValidAuthor(const std::wstring& id)
	{
		if (id.size() < 2 || id.size() > 12) return false;
		const auto alpha = [](wchar_t c) { return (c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z'); };
		if (!alpha(id[0])) return false;
		return std::all_of(id.begin(), id.end(), [&](wchar_t c) { return alpha(c) || (c >= L'0' && c <= L'9'); });
	}

	inline std::wstring Trim(const std::wstring& text)
	{
		static const wchar_t blank[] = {L' ', L'\t', wchar_t(0x3000), 0};  // incl. ideographic space
		const auto begin = text.find_first_not_of(blank);
		if (begin == text.npos) return {};
		return text.substr(begin, text.find_last_not_of(blank) + 1 - begin);
	}

	struct Article
	{
		std::wstring author;  // Empty when the reader opened past the header.
		int first = 0, last = 0;
		bool board = false;   // Inside a board list or one of its articles.
	};

	struct Settings
	{
		std::vector<Rule> rules;
		Effect defaultEffect = Effect::Stars;
		int foreground = -1;
		int background = -1;

		bool Empty() const { return rules.empty(); }

		Effect Resolve(Choice choice) const
		{
			switch (choice)
			{
			case Choice::Color: return Effect::Color;
			case Choice::Stars: return Effect::Stars;
			case Choice::Hide: return Effect::Hide;
			default: return defaultEffect;
			}
		}

		// Index of a rule with the same kind and value (ignoring English case), or -1.
		int Find(Kind kind, const std::wstring& value, int except = -1) const
		{
			for (size_t i = 0; i < rules.size(); ++i)
				if (int(i) != except && rules[i].kind == kind && SameText(rules[i].value, value))
					return int(i);
			return -1;
		}

		// Offsets refer to the supplied field, excluding layout padding. Keywords
		// match anywhere; every occurrence is reported so a wrapped paragraph masks
		// only affected rows. onMatch(pos, length, effect, line).
		template<typename OnMatch>
		void Match(Field field, Scope scope, const std::wstring& text, OnMatch onMatch) const
		{
			const auto equal = [](wchar_t a, wchar_t b) { return FoldEnglish(a) == FoldEnglish(b); };
			const Kind kind = field == Field::Author ? Kind::Author : Kind::Keyword;
			for (const Rule& rule : rules)
			{
				if (rule.kind != kind || rule.value.empty() || !Covers(rule.scope, scope))
					continue;
				if (kind == Kind::Author)
				{
					if (SameText(text, rule.value))
						onMatch(0, text.size(), Resolve(rule.choice), true);
					continue;
				}
				auto start = text.begin();
				while (start != text.end())
				{
					auto found = std::search(start, text.end(), rule.value.begin(), rule.value.end(), equal);
					if (found == text.end()) break;
					onMatch(static_cast<size_t>(found - text.begin()), rule.value.size(), Resolve(rule.choice), rule.line);
					start = found + 1;
				}
			}
		}
	};
}
