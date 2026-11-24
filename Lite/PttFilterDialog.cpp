#include "stdafx.h"
#include "PttFilterDialog.h"
#include "AppConfig.h"
#include "../Resource/resource.h"
#include <commctrl.h>

namespace PttFilter
{
	static CString Path() { return ConfigPath + "PttFilter.dat"; }

	// Unreleased format: files from other versions are ignored.
	static const UINT kVersion = 3;

	static Settings Load()
	{
		try
		{
			CFile f;
			if (!f.Open(Path(), CFile::modeRead | CFile::shareDenyWrite)) return {};
			CArchive ar(&f, CArchive::load);
			UINT version, count;
			int effect, fg, bg;
			ar >> version;
			if (version != kVersion) return {};
			ar >> effect >> fg >> bg >> count;
			if (effect < 1 || effect > 3 || fg < -1 || fg > 15 || bg < -1 || bg > 15) return {};
			Settings s;
			s.defaultEffect = Effect(effect);
			s.foreground = fg;
			s.background = bg;
			for (UINT i = 0; i < count; ++i)
			{
				CStringW value;
				BYTE kind, scope, choice, line;
				ar >> kind >> value >> scope >> choice >> line;
				if (kind <= 1 && scope >= 1 && scope <= 7 && choice <= 3 && !value.IsEmpty())
					s.rules.push_back({Kind(kind), value.GetString(), Scope(scope), Choice(choice), line != 0});
			}
			return s;
		}
		catch (CException* e) { e->Delete(); return {}; }
	}

	static Settings& Shared() { static Settings settings = Load(); return settings; }
	const Settings& Current() { return Shared(); }

	static bool Save(const Settings& s)
	{
		const CString path = Path();
		char tmp[MAX_PATH];
		if (!GetTempFileName(ConfigPath, "ptf", 0, tmp)) return false;
		try
		{
			CFile f(tmp, CFile::modeWrite | CFile::modeCreate | CFile::shareExclusive);
			CArchive ar(&f, CArchive::store);
			ar << kVersion << int(s.defaultEffect) << s.foreground << s.background;
			ar << UINT(s.rules.size());
			for (auto& r : s.rules)
				ar << BYTE(r.kind) << CStringW(r.value.c_str()) << BYTE(r.scope) << BYTE(r.choice) << BYTE(r.line);
			ar.Close();
			f.Close();
			if (MoveFileEx(tmp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return true;
		}
		catch (CException* e) { e->Delete(); }
		DeleteFile(tmp);
		return false;
	}

	// ---- Labels -----------------------------------------------------------

	static const wchar_t* const kKinds[] = {L"帳號", L"關鍵字"};
	static const wchar_t* const kEffects[] = {L"標色", L"星號", L"隱藏"};  // Effect - 1
	static const wchar_t* const kColors[16] = {L"黑", L"紅", L"綠", L"黃", L"藍", L"紫", L"青", L"白",
		L"灰", L"亮紅", L"亮綠", L"亮黃", L"亮藍", L"亮紫", L"亮青", L"亮白"};
	static const struct { Scope scope; int id; LPCWSTR name; } kPlaces[] = {
		{Scope::List, IDC_FILTER_IN_LIST, L"列表"}, {Scope::Body, IDC_FILTER_IN_BODY, L"內文"},
		{Scope::Comment, IDC_FILTER_IN_COMMENT, L"推文"}};

	static std::wstring ScopeLabel(Scope scope)
	{
		std::wstring text;
		for (auto& p : kPlaces)
			if (Covers(scope, p.scope)) text += (text.empty() ? L"" : L"、") + std::wstring(p.name);
		return text;
	}
	// How much of an article body a match masks.
	static std::wstring ExtentLabel(const Rule& r)
	{
		if (!Covers(r.scope, Scope::Body)) return L"—";
		return r.kind == Kind::Author ? L"整篇" : r.line ? L"單行" : L"關鍵字";
	}
	static std::wstring EffectLabel(Choice choice)
	{
		return choice == Choice::Default ? L"預設" : kEffects[int(choice) - 1];
	}

	static int Selected(HWND h, int id) { return int(SendDlgItemMessageW(h, id, CB_GETCURSEL, 0, 0)); }
	static void Select(HWND h, int id, int n) { SendDlgItemMessageW(h, id, CB_SETCURSEL, n, 0); }
	static void AddItem(HWND h, int id, LPCWSTR text) { SendDlgItemMessageW(h, id, CB_ADDSTRING, 0, LPARAM(text)); }
	static std::wstring Text(HWND h, int id)
	{
		const int length = GetWindowTextLengthW(GetDlgItem(h, id));
		std::wstring value(size_t(length) + 1, 0);
		GetDlgItemTextW(h, id, &value[0], length + 1);
		value.resize(length);
		return value;
	}

	// ---- Rule controls (shared by the add row and the edit dialog) --------

	static Kind KindOf(HWND h) { return Selected(h, IDC_FILTER_KIND) == 1 ? Kind::Keyword : Kind::Author; }

	// Body extent: authors always mask the whole article; keywords pick word or row.
	static void FillExtent(HWND h, Kind kind, bool line)
	{
		SendDlgItemMessageW(h, IDC_FILTER_EXTENT, CB_RESETCONTENT, 0, 0);
		if (kind == Kind::Author) AddItem(h, IDC_FILTER_EXTENT, L"整篇");
		else { AddItem(h, IDC_FILTER_EXTENT, L"關鍵字"); AddItem(h, IDC_FILTER_EXTENT, L"單行"); }
		Select(h, IDC_FILTER_EXTENT, kind == Kind::Keyword && line ? 1 : 0);
	}

	static void UpdateExtent(HWND h)
	{
		EnableWindow(GetDlgItem(h, IDC_FILTER_EXTENT),
			KindOf(h) == Kind::Keyword && IsDlgButtonChecked(h, IDC_FILTER_IN_BODY) == BST_CHECKED);
	}

	static void InitRuleControls(HWND h, const Rule& r)
	{
		for (auto v : kKinds) AddItem(h, IDC_FILTER_KIND, v);
		AddItem(h, IDC_FILTER_EFFECT, L"預設");
		for (auto v : kEffects) AddItem(h, IDC_FILTER_EFFECT, v);
		SendDlgItemMessageW(h, IDC_FILTER_VALUE, EM_LIMITTEXT, 60, 0);
		Select(h, IDC_FILTER_KIND, int(r.kind));
		SetDlgItemTextW(h, IDC_FILTER_VALUE, r.value.c_str());
		for (auto& p : kPlaces) CheckDlgButton(h, p.id, Covers(r.scope, p.scope) ? BST_CHECKED : BST_UNCHECKED);
		FillExtent(h, r.kind, r.line);
		UpdateExtent(h);
		Select(h, IDC_FILTER_EFFECT, int(r.choice));
	}

	static void RuleError(HWND h, LPCWSTR message)
	{
		SetDlgItemTextW(h, IDC_FILTER_ERROR, message);
		if (*message)
		{
			SetFocus(GetDlgItem(h, IDC_FILTER_VALUE));
			SendDlgItemMessageW(h, IDC_FILTER_VALUE, EM_SETSEL, 0, -1);
		}
	}

	// Validates the controls; on failure shows the reason and returns false.
	static bool ReadRule(HWND h, const Settings& s, int except, Rule& r)
	{
		r.kind = KindOf(h);
		r.value = Trim(Text(h, IDC_FILTER_VALUE));
		int scope = 0;
		for (auto& p : kPlaces) if (IsDlgButtonChecked(h, p.id) == BST_CHECKED) scope |= int(p.scope);
		r.scope = Scope(scope);
		r.line = r.kind == Kind::Author || Selected(h, IDC_FILTER_EXTENT) == 1;
		r.choice = Choice((std::max)(0, Selected(h, IDC_FILTER_EFFECT)));
		if (r.value.empty())
			RuleError(h, r.kind == Kind::Author ? L"請輸入帳號。" : L"請輸入關鍵字。");
		else if (!scope)
			RuleError(h, L"請至少勾選一個套用位置（列表、內文或推文）。");
		else if (r.kind == Kind::Author && !ValidAuthor(r.value))
			RuleError(h, L"帳號須為英文字母開頭、2～12 個英文字母或數字。");
		else if (s.Find(r.kind, r.value, except) >= 0)
			RuleError(h, L"清單中已有相同的規則。");
		else return true;
		return false;
	}

	// Handles the commands both rule editors share. Returns true when consumed.
	static bool RuleCommand(HWND h, WPARAM wp)
	{
		if (LOWORD(wp) == IDC_FILTER_KIND && HIWORD(wp) == CBN_SELCHANGE)
		{
			FillExtent(h, KindOf(h), true);
			UpdateExtent(h);
			RuleError(h, L"");
			return true;
		}
		if (LOWORD(wp) == IDC_FILTER_IN_BODY && HIWORD(wp) == BN_CLICKED)
		{
			UpdateExtent(h);
			return true;
		}
		if (LOWORD(wp) == IDC_FILTER_VALUE && HIWORD(wp) == EN_CHANGE)
		{
			SetDlgItemTextW(h, IDC_FILTER_ERROR, L"");
			return true;
		}
		return false;
	}

	// Draws the validation message in red. Returns 0 for other statics.
	static INT_PTR ErrorColor(HWND h, WPARAM wp, LPARAM lp)
	{
		if (HWND(lp) != GetDlgItem(h, IDC_FILTER_ERROR)) return 0;
		SetTextColor(HDC(wp), RGB(200, 0, 0));
		SetBkMode(HDC(wp), TRANSPARENT);
		return INT_PTR(GetSysColorBrush(COLOR_BTNFACE));
	}

	struct RuleEdit { const Settings* settings; int index; Rule rule; };

	static INT_PTR CALLBACK RuleDialog(HWND h, UINT msg, WPARAM wp, LPARAM lp)
	{
		if (msg == WM_INITDIALOG)
		{
			SetWindowLongPtrW(h, DWLP_USER, lp);
			InitRuleControls(h, reinterpret_cast<RuleEdit*>(lp)->rule);
			SetFocus(GetDlgItem(h, IDC_FILTER_VALUE));
			SendDlgItemMessageW(h, IDC_FILTER_VALUE, EM_SETSEL, 0, -1);
			return FALSE;
		}
		if (msg == WM_CTLCOLORSTATIC) return ErrorColor(h, wp, lp);
		if (msg != WM_COMMAND) return FALSE;
		auto& edit = *reinterpret_cast<RuleEdit*>(GetWindowLongPtrW(h, DWLP_USER));
		if (RuleCommand(h, wp)) return TRUE;
		if (LOWORD(wp) == IDOK)
		{
			if (ReadRule(h, *edit.settings, edit.index, edit.rule)) EndDialog(h, IDOK);
			return TRUE;
		}
		if (LOWORD(wp) == IDCANCEL) { EndDialog(h, IDCANCEL); return TRUE; }
		return FALSE;
	}

	// ---- Main dialog -----------------------------------------------------

	enum { kKeepColor = 99, kFirstColor = 100 };

	struct State
	{
		Settings settings;
		int sortColumn = -1;
		bool descending = false;
	};

	static State& StateOf(HWND h) { return *reinterpret_cast<State*>(GetWindowLongPtrW(h, DWLP_USER)); }
	static HWND List(HWND h) { return GetDlgItem(h, IDC_FILTER_LIST); }

	static std::vector<int> SelectedRules(HWND h)
	{
		std::vector<int> result;
		for (int item = -1; (item = int(SendMessageW(List(h), LVM_GETNEXTITEM, item, LVNI_SELECTED))) >= 0;)
		{
			LVITEMW it{};
			it.mask = LVIF_PARAM;
			it.iItem = item;
			SendMessageW(List(h), LVM_GETITEMW, 0, LPARAM(&it));
			result.push_back(int(it.lParam));
		}
		return result;
	}

	static void UpdateButtons(HWND h)
	{
		const auto count = SendMessageW(List(h), LVM_GETSELECTEDCOUNT, 0, 0);
		EnableWindow(GetDlgItem(h, IDC_FILTER_EDIT), count == 1);
		EnableWindow(GetDlgItem(h, IDC_FILTER_REMOVE), count > 0);
	}

	static bool Visible(HWND h, const Rule& r)
	{
		const int show = Selected(h, IDC_FILTER_SHOW);  // 0 all, 1 authors, 2 keywords
		if (show > 0 && int(r.kind) != show - 1) return false;
		const std::wstring query = Trim(Text(h, IDC_FILTER_SEARCH));
		if (query.empty()) return true;
		const auto equal = [](wchar_t a, wchar_t b) { return FoldEnglish(a) == FoldEnglish(b); };
		return std::search(r.value.begin(), r.value.end(), query.begin(), query.end(), equal) != r.value.end();
	}

	static void SetCell(HWND list, int item, int column, const std::wstring& text)
	{
		LVITEMW it{};
		it.iSubItem = column;
		it.pszText = const_cast<LPWSTR>(text.c_str());
		SendMessageW(list, LVM_SETITEMTEXTW, item, LPARAM(&it));
	}

	// Rebuilds the list. `select` holds rule indexes to select afterwards.
	static void Refresh(HWND h, const std::vector<int>& select = {})
	{
		const auto& s = StateOf(h).settings;
		HWND list = List(h);
		SendMessageW(list, WM_SETREDRAW, FALSE, 0);
		SendMessageW(list, LVM_DELETEALLITEMS, 0, 0);
		int shown = 0;
		for (size_t i = 0; i < s.rules.size(); ++i)
		{
			const Rule& r = s.rules[i];
			if (!Visible(h, r)) continue;
			LVITEMW it{};
			it.mask = LVIF_TEXT | LVIF_PARAM;
			it.iItem = shown;
			it.lParam = LPARAM(i);
			it.pszText = const_cast<LPWSTR>(kKinds[int(r.kind)]);
			const int item = int(SendMessageW(list, LVM_INSERTITEMW, 0, LPARAM(&it)));
			SetCell(list, item, 1, r.value);
			SetCell(list, item, 2, ScopeLabel(r.scope));
			SetCell(list, item, 3, ExtentLabel(r));
			SetCell(list, item, 4, EffectLabel(r.choice));
			if (std::find(select.begin(), select.end(), int(i)) != select.end())
			{
				LVITEMW state{};
				state.stateMask = state.state = LVIS_SELECTED | LVIS_FOCUSED;
				SendMessageW(list, LVM_SETITEMSTATE, item, LPARAM(&state));
				SendMessageW(list, LVM_ENSUREVISIBLE, item, FALSE);
			}
			++shown;
		}
		SendMessageW(list, WM_SETREDRAW, TRUE, 0);
		InvalidateRect(list, nullptr, TRUE);
		wchar_t count[64];
		if (size_t(shown) == s.rules.size()) swprintf_s(count, L"共 %d 條規則", shown);
		else swprintf_s(count, L"共 %d 條，顯示 %d 條", int(s.rules.size()), shown);
		SetDlgItemTextW(h, IDC_FILTER_COUNT, count);
		UpdateButtons(h);
	}

	static void Sort(HWND h, int column)
	{
		auto& state = StateOf(h);
		state.descending = state.sortColumn == column && !state.descending;
		state.sortColumn = column;
		const auto& s = state.settings;
		const auto key = [&](const Rule& r) -> std::wstring {
			switch (column)
			{
			case 0: return kKinds[int(r.kind)];
			case 1: { std::wstring v = r.value; for (auto& c : v) c = FoldEnglish(c); return v; }
			case 2: return std::wstring(1, wchar_t(L'0' + 8 - int(r.scope)));
			case 3: return ExtentLabel(r);
			default: return std::wstring(1, wchar_t(L'0' + int(s.Resolve(r.choice)))) + EffectLabel(r.choice);
			}
		};
		const auto selected = SelectedRules(h);
		std::vector<Rule> chosen;
		for (int i : selected) chosen.push_back(state.settings.rules[i]);
		auto& rules = state.settings.rules;
		std::stable_sort(rules.begin(), rules.end(), [&](const Rule& a, const Rule& b) {
			return state.descending ? key(b) < key(a) : key(a) < key(b);
		});
		std::vector<int> reselect;
		for (auto& r : chosen) reselect.push_back(state.settings.Find(r.kind, r.value));
		Refresh(h, reselect);
	}

	static void AddRule(HWND h)
	{
		auto& s = StateOf(h).settings;
		Rule r;
		if (!ReadRule(h, s, -1, r)) return;
		s.rules.push_back(r);
		if (!Visible(h, r))
		{
			SetDlgItemTextW(h, IDC_FILTER_SEARCH, L"");
			Select(h, IDC_FILTER_SHOW, 0);
		}
		Refresh(h, {int(s.rules.size()) - 1});
		SetDlgItemTextW(h, IDC_FILTER_VALUE, L"");
		SetDlgItemTextW(h, IDC_FILTER_ERROR, L"");
		SetFocus(GetDlgItem(h, IDC_FILTER_VALUE));
	}

	static void EditRule(HWND h)
	{
		const auto selected = SelectedRules(h);
		if (selected.size() != 1) return;
		auto& s = StateOf(h).settings;
		RuleEdit edit{&s, selected[0], s.rules[selected[0]]};
		if (DialogBoxParamW(AfxGetResourceHandle(), MAKEINTRESOURCEW(IDD_PTT_FILTER_RULE), h,
			RuleDialog, LPARAM(&edit)) != IDOK) return;
		s.rules[selected[0]] = edit.rule;
		Refresh(h, selected);
		SetFocus(List(h));
	}

	static void RemoveRules(HWND h)
	{
		auto selected = SelectedRules(h);
		if (selected.empty()) return;
		std::sort(selected.rbegin(), selected.rend());
		auto& rules = StateOf(h).settings.rules;
		for (int i : selected) rules.erase(rules.begin() + i);
		Refresh(h);
		SetFocus(List(h));
	}

	// ---- Color pickers -----------------------------------------------------

	static void FillKeep(HDC dc, const RECT& rc)
	{
		const HBRUSH hatch = CreateHatchBrush(HS_BDIAGONAL, RGB(150, 150, 150));
		const COLORREF oldBk = SetBkColor(dc, RGB(255, 255, 255));
		const int oldMode = SetBkMode(dc, OPAQUE);
		FillRect(dc, &rc, hatch);
		SetBkMode(dc, oldMode);
		SetBkColor(dc, oldBk);
		DeleteObject(hatch);
	}

	static void Swatch(HDC dc, RECT rc, int color)
	{
		if (color < 0) FillKeep(dc, rc);
		else
		{
			const HBRUSH brush = CreateSolidBrush(AppConfig.colormap[color]);
			FillRect(dc, &rc, brush);
			DeleteObject(brush);
		}
		FrameRect(dc, &rc, GetSysColorBrush(COLOR_BTNSHADOW));
	}

	static LPCWSTR ColorName(int color) { return color < 0 ? L"保留原色" : kColors[color]; }

	static void PickColor(HWND h, int id)
	{
		auto& s = StateOf(h).settings;
		int& color = id == IDC_FILTER_FG ? s.foreground : s.background;
		HMENU menu = CreatePopupMenu();
		// Two rows of eight swatches, like the ANSI toolbar, then "keep".
		for (int column = 0; column < 8; ++column)
		{
			AppendMenuW(menu, MF_OWNERDRAW | (column ? MF_MENUBREAK : 0), kFirstColor + column, nullptr);
			AppendMenuW(menu, MF_OWNERDRAW, kFirstColor + column + 8, nullptr);
		}
		AppendMenuW(menu, MF_OWNERDRAW | MF_MENUBARBREAK, kKeepColor, nullptr);
		RECT rc;
		GetWindowRect(GetDlgItem(h, id), &rc);
		const UINT chosen = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN | TPM_NONOTIFY,
			rc.left, rc.bottom, 0, h, nullptr);
		DestroyMenu(menu);
		if (!chosen) return;
		color = chosen == kKeepColor ? -1 : int(chosen) - kFirstColor;
		InvalidateRect(GetDlgItem(h, id), nullptr, FALSE);
		InvalidateRect(GetDlgItem(h, IDC_FILTER_PREVIEW), nullptr, FALSE);
	}

	static void DrawMenuSwatch(const DRAWITEMSTRUCT& d)
	{
		const bool selected = (d.itemState & ODS_SELECTED) != 0;
		FillRect(d.hDC, &d.rcItem, GetSysColorBrush(selected ? COLOR_HIGHLIGHT : COLOR_MENU));
		RECT chip = d.rcItem;
		InflateRect(&chip, -3, -3);
		if (d.itemID == kKeepColor)
		{
			RECT box = chip;
			box.right = box.left + (box.bottom - box.top);
			Swatch(d.hDC, box, -1);
			RECT text = chip;
			text.left = box.right + 6;
			SetBkMode(d.hDC, TRANSPARENT);
			SetTextColor(d.hDC, GetSysColor(selected ? COLOR_HIGHLIGHTTEXT : COLOR_MENUTEXT));
			DrawTextW(d.hDC, L"保留原色", -1, &text, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
		}
		else Swatch(d.hDC, chip, int(d.itemID) - kFirstColor);
	}

	static void DrawColorButton(HWND h, const DRAWITEMSTRUCT& d)
	{
		const auto& s = StateOf(h).settings;
		const int color = d.CtlID == IDC_FILTER_FG ? s.foreground : s.background;
		RECT rc = d.rcItem;
		DrawFrameControl(d.hDC, &rc, DFC_BUTTON, DFCS_BUTTONPUSH | (d.itemState & ODS_SELECTED ? DFCS_PUSHED : 0));
		InflateRect(&rc, -4, -3);
		RECT chip = rc;
		chip.right = chip.left + (chip.bottom - chip.top);
		Swatch(d.hDC, chip, color);
		RECT text = rc;
		text.left = chip.right + 4;
		SetBkMode(d.hDC, TRANSPARENT);
		SetTextColor(d.hDC, GetSysColor(IsWindowEnabled(d.hwndItem) ? COLOR_BTNTEXT : COLOR_GRAYTEXT));
		DrawTextW(d.hDC, ColorName(color), -1, &text, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
		text.left = text.right - 8;
		DrawTextW(d.hDC, L"▼", -1, &text, DT_SINGLELINE | DT_VCENTER | DT_RIGHT);
		if (d.itemState & ODS_FOCUS)
		{
			RECT focus = d.rcItem;
			InflateRect(&focus, -2, -2);
			DrawFocusRect(d.hDC, &focus);
		}
	}

	// Shows the color mask over plain text and over text that already has a background.
	static void DrawPreview(HWND h, const DRAWITEMSTRUCT& d)
	{
		const auto& s = StateOf(h).settings;
		const COLORREF* map = AppConfig.colormap;
		const HBRUSH black = CreateSolidBrush(map[0]);
		FillRect(d.hDC, &d.rcItem, black);
		DeleteObject(black);
		const HFONT old = HFONT(SelectObject(d.hDC, HFONT(SendMessageW(h, WM_GETFONT, 0, 0))));
		const int height = (d.rcItem.bottom - d.rcItem.top) / 2;
		const COLORREF originalBk[2] = {map[0], map[4]};
		for (int line = 0; line < 2; ++line)
		{
			RECT row = d.rcItem;
			row.top += line * height;
			row.bottom = row.top + height;
			row.left += 4;
			SIZE mark;
			GetTextExtentPoint32W(d.hDC, L"□ ", 2, &mark);
			SetBkMode(d.hDC, OPAQUE);
			SetBkColor(d.hDC, originalBk[line]);
			SetTextColor(d.hDC, map[7]);
			DrawTextW(d.hDC, L"□ ", 2, &row, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOCLIP);
			row.left += mark.cx;
			SetTextColor(d.hDC, s.foreground >= 0 ? map[s.foreground] : map[7]);
			SetBkColor(d.hDC, s.background >= 0 ? map[s.background] : originalBk[line]);
			DrawTextW(d.hDC, L"範例標題", -1, &row, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOCLIP);
		}
		SelectObject(d.hDC, old);
	}

	// ---- Dialog procedure ------------------------------------------------

	static void InitList(HWND h)
	{
		HWND list = List(h);
		SendMessageW(list, LVM_SETEXTENDEDLISTVIEWSTYLE, 0,
			LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
		RECT rc;
		GetClientRect(list, &rc);
		const int width = rc.right - GetSystemMetrics(SM_CXVSCROLL);
		const struct { LPCWSTR title; int share; } columns[] = {
			{L"類型", 12}, {L"內容", 32}, {L"套用於", 24}, {L"內文遮罩", 16}, {L"效果", 16}};
		for (int i = 0; i < 5; ++i)
		{
			LVCOLUMNW c{};
			c.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
			c.pszText = const_cast<LPWSTR>(columns[i].title);
			c.cx = width * columns[i].share / 100;
			c.iSubItem = i;
			SendMessageW(list, LVM_INSERTCOLUMNW, i, LPARAM(&c));
		}
	}

	static INT_PTR CALLBACK Dialog(HWND h, UINT msg, WPARAM wp, LPARAM lp)
	{
		switch (msg)
		{
		case WM_INITDIALOG:
		{
			SetWindowLongPtrW(h, DWLP_USER, lp);
			const auto& s = reinterpret_cast<State*>(lp)->settings;
			SendDlgItemMessageW(h, IDC_FILTER_SEARCH, EM_SETCUEBANNER, TRUE, LPARAM(L"搜尋清單"));
			for (auto v : {L"全部類型", L"帳號", L"關鍵字"}) AddItem(h, IDC_FILTER_SHOW, v);
			Select(h, IDC_FILTER_SHOW, 0);
			for (auto v : kEffects) AddItem(h, IDC_FILTER_DEFAULT, v);
			Select(h, IDC_FILTER_DEFAULT, int(s.defaultEffect) - 1);
			InitRuleControls(h, Rule{Kind::Author});
			SendDlgItemMessageW(h, IDC_FILTER_VALUE, EM_SETCUEBANNER, TRUE, LPARAM(L"帳號或關鍵字"));
			InitList(h);
			Refresh(h);
			SetFocus(GetDlgItem(h, IDC_FILTER_VALUE));
			return FALSE;
		}
		case WM_CTLCOLORSTATIC:
			return ErrorColor(h, wp, lp);
		case WM_MEASUREITEM:
		{
			auto& m = *reinterpret_cast<MEASUREITEMSTRUCT*>(lp);
			if (m.CtlType != ODT_MENU) return FALSE;
			const UINT unit = GetSystemMetrics(SM_CYMENUCHECK) + 8;
			m.itemHeight = unit;
			m.itemWidth = m.itemID == kKeepColor ? unit * 4 : unit - GetSystemMetrics(SM_CXMENUCHECK);
			return TRUE;
		}
		case WM_DRAWITEM:
		{
			const auto& d = *reinterpret_cast<DRAWITEMSTRUCT*>(lp);
			if (d.CtlType == ODT_MENU) DrawMenuSwatch(d);
			else if (d.CtlID == IDC_FILTER_PREVIEW) DrawPreview(h, d);
			else if (d.CtlID == IDC_FILTER_FG || d.CtlID == IDC_FILTER_BG) DrawColorButton(h, d);
			else return FALSE;
			return TRUE;
		}
		case WM_NOTIFY:
		{
			const auto& n = *reinterpret_cast<NMHDR*>(lp);
			if (n.idFrom != IDC_FILTER_LIST) return FALSE;
			switch (n.code)
			{
			case NM_DBLCLK: EditRule(h); return TRUE;
			case LVN_ITEMCHANGED: UpdateButtons(h); return TRUE;
			case LVN_COLUMNCLICK: Sort(h, reinterpret_cast<const NMLISTVIEW*>(lp)->iSubItem); return TRUE;
			case LVN_KEYDOWN:
			{
				const WORD key = reinterpret_cast<const NMLVKEYDOWN*>(lp)->wVKey;
				if (key == VK_DELETE) RemoveRules(h);
				else if (key == 'A' && GetKeyState(VK_CONTROL) < 0)
				{
					LVITEMW all{};
					all.stateMask = all.state = LVIS_SELECTED;
					SendMessageW(List(h), LVM_SETITEMSTATE, WPARAM(-1), LPARAM(&all));
				}
				return TRUE;
			}
			}
			return FALSE;
		}
		case WM_COMMAND:
			break;
		default:
			return FALSE;
		}

		auto& s = StateOf(h).settings;
		if (RuleCommand(h, wp)) return TRUE;
		switch (LOWORD(wp))
		{
		case IDC_FILTER_SEARCH:
			if (HIWORD(wp) == EN_CHANGE) Refresh(h, SelectedRules(h));
			return TRUE;
		case IDC_FILTER_SHOW:
			if (HIWORD(wp) == CBN_SELCHANGE) Refresh(h, SelectedRules(h));
			return TRUE;
		case IDC_FILTER_DEFAULT:
			if (HIWORD(wp) != CBN_SELCHANGE) return FALSE;
			s.defaultEffect = Effect(Selected(h, IDC_FILTER_DEFAULT) + 1);
			return TRUE;
		case IDC_FILTER_ADD: AddRule(h); return TRUE;
		case IDC_FILTER_EDIT: EditRule(h); return TRUE;
		case IDC_FILTER_REMOVE: RemoveRules(h); return TRUE;
		case IDC_FILTER_FG:
		case IDC_FILTER_BG:
			if (HIWORD(wp) == BN_CLICKED) PickColor(h, LOWORD(wp));
			return TRUE;
		case IDOK:
		{
			// Enter acts on the control that has focus; only the button saves.
			const HWND focus = GetFocus();
			for (int id : {IDC_FILTER_VALUE, IDC_FILTER_KIND, IDC_FILTER_EXTENT, IDC_FILTER_EFFECT})
				if (focus == GetDlgItem(h, id)) { AddRule(h); return TRUE; }
			if (focus == List(h)) { EditRule(h); return TRUE; }
			if (focus == GetDlgItem(h, IDC_FILTER_SEARCH) || focus == GetDlgItem(h, IDC_FILTER_SHOW)) return TRUE;
			if (!Save(s)) { MessageBoxW(h, L"無法儲存過濾設定。", L"PTT 名單與關鍵字", MB_ICONERROR); return TRUE; }
			EndDialog(h, IDOK);
			return TRUE;
		}
		case IDCANCEL: EndDialog(h, IDCANCEL); return TRUE;
		}
		return FALSE;
	}

	bool Edit(HWND owner)
	{
		INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_LISTVIEW_CLASSES};
		InitCommonControlsEx(&icc);
		State state;
		state.settings = Shared();
		if (DialogBoxParamW(AfxGetResourceHandle(), MAKEINTRESOURCEW(IDD_PTT_FILTER), owner,
			Dialog, LPARAM(&state)) != IDOK) return false;
		Shared() = std::move(state.settings);
		return true;
	}
}
