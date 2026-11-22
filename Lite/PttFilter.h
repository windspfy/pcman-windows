#pragma once

#include <algorithm>
#include <string>
#include <vector>

namespace PttFilter
{
	enum class Effect { None, Color, Stars, Hide };
	enum class Choice { Default, Color, Stars, Hide };
	enum class Scope { Article = 1, Comment = 2, Both = 3 };
	enum class Field { Author, Text };

	struct Rule
	{
		std::wstring value;
		Scope scope = Scope::Both;
		Choice choice = Choice::Default;
		bool exact = false;
	};

	inline wchar_t FoldEnglish(wchar_t ch)
	{
		return ch >= L'A' && ch <= L'Z' ? ch + (L'a' - L'A') : ch;
	}

	struct Article
	{
		std::wstring author;
		int first = 0, last = 0;
	};

	struct Settings
	{
		std::vector<Rule> authors;
		std::vector<Rule> keywords;
		Effect defaultEffect = Effect::Stars;
		int foreground = -1;
		int background = -1;

		bool Empty() const { return authors.empty() && keywords.empty(); }

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

		// Offsets refer to the supplied field, excluding layout padding.
		// Report every occurrence so a wrapped paragraph masks only affected rows.
		template<typename OnMatch>
		void Match(Field field, Scope scope, const std::wstring& text, OnMatch onMatch) const
		{
			const auto equal = [](wchar_t a, wchar_t b) { return FoldEnglish(a) == FoldEnglish(b); };
			const auto& rules = field == Field::Author ? authors : keywords;
			for (const Rule& rule : rules)
			{
				if (!(static_cast<int>(scope) & static_cast<int>(rule.scope)) || rule.value.empty())
					continue;
				if (field == Field::Author || rule.exact)
				{
					if (text.size() == rule.value.size() &&
						std::equal(text.begin(), text.end(), rule.value.begin(), equal))
						onMatch(0, text.size(), Resolve(rule.choice));
					continue;
				}
				auto start = text.begin();
				while (start != text.end())
				{
					auto found = std::search(start, text.end(), rule.value.begin(), rule.value.end(), equal);
					if (found == text.end()) break;
					onMatch(static_cast<size_t>(found - text.begin()), rule.value.size(), Resolve(rule.choice));
					start = found + 1;
				}
			}
		}
	};
}
