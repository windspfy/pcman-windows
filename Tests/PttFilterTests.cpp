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
	s.authors = {{L"Alice", Scope::Article}, {L"Bob", Scope::Comment, Choice::Color}};
	s.keywords = {{L"暴雷", Scope::Both, Choice::Hide}, {L"A B!", Scope::Comment, Choice::Stars, true}};
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
	check(at(3,L"123")==Effect::None && at(3,L"爆")==Effect::None && at(3,L"分類")==Effect::None,"list metadata");
	check(masks[4].empty(),"similar ID and keyword in category");
	check(at(5,L"含")==Effect::Hide && at(5,L"other")==Effect::None,"entire keyword title");
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
	check(at(6,L"bOB")==Effect::Color && at(6,L"有")==Effect::Hide && at(6,L"推")==Effect::None &&
		at(6,L"11/22")==Effect::None,"comment overlap, time and symbol");
	check(at(7,L"a b!")==Effect::Stars && at(7,L"Bob2")==Effect::None && masks[8].empty(),"exact keyword, spaces and scope");
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
	std::printf("PTT filter: %d checks, %d failures\n", checks, failures);
	return failures;
}
