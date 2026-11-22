#include "stdafx.h"
#include "PttFilterDialog.h"
#include "../Resource/resource.h"

namespace PttFilter
{
	static CString Path() { return ConfigPath + "PttFilter.dat"; }

	static void ReadRules(CArchive& ar, std::vector<Rule>& rules)
	{
		UINT count;
		ar >> count;
		for (UINT i = 0; i < count; ++i)
		{
			CStringW value;
			BYTE scope, choice, exact;
			ar >> value >> scope >> choice >> exact;
			if (scope >= 1 && scope <= 3 && choice <= 3 && !value.IsEmpty())
				rules.push_back({value.GetString(), Scope(scope), Choice(choice), exact != 0});
		}
	}

	static Settings Load()
	{
		try
		{
			CFile f;
			if (!f.Open(Path(), CFile::modeRead | CFile::shareDenyWrite)) return {};
			CArchive ar(&f, CArchive::load);
			UINT version;
			int effect, fg, bg;
			ar >> version >> effect >> fg >> bg;
			if (version != 1 || effect < 1 || effect > 3 || fg < -1 || fg > 15 || bg < -1 || bg > 15) return {};
			Settings s;
			s.defaultEffect = Effect(effect);
			s.foreground = fg;
			s.background = bg;
			ReadRules(ar, s.authors);
			ReadRules(ar, s.keywords);
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
			ar << UINT(1) << int(s.defaultEffect) << s.foreground << s.background;
			for (auto rules : {&s.authors, &s.keywords})
			{
				ar << UINT(rules->size());
				for (auto& r : *rules)
					ar << CStringW(r.value.c_str()) << BYTE(r.scope) << BYTE(r.choice) << BYTE(r.exact);
			}
			ar.Close();
			f.Close();
			if (MoveFileEx(tmp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return true;
		}
		catch (CException* e) { e->Delete(); }
		DeleteFile(tmp);
		return false;
	}

	static int Selected(HWND h, int id) { return int(SendDlgItemMessageW(h, id, CB_GETCURSEL, 0, 0)); }
	static void Select(HWND h, int id, int n) { SendDlgItemMessageW(h, id, CB_SETCURSEL, n, 0); }
	static void Add(HWND h, int id, LPCWSTR text) { SendDlgItemMessageW(h, id, CB_ADDSTRING, 0, LPARAM(text)); }

	static INT_PTR CALLBACK Dialog(HWND h, UINT msg, WPARAM wp, LPARAM lp)
	{
		if (msg == WM_INITDIALOG)
		{
			SetWindowLongPtrW(h, DWLP_USER, lp);
			auto& s = *reinterpret_cast<Settings*>(lp);
			for (auto v : {L"帳號黑名單", L"關鍵字"}) Add(h, IDC_FILTER_KIND, v);
			for (auto v : {L"文章", L"推文", L"文章與推文"}) Add(h, IDC_FILTER_SCOPE, v);
			Add(h, IDC_FILTER_EFFECT, L"跟隨預設");
			for (auto v : {L"標色", L"等寬星號", L"隱藏"})
				for (int id : {IDC_FILTER_EFFECT, IDC_FILTER_DEFAULT}) Add(h, id, v);
			for (int id : {IDC_FILTER_FG, IDC_FILTER_BG})
				for (auto v : {L"保留原色",L"黑",L"紅",L"綠",L"黃",L"藍",L"紫",L"青",L"白",
					L"灰",L"亮紅",L"亮綠",L"亮黃",L"亮藍",L"亮紫",L"亮青",L"亮白"}) Add(h, id, v);
			Select(h, IDC_FILTER_DEFAULT, int(s.defaultEffect)-1);
			Select(h, IDC_FILTER_FG, s.foreground+1);
			Select(h, IDC_FILTER_BG, s.background+1);
			Select(h, IDC_FILTER_KIND, 0);
		}
		else if (msg != WM_COMMAND) return FALSE;
		auto& s = *reinterpret_cast<Settings*>(GetWindowLongPtrW(h, DWLP_USER));
		const bool keyword = Selected(h, IDC_FILTER_KIND) == 1;
		auto& rules = keyword ? s.keywords : s.authors;
		const int selected = int(SendDlgItemMessageW(h, IDC_FILTER_LIST, LB_GETCURSEL, 0, 0));
		const bool valid = selected >= 0 && size_t(selected) < rules.size();
		int index = -1;
		switch (msg == WM_INITDIALOG ? IDC_FILTER_KIND : LOWORD(wp))
		{
		case IDC_FILTER_KIND:
			if (msg != WM_INITDIALOG && HIWORD(wp) != CBN_SELCHANGE) return FALSE;
			break;
		case IDC_FILTER_LIST:
			if (HIWORD(wp) != LBN_SELCHANGE || !valid) return FALSE;
			index = selected;
			break;
		case IDC_FILTER_ADD:
		case IDC_FILTER_UPDATE:
		{
			int length = GetWindowTextLengthW(GetDlgItem(h, IDC_FILTER_VALUE));
			std::wstring value(size_t(length)+1, 0);
			GetDlgItemTextW(h, IDC_FILTER_VALUE, &value[0], length+1);
			value.resize(length);
			if (value.find_first_not_of(L' ') == value.npos) { SetFocus(GetDlgItem(h, IDC_FILTER_VALUE)); return TRUE; }
			const auto start = value.find_first_not_of(L' ');
			value = value.substr(start, value.find_last_not_of(L' ') + 1 - start);
			Rule r{value, Scope(Selected(h, IDC_FILTER_SCOPE)+1), Choice(Selected(h, IDC_FILTER_EFFECT)),
				keyword && IsDlgButtonChecked(h, IDC_FILTER_EXACT) == BST_CHECKED};
			if (LOWORD(wp) == IDC_FILTER_ADD) { index = int(rules.size()); rules.push_back(r); }
			else if (valid) { rules[selected] = r; index = selected; }
			break;
		}
		case IDC_FILTER_REMOVE:
			if (valid) rules.erase(rules.begin()+selected);
			break;
		case IDOK:
			s.defaultEffect = Effect(Selected(h, IDC_FILTER_DEFAULT)+1);
			s.foreground = Selected(h, IDC_FILTER_FG)-1;
			s.background = Selected(h, IDC_FILTER_BG)-1;
			if (!Save(s)) { MessageBoxW(h, L"無法儲存過濾設定。", L"PTT 黑名單", MB_ICONERROR); return TRUE; }
			EndDialog(h, IDOK); return TRUE;
		case IDCANCEL: EndDialog(h, IDCANCEL); return TRUE;
		default: return FALSE;
		}
		SendDlgItemMessageW(h, IDC_FILTER_LIST, LB_RESETCONTENT, 0, 0);
		for (auto& r : rules) SendDlgItemMessageW(h, IDC_FILTER_LIST, LB_ADDSTRING, 0, LPARAM(r.value.c_str()));
		SendDlgItemMessageW(h, IDC_FILTER_LIST, LB_SETCURSEL, index, 0);
		const Rule r = index >= 0 ? rules[index] : Rule{};
		SetDlgItemTextW(h, IDC_FILTER_VALUE, r.value.c_str());
		Select(h, IDC_FILTER_SCOPE, int(r.scope)-1);
		Select(h, IDC_FILTER_EFFECT, int(r.choice));
		CheckDlgButton(h, IDC_FILTER_EXACT, !keyword || r.exact ? BST_CHECKED : BST_UNCHECKED);
		EnableWindow(GetDlgItem(h, IDC_FILTER_EXACT), keyword);
		for (int id : {IDC_FILTER_UPDATE, IDC_FILTER_REMOVE}) EnableWindow(GetDlgItem(h, id), index >= 0);
		return TRUE;
	}

	bool Edit(HWND owner)
	{
		Settings draft = Shared();
		if (DialogBoxParamW(AfxGetResourceHandle(), MAKEINTRESOURCEW(IDD_PTT_FILTER), owner,
			Dialog, LPARAM(&draft)) != IDOK) return false;
		Shared() = std::move(draft);
		return true;
	}
}
