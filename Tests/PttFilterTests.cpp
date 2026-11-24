#include "../Lite/PttFilterScreen.h"
#include <cstdio>

int RunPttFilterTests()
{
	using namespace PttFilter;
	int checks = 0, failures = 0;
	auto check = [&](bool ok, const char* label) {
		++checks;
		if (!ok) { ++failures; std::fprintf(stderr, "PTT filter: %s\n", label); }
	};
	Settings s;
	const auto ListBody = Scope(int(Scope::List) | int(Scope::Body));
	s.rules = {{Kind::Author, L"Alice", ListBody}, {Kind::Author, L"Bob", Scope::Comment, Choice::Color},
		{Kind::Keyword, L"暴雷", Scope::All, Choice::Hide}, {Kind::Keyword, L"A B!", Scope::Comment, Choice::Stars}};
	std::vector<std::wstring> rows = {L"看板《Test》", L"[→]閱讀", L" 編號   日 期 作 者       文 章 標 題 人氣",
		L"> 123 +爆 11/22 ALICE    □ [分類] 正常標題", L"  124 +  11/22 Alice2   □ [暴雷] 標題",
		L"  125 +  11/22 other    R: [分類] 含暴雷", L"文章列表"};
	Masks masks;
	auto at = [&](int y, const wchar_t* what) {
		const auto pos = rows[y].find(what);
		Effect result = Effect::None;
		for (auto span : masks[y]) if (span.begin <= pos && pos < span.end && span.effect > result) result = span.effect;
		return result;
	};
	Article a;
	auto list = [&] { masks = Analyze(s, rows, {}, 0, 0, a); };
	list();
	check(at(3,L"ALICE")==Effect::Stars && at(3,L"正常")==Effect::Stars, "list author and title");
	check(at(3,L"123")==Effect::None && at(3,L"爆")==Effect::Stars && at(3,L">")==Effect::None,
		"list rows mask all but the cursor and number");
	check(masks[4].empty(),"similar ID and keyword in category");
	check(at(5,L"含")==Effect::Hide && at(5,L"other")==Effect::Hide,"keyword title masks the row");
	rows = {L" 作者  Alice (nickname)                                     看板  Test", L" 標題  [分類] 標題", L" 時間  11/22",
		L"────",L"正文暴",L"雷結局",L"推 bOB:   有暴雷    11/22 10:00",L"→ Bob2: a b!  11/22 10:00",
		L"噓 Bob2: a  b!  11/22 10:00", L"推 Other: 好文  11/22 10:00", L"瀏覽 第 1/2 頁 (50%) 目前顯示: 第 1~10 行"};
	std::vector<Wrap> wraps(rows.size());
	auto article = [&] { masks = Analyze(s, rows, wraps, 1, 10, a); };
	article();
	check(at(0,L"Alice")==Effect::Stars && at(0,L"看板")==Effect::None && masks[2].empty(),"header fields");
	check(at(1,L"分類")==Effect::None && masks[3].empty(),"header metadata");
	check(at(4,L"正文")==Effect::Stars && at(5,L"雷")==Effect::Stars,"manual line break");
	wraps[5] = Wrap::AtMargin;
	article();
	check(at(4,L"正文")==Effect::Hide && at(5,L"結局")==Effect::Hide,"confirmed wrap");
	check(at(6,L"bOB")==Effect::Hide && at(6,L"有")==Effect::Hide && at(6,L"推")==Effect::None &&
		at(6,L"11/22")==Effect::None, "comments mask ID and text with the strongest effect");
	check(at(7,L"a b!")==Effect::Stars && at(7,L"Bob2")==Effect::Stars && masks[8].empty(),"keyword spaces and scope");
	check(masks[9].empty() && masks.back().empty(),"other commenter and footer");
	rows[4] += L' ';
	article();
	check(at(4,L"正文")==Effect::Stars, "typed space at the margin separates words");
	wraps[5] = Wrap::BeforeWideGlyph;
	article();
	check(at(4,L"正文")==Effect::Hide, "unused edge before a wide glyph is padding");
	rows[4].pop_back();
	wraps[5] = Wrap::AtMargin;
	s.defaultEffect = Effect::Color;
	article();
	check(at(0,L"Alice")==Effect::Color && at(5,L"結局")==Effect::Hide,"following and overriding default");
	auto partial = rows;
	partial.erase(partial.begin());
	masks = Analyze(s, partial, {}, 2, 10, a);
	check(!masks[0].empty() && masks[0][0].begin == partial[0].rfind(L"標題") && masks[1].empty(),
		"scrolling the author offscreen preserves category and timestamp");
	auto following = rows;
	following[0] = L"下一頁的正文";
	masks = Analyze(s, following, wraps, 9, 18, a);
	check(!masks[0].empty() && masks[0][0].effect==Effect::Color, "continued reading page retains author");
	Wraps links;
	char storage[4]{};
	links.Start(storage+1, storage, false);
	links.Start(storage+2, storage+1, true);
	check(links.Between(storage, storage+1)==Wrap::AtMargin &&
		links.Between(storage+1, storage+2)==Wrap::BeforeWideGlyph, "wraps move with adjacent physical rows");
	links.Forget(storage+1);
	check(links.Between(storage, storage+1)==Wrap::Separate &&
		links.Between(storage+1, storage+2)==Wrap::Separate, "recycling a row breaks both links");
	rows.back() = L"編輯文章";
	list();
	check(masks[4].empty() && a.author.empty(),"editor resets context");
	rows[0] = L"寄件者 Alice"; rows.back() = L"瀏覽 第 1/1 頁";
	article();
	check(masks[6].empty(),"mail reader");
	check(ArticleMetadata(L"※ 發信站: 批踢踢實業坊") && !ArticleMetadata(L"※ 注意暴雷"),
		"body bullets distinguished from server metadata");
	std::vector<Span> unknown;
	check(MaskComment(s, L"推 (unknown): 暴雷", unknown) && unknown.empty(),
		"unknown comment fields remain visible");
	// Real PTT rows: the 2-column push count abuts the date (mbbsd/bbs.c readdoent).
	s.defaultEffect = Effect::Stars;
	rows = {L"【板主:x】 看板《Test》", L"[←]離開 [→]閱讀", L"   編號    日 期 作者          文  章  標  題",
		L"●123456 +爆11/24 ALICE        □ [問卦] 正常標題", L" 123457 + 511/24 other        轉 [新聞] 含暴雷",
		L" 123458   X111/24 bob          R: [閒聊] 無關", L"      ★   11/24 Alice        □ [公告] 置底",
		L" 123459 ~ 1 1/05 other        鎖 [情報] 平常", L"文章選讀 (y)回應"};
	list();
	check(at(3,L"ALICE")==Effect::Stars && at(3,L"123456")==Effect::None && at(3,L"11/24")==Effect::Stars &&
		at(3,L"●")==Effect::None, "push count abutting the date");
	check(at(4,L"含")==Effect::Hide && at(4,L"other")==Effect::Hide, "forwarded title keyword");
	check(masks[5].empty(), "comment-only author rule skips the list");
	check(at(6,L"Alice")==Effect::Stars && at(6,L"★")==Effect::None, "pinned article keeps its star");
	check(masks[7].empty(), "single-digit month and locked mark");
	check(a.board && a.author.empty(), "board list starts a board context");

	// Opening past the header (or jumping to the end) still filters comments and body.
	std::vector<std::wstring> tail = {L"正文暴雷", L"推 Bob: 不喜歡       11/24 10:00",
		L"推 Other: 有暴雷     11/24 10:01", L"推 Other: 好文       11/24 10:02",
		L"瀏覽 第 20/20 頁 (100%) 目前顯示: 第 480~483 行"};
	masks = Analyze(s, tail, {}, 480, 483, a);
	auto atTail = [&](int y, const wchar_t* what) {
		const auto pos = tail[y].find(what);
		Effect result = Effect::None;
		for (auto span : masks[y]) if (span.begin <= pos && pos < span.end && span.effect > result) result = span.effect;
		return result;
	};
	check(atTail(0,L"正文")==Effect::Hide, "body keyword without a known author");
	check(atTail(1,L"Bob")==Effect::Color && atTail(2,L"有")==Effect::Hide && atTail(2,L"Other")==Effect::Hide && atTail(2,L"11/24")==Effect::None && atTail(2,L"推")==Effect::None &&
		masks[3].empty(), "comments without a known author");
	rows = {L" 作者  Alice (nickname)                                     看板  Test", L" 標題  [分類] 標題", L" 時間  11/22",
		L"正文", L"瀏覽 第 1/20 頁 (5%) 目前顯示: 第 1~4 行"};
	Analyze(s, rows, {}, 1, 4, a);
	masks = Analyze(s, tail, {}, 480, 483, a);
	check(atTail(0,L"正文")==Effect::Hide && a.author == L"Alice", "jumping to the last page keeps the author");
	std::vector<std::wstring> blank(tail.size(), L"");
	Analyze(s, blank, {}, 0, 0, a);
	check(a.board && a.author == L"Alice", "a blank redraw keeps the article context");
	Article mail;
	masks = Analyze(s, tail, {}, 480, 483, mail);
	check(masks[0].empty() && masks[1].empty(), "a reader not opened from a board stays unfiltered");

	// Body keywords can mask only the word, also across a confirmed wrap.
	Settings k;
	k.rules = {{Kind::Keyword, L"暴雷", Scope::Body, Choice::Stars, false}, {Kind::Keyword, L"劇透", Scope::List}};
	rows = {L" 作者  Carol (n)                       看板  Test", L" 標題  暴雷消息", L" 時間  11/22",
		L"前言暴雷後話", L"AAA暴", L"雷BBB", L"劇透", L"推 Xyz: 暴雷  11/24 10:00", L"瀏覽 第 1/1 頁 (100%) 目前顯示: 第 1~8 行"};
	std::vector<Wrap> kw(rows.size());
	kw[5] = Wrap::AtMargin;
	Article fresh;
	masks = Analyze(k, rows, kw, 1, 8, fresh);
	check(at(1,L"暴雷")==Effect::Stars && at(1,L"消息")==Effect::None, "keyword-only title");
	check(at(3,L"暴雷")==Effect::Stars && at(3,L"前言")==Effect::None && at(3,L"後話")==Effect::None, "keyword-only body");
	check(at(4,L"暴")==Effect::Stars && at(4,L"AAA")==Effect::None && at(5,L"雷")==Effect::Stars &&
		at(5,L"BBB")==Effect::None, "keyword-only match across a wrap");
	check(masks[6].empty() && masks[7].empty(), "list-only keyword skips the body; body-only skips comments");
	k.rules[0].line = true;
	masks = Analyze(k, rows, kw, 1, 8, fresh);
	check(at(3,L"前言")==Effect::Stars && at(4,L"AAA")==Effect::Stars && at(5,L"BBB")==Effect::Stars, "line extent");

	check(ValidAuthor(L"abc123") && !ValidAuthor(L"1abc") && !ValidAuthor(L"a") &&
		!ValidAuthor(L"abcdefghijklm") && !ValidAuthor(L"ab_c"), "author ID validation");
	check(Trim(L"  暴雷　") == L"暴雷" && s.Find(Kind::Author, L"ALICE") == 0 &&
		s.Find(Kind::Keyword, L"alice") < 0 && s.Find(Kind::Author, L"alice", 0) < 0, "duplicate lookup");
	std::printf("PTT filter: %d checks, %d failures\n", checks, failures);
	return failures;
}
