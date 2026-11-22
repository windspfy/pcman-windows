// TelnetConn.cpp : implementation file
//

#include <algorithm>

#include "stdafx.h"
#include "TelnetConn.h"

#include "TermView.h"
#include "listdlg.h"
#include "mainfrm.h"
#include "StrUtils.h"
#include "WinUtils.h"
#include "Clipboard.h"
#include "WindowsTerminalRowDecoder.h"
extern CUcs2Conv g_ucs2conv;
#include "Websocket.h"

#include <mmsystem.h>

/*
typedef void (CTelnetConn::*ansi_func)(int);
typedef void (CTelnetConn::*ansi_func2)(int,int);
typedef void (CTelnetConn::*ansi_funcs)(CString);
struct ANSI_TAB
{
	char code;
	char param_type;	//-1表示直接傳入LPCTSTR
	char d1;	//-1表示無效
	char d2;	//-1表示無效
	ansi_func pfunc;	//ansi function
};

struct ANSI_TAB ansi_tab[]={
	{'m',-1,-1,-1,(ansi_func)CTelnetConn::SetCurrentAttributes},	//color
	{'K',1,0,-1,CTelnetConn::ClearCurrentLine},	//clear current line
	{'H',2,1,1,(ansi_func)CTelnetConn::GotoXY},	//move cursor
	{'F',2,1,1,(ansi_func)CTelnetConn::GotoXY},	//move cursor
	{'r',2,-1,-1,(ansi_func)CTelnetConn::SetScrollRange},	//set scroll range
	{'J',1,-1,-1,CTelnetConn::ClearScreen},	//clear screen
	{'E',1,1,-1,CTelnetConn::LineFeed},	//line feed
	{'L',1,1,-1,CTelnetConn::InsertLines},	//insert line
	{'A',1,1,-1,CTelnetConn::GoUp},	//up
	{'B',1,1,-1,CTelnetConn::GoDown},	//down
	{'C',1,1,-1,CTelnetConn::GoRight},	//right
	{'D',1,1,-1,CTelnetConn::GoLeft},	//left
	//新增的ANSI支援
	{'s',1,-1,-1,CTelnetConn::SaveCursorPos},
	{'u',1,-1,-1,CTelnetConn::RestoreCursorPos},
	{'@',1,1,-1,CTelnetConn::InsertChar},	//insert char
	{'M',1,1,-1,CTelnetConn::DeleteLines},	//delete n line
	{'P',1,1,-1,CTelnetConn::DeleteChar},	//delete char
	{'U',1,1,-1,CTelnetConn::NextPages},	//next n page
	{'V',1,1,-1,CTelnetConn::PrevPages},	//previous n page
	{'Z',1,1,-1,CTelnetConn::BackTab},	//cursor back tab
	{'h',1,-1,-1,CTelnetConn::SetMode},	//set mode
	{'l',1,-1,-1,CTelnetConn::ResetMode},	//reset mode
	{'n',1,-1,-1,CTelnetConn::DeviceStatusReport},	//Device Status Report
	{0,0,-1,-1,0}	//null
};
*/

/////////////////////////////////////////////////////////////////////////////
// CTelnetConn

BYTE CTelnetConn::buffer[];
CString CTelnetConn::downloaded_article;
int CTelnetConn::current_download_line;
std::atomic<uint64_t> CTelnetConn::connection_counter;

CTelnetConn::CTelnetConn()
	: connection_id(++connection_counter)
{
	screen = NULL;
	idle_time = 0;
	time = 0;
	cur_attr = 7;
	attr_flags = 0;

	pansi_param = ansi_param;
	insert_mode = 1;

	is_getting_article = 0;
	get_article_with_ansi = 0;
	get_article_in_editor = 0;

	old_cursor_pos.x = old_cursor_pos.y = cursor_pos.x = cursor_pos.y = 0;

	is_lookup_host = true;
	is_telnet = true;

	scr_top = 0;
	scr_bottom = 23;

	buf = NULL;
	last_byte = NULL;

	sel_start.x = 0;
	sel_start.y = 0;
	sel_end.x = 0;
	sel_end.y = 0;
	sel_block = false;

	site_settings.localecho = 0;
	*ansi_param = 0;
	pansi_param = ansi_param;

	site_settings.paste_autowrap = 0;

	key_map = NULL;
}

CTelnetConn::~CTelnetConn()
{
	if (is_connected)
		Shutdown();
	Close();

	for (int i = 0; i < CTermView::all_telnet_conns.GetSize(); i++)
	{
		if (CTermView::all_telnet_conns[i] == (void*)this)
		{
			CTermView::all_telnet_conns.RemoveAt(i);
			break;
		}
	}

	if (screen)
	{
		for (long i = 0;i < site_settings.line_count;i++)
			delete []screen[i];
		delete []screen;
	}

	if (key_map)
		key_map->Release();

	if (site_settings.text_input_conv || site_settings.text_output_conv)
		view->chi_conv.Release();

}


/////////////////////////////////////////////////////////////////////////////
// CTelnetConn member functions

int CTelnetConn::Close()
{
	if (conn_io)
		conn_io->Close();
	return 0;
}

// Delegate implementation to send connection info to window.
class CWindowMessageConnEventDelegate : public CConnEventDelegate
{
public:
	CWindowMessageConnEventDelegate(HWND handle, UINT msg, uint64_t connection_id)
		: handle_(handle), msg_(msg), connection_id_(connection_id)
	{}

	void OnReceive(const void* data, size_t length) override
	{
		auto event = CConnEvent::MakeUnique(connection_id_, CConnEvent::EVENT_DATA);
		event->data.assign((const char*)data, length);
		PostMessage(handle_, msg_, (WPARAM)event.release(), (LPARAM)0);
	}

	void OnConnect(bool success) override
	{
		auto event = CConnEvent::MakeUnique(
			connection_id_,
			success ? CConnEvent::EVENT_CONNECT : CConnEvent::EVENT_CONNECT_FAILED);
		PostMessage(handle_, msg_, (WPARAM)event.release(), (LPARAM)0);
	}

	void OnClose() override
	{
		auto event = CConnEvent::MakeUnique(connection_id_, CConnEvent::EVENT_CLOSE);
		PostMessage(handle_, msg_, (WPARAM)event.release(), (LPARAM)0);
	}

private:
	HWND handle_;
	UINT msg_;
	uint64_t connection_id_;
};

void CTelnetConn::Connect(sockaddr *addr, int len)
{
	auto delegate = std::make_shared<CWindowMessageConnEventDelegate>(
		view->m_hWnd, WM_CONN_EVENT, GetConnectionID());
	tcp_socket = std::make_shared<CTcpSocket>(*addr, len, delegate);
	conn_io = tcp_socket;

	// Start select before connect, ensuring FD_CONNECT will be received.
	WSAAsyncSelect(tcp_socket->GetSocket(), view->m_hWnd, WM_SOCKET, FD_READ | FD_CLOSE | FD_CONNECT);
	tcp_socket->Connect();
}

void CTelnetConn::ConnectWebsocket()
{
	auto delegate = std::make_shared<CWindowMessageConnEventDelegate>(
		view->m_hWnd, WM_CONN_EVENT, GetConnectionID());
	conn_io = CreateWebsocket(address, delegate);
	conn_io->Connect();
}

#ifndef SD_SEND
#define	SD_SEND	1
#endif

int CTelnetConn::Shutdown()
{
	if (conn_io)
		conn_io->Shutdown();
	return 0;
}

void CTelnetConn::HandleConnEvent(std::unique_ptr<CConnEvent> event)
{
	if (source_limit_stopped) return;
	switch (event->event_type) {
	case CConnEvent::EVENT_CONNECT:
		OnConnect(true);
		break;

	case CConnEvent::EVENT_CONNECT_FAILED:
		OnConnect(false);
		break;

	case CConnEvent::EVENT_CLOSE:
		OnClose();
		break;

	case CConnEvent::EVENT_DATA:
		for (size_t i = 0; i < event->data.size(); i += sizeof(buffer)) {
			size_t s = min(event->data.size() - i, sizeof(buffer));
			memcpy(buffer, event->data.data() + i, s);
			OnReceive(s);
		}
		break;
	}
}

void CTelnetConn::OnSocket(WPARAM wparam, LPARAM lparam)
{
	if (!tcp_socket)
		return;
	tcp_socket->OnSocketMessage(wparam, lparam);
}

void CTelnetConn::OnReceive(int len)
{
	if (source_limit_stopped) return;
	if (view->telnet != this)
	{
		int idx = view->parent->ConnToIndex(this);
		TCITEM tcitem;
		tcitem.mask = TCIF_IMAGE;
		view->parent->tab.GetItem(idx, &tcitem);

		if (tcitem.iImage != 5)
		{
			tcitem.iImage = 2;
			view->parent->tab.SetItem(idx, &tcitem);
		}
	}
	else
	{
		SCROLLINFO info;
		view->GetScrollInfo(SB_VERT, &info);
		if (info.nPos != (int)(info.nMax - info.nPage))
		{
			scroll_pos = info.nPos = (info.nMax - info.nPage);
			info.fMask = SIF_POS;
			view->SetScrollInfo(SB_VERT, &info);
			view->Invalidate(FALSE);
		}
	}
	ProcessData(len);
}

inline void CTelnetConn::OnIAC()
{
	BYTE ret[24];
	const int iacLen = int(pansi_param - ansi_param);
	BYTE* pbuf = (BYTE*)ansi_param + 1;
	// Both responses acknowledge a keep-alive. Never reply DONT or display
	// these protocol bytes, even when no keep-alive is currently enabled.
	if (iacLen >= 3 && KeepAlive::IsTimingMarkReply(pbuf[0], pbuf[1]))
		return;
	switch (*pbuf)
	{
	case WILL:
		pbuf++;
		ret[0] = IAC;
		ret[2] = *buf;
		switch (*pbuf)
		{
		case ECHO:
		case SUPRESS_GO_AHEAD:
			ret[1] = DO;
			break;
		default:
			ret[1] = DONT;
		}
		Send(ret, 3);
		break;
	case WONT:
		return;
	case DO:
		pbuf++;
		ret[0] = IAC;
		ret[2] = *buf;
		switch (*pbuf)
		{
		case TERMINAL_TYPE:
			ret[1] = WILL;
			Send(ret, 3);
			break;
		case NAWS:
			ret[1] = WILL;
			Send(ret, 3);
			SendNaws();
			break;
		default:
			ret[1] = WONT;
			Send(ret, 3);
		}
		break;
	case DONT:
		break;
	case SB:
		{
			pbuf++;
			switch (*pbuf)
			{
			case TERMINAL_TYPE:
				pbuf++;
				if (*pbuf == SEND)
				{
					int l = site_settings.termtype.GetLength() + 6;
					BYTE *type = new BYTE[l];
					type[0] = IAC;	type[1] = SB;	type[2] = TERMINAL_TYPE;	type[3] = IS;
					strcpy((char*)type + 4, LPCTSTR(site_settings.termtype));	type[l-2] = IAC;	type[l-1] = SE;
					Send(type, l);
					delete []type;
				}
				break;
			default:
				break;
			}	//end switch
			return;
		}
	default:
		return;
	}
}

class CDownloadArticleDlg : public CDialog
{
public:
	CDownloadArticleDlg(CTelnetConn* _telnet)
			: CDialog(IDD_DOWNLOADING), telnet(_telnet)
	{
	}

	BOOL OnInitDialog()
	{
		CDialog::OnInitDialog();
		return TRUE;
	}

	void OnCancel()
	{
		telnet->CopyArticleComplete(true);
		CDialog::OnCancel();
	}

	void PostNcDestroy();
protected:
	CTelnetConn* telnet;
};

static CDownloadArticleDlg* download_article_dlg = NULL;

void CDownloadArticleDlg::PostNcDestroy()
{
	CDialog::PostNcDestroy();
	if (download_article_dlg)
	{
		download_article_dlg = NULL;
		delete this;
	}
}

void CTelnetConn::StopForSourceLimit()
{
	if (source_limit_stopped) return;
	source_limit_stopped = true;
	is_connected = false;
	is_disconnected = true;
	Close();
	view->Invalidate(FALSE);
	view->parent->UpdateStatus();
}

void CTelnetConn::WriteReceivedGlyph(BYTE value, BYTE attr, const OscHyperlinks::Target& link, bool pair)
{
	if (source_limit_stopped) return;
	if (cursor_pos.y > last_line || cursor_pos.y < first_line) return;
	const int cols = site_settings.cols_per_page;
	const int width = pair ? 2 : 1;
	auto margin = TerminalCells::RightMargin::Clip;
	if (!is_ansi_editor)
	{
		if (remote_line_wrap == 0) margin = TerminalCells::RightMargin::Overwrite;
		else if (remote_line_wrap > 0 || site_settings.line_wrap) margin = TerminalCells::RightMargin::Wrap;
	}
	const auto placement = TerminalCells::PlaceGlyph(cols, cursor_pos.x, width, margin);
	if (placement.column < 0) return;
	if (placement.clearEdge)
	{
		char* edge = screen[cursor_pos.y];
		int column = 0;
		while (column < cols - 1)
		{
			const BYTE next = static_cast<BYTE>(edge[column + 1]);
			const bool wide = CUcs2Conv::IsBig5Pair(static_cast<BYTE>(edge[column]), next);
			if (wide && column + 1 == cols - 1) break;
			column += wide ? 2 : 1;
		}
		if (!encoded_rows.At(edge).Bytes().empty())
		{
			const TerminalCells::EncodedRow::Byte blanks[2] = {{' ', {attr, 0}}, {' ', {attr, 0}}};
			if (!encoded_rows.TryEdit(edge, [&](const TerminalCells::EncodedRow& source) {
				return source.WriteByteCells(column, blanks, cols - column, true);
			})) return StopForSourceLimit();
		}
		memset(edge + column, ' ', cols - column);
		memset(GetLineAttr(edge) + column, attr, cols - column);
		osc_links.Erase(edge, column, cols - column);
		SetUpdateWholeLine(cursor_pos.y);
	}
	if (placement.lineFeed) LineFeed();
	LPSTR curstr = screen[cursor_pos.y];
	LPBYTE pcuratb = GetLineAttr(curstr);
	const int column = placement.column;
	if (!is_ansi_editor && !encoded_rows.At(curstr).Bytes().empty())
	{
		const TerminalCells::EncodedRow::Byte cells[2] = {
			{pair ? pending_big5 : value, {pair ? pending_big5_attr : attr, 0}},
			{value, {attr, 0}}
		};
		if (!encoded_rows.TryEdit(curstr, [&](const TerminalCells::EncodedRow& source) {
			return source.WriteByteCells(column, cells, width, legacy_run_start || placement.lineFeed);
		})) return StopForSourceLimit();
	}
	if (is_ansi_editor && insert_mode)
	{
		osc_links.Copy(curstr, column + 1, curstr, column,
			site_settings.cols_per_page - column - 1, site_settings.cols_per_page);
		for (int i = site_settings.cols_per_page - 1; i > column; i--)
		{
			curstr[i] = curstr[i-1];
			pcuratb[i] = pcuratb[i-1];
		}
		SetUpdateWholeLine(cursor_pos.y);
	}
	for (int i = 0; i < width; ++i)
	{
		const bool lead = pair && i == 0;
		const BYTE byte = lead ? pending_big5 : value;
		const BYTE color = lead ? pending_big5_attr : attr;
		const int x = column + i;
		const bool changedLink = osc_links.Write(curstr, x, cols, lead ? pending_big5_link : link);
		if (static_cast<BYTE>(curstr[x]) != byte || pcuratb[x] != color || changedLink)
			SetUpdateLine(cursor_pos.y, static_cast<BYTE>(x));
		pcuratb[x] = color;
		curstr[x] = byte;
	}
	cursor_pos.x = placement.nextColumn;
	legacy_run_start = false;
}

void CTelnetConn::FinishBig5Text()
{
	if (!pending_big5) return;
	WriteReceivedGlyph(pending_big5, pending_big5_attr, pending_big5_link, false);
	pending_big5 = 0;
	pending_big5_link.reset();
}

TerminalCells::EncodedRow CTelnetConn::ReadUtf8Source(const char* row)
{
	if (sync_output.Active())
		if (const auto* snapshot = sync_output.EncodedSource(row)) return *snapshot;
	const auto& stored = encoded_rows.At(row);
	if (!stored.Bytes().empty()) return stored;
	const BYTE* attrs = GetLineAttr(row);
	int length = site_settings.cols_per_page;
	while (length && row[length-1] == ' ' && attrs[length-1] == 7) --length;
	std::vector<TerminalCells::EncodedRow::Byte> bytes;
	bytes.reserve(length);
	for (int i = 0; i < length; ++i)
		bytes.push_back({static_cast<BYTE>(row[i]), {attrs[i], 0}});
	return TerminalCells::EncodedRow(std::move(bytes));
}

bool CTelnetConn::HasUtf8Blink(const char* row)
{
	const auto& bytes = encoded_rows.At(row).Bytes();
	if (!bytes.empty())
		return std::any_of(bytes.begin(), bytes.end(), [](const auto& b) { return IsAttrBlink(b.style.attribute); });
	const BYTE* attrs = GetLineAttr(row);
	return std::any_of(attrs, attrs + site_settings.cols_per_page, [](BYTE attr) { return IsAttrBlink(attr); });
}

void CTelnetConn::MirrorUtf8Row(char* row)
{
	const int cols = site_settings.cols_per_page;
	const auto& bytes = encoded_rows.At(row).Bytes();
	BYTE* attrs = GetLineAttr(row);
	for (int i = 0; i < cols; ++i)
	{
		const bool present = static_cast<size_t>(i) < bytes.size();
		row[i] = present ? bytes[i].value : ' ';
		attrs[i] = present ? bytes[i].style.attribute : 7;
	}
}

void CTelnetConn::WriteUtf8Glyph(const TerminalCells::EncodedTextDecoder::Glyph& first,
	const OscHyperlinks::Target& firstLink, const TerminalCells::EncodedTextDecoder::Glyph* nextGlyph)
{
	if (source_limit_stopped) return;
	const int cols = site_settings.cols_per_page;
	const bool full = scr_top == 0 && scr_bottom == site_settings.lines_per_page - 1;
	const int top = full ? 0 : first_line + scr_top, bottom = first_line + scr_bottom;
	auto margin = TerminalCells::RightMargin::Clip;
	if (!is_ansi_editor)
	{
		if (remote_line_wrap == 0) margin = TerminalCells::RightMargin::Overwrite;
		else if (remote_line_wrap > 0 || site_settings.line_wrap) margin = TerminalCells::RightMargin::Wrap;
	}
	struct Update {
		TerminalCells::GlyphPlacement placement;
		char* edge = nullptr;
		char* row = nullptr;
		int shifted = 0;
		OscHyperlinks::Target link;
	};
	Update updates[2];
	const int count = nextGlyph ? 2 : 1;
	auto pos = cursor_pos;
	int rotations = 0;
	std::map<const void*, TerminalCells::EncodedRow> staged;
	auto physical = [&](int y) {
		if (y >= top && y <= bottom) y = top + (y - top + rotations) % (bottom - top + 1);
		return screen[y];
	};
	auto source = [&](char* row) -> TerminalCells::EncodedRow& {
		auto [it, inserted] = staged.try_emplace(row);
		if (inserted) it->second = ReadUtf8Source(row);
		return it->second;
	};
	try
	{
		for (int i = 0; i < count; ++i)
		{
			const auto& glyph = i ? *nextGlyph : first;
			auto& update = updates[i];
			char* row = physical(pos.y);
			if (!glyph.width)
			{
				if (glyph.scalar < 0xa0 || glyph.source.Bytes().front().boundaryBefore || pos.x <= 0) continue;
				auto& old = source(row);
				const auto projection = old.ReadUtf8(cols);
				int lead = (std::min)(static_cast<int>(pos.x), cols) - 1;
				if (projection.At(lead).continuation) --lead;
				if (projection.Source(lead).begin == projection.Source(lead).end) continue;
				old = old.AppendUtf8Marks(cols, lead, glyph.source);
				update.row = row;
				continue;
			}
			auto& placement = update.placement;
			placement = TerminalCells::PlaceGlyph(cols, pos.x, glyph.width, margin);
			if (placement.column < 0) continue;
			if (placement.clearEdge)
			{
				auto& edge = source(row);
				edge = edge.ReplaceUtf8(cols, cols - 1, cols, {});
				update.edge = row;
			}
			if (placement.lineFeed)
			{
				if (pos.y == bottom)
				{
					++rotations;
					staged.insert_or_assign(physical(bottom), TerminalCells::EncodedRow());
				}
				else if (pos.y < last_line) ++pos.y;
				row = physical(pos.y);
			}
			auto& old = source(row);
			if (is_ansi_editor && insert_mode)
			{
				const auto before = old.ReadUtf8(cols);
				const auto range = before.Source(placement.column);
				if (range.begin != range.end)
				{
					old = TerminalCells::EncodedRow::Shift(before, placement.column, glyph.width, true);
					update.shifted = glyph.width;
				}
			}
			old = old.WriteUtf8Glyph(cols, placement.column, glyph.source, !placement.lineFeed);
			update.row = row;
			update.link = i ? osc_links.ActiveTarget() : firstLink;
			pos.x = placement.nextColumn;
		}
		if (!encoded_rows.CommitRows(staged, [&] {
			for (int i = 0; i < count; ++i)
			{
				const auto& update = updates[i];
				if (update.edge)
				{
					osc_links.Erase(update.edge, cols - 1, 1);
					SetUpdateWholeLine(cursor_pos.y);
				}
				if (update.placement.lineFeed) LineFeed();
				SetUpdateWholeLine(cursor_pos.y);
			}
		})) return StopForSourceLimit();
	}
	catch (const std::bad_alloc&) { return StopForSourceLimit(); }
	catch (const std::length_error&) { return StopForSourceLimit(); }
	for (int i = 0; i < count; ++i)
	{
		const auto& update = updates[i];
		if (update.edge) MirrorUtf8Row(update.edge);
		if (!update.row) continue;
		MirrorUtf8Row(update.row);
		if (update.shifted)
		{
			const int start = update.placement.column;
			osc_links.Copy(update.row, start + update.shifted, update.row, start, cols - start - update.shifted, cols);
		}
		if (update.placement.column >= 0)
			for (int x = update.placement.column; x < update.placement.nextColumn; ++x)
				osc_links.Write(update.row, x, cols, update.link);
	}
	cursor_pos = pos;
}

void CTelnetConn::FinishReceivedText()
{
	if (source_limit_stopped) return;
	FinishBig5Text();
	for (const auto& glyph : utf8_decoder.FinishText())
		WriteUtf8Glyph(glyph, utf8_prefix_link);
	utf8_prefix_link.reset();
	legacy_run_start = true;
}

void CTelnetConn::OnText(bool fromNetwork)
{
	if (source_limit_stopped) return;
	CheckSynchronizedOutputTimeout();
	auto& parser = fromNetwork ? ansi_parser : local_parser;
	if (pending_big5 && view->GetCodePage() != 950) FinishReceivedText();
	while (buf < last_byte && !source_limit_stopped)
	{
		if (*buf == IAC)
		{
			buf--;
			break;
		}

		const AnsiSequenceParser::Result parsed = parser.Feed(*buf);
		if (*buf == 0x18 || *buf == 0x1a) sgr66_attribute.Reset();
		if (parsed == AnsiSequenceParser::Osc)
			osc_links.Command(parser.OscSequence(), parser.OscValid());
		else if (parsed == AnsiSequenceParser::SyncBegin)
			BeginSynchronizedOutput();
		else if (parsed == AnsiSequenceParser::SyncEnd)
			EndSynchronizedOutput();
		else if (parsed == AnsiSequenceParser::AutoWrapOn || parsed == AnsiSequenceParser::AutoWrapOff)
		{
			FinishReceivedText();
			if (source_limit_stopped) return;
			sgr66_attribute.Reset();
			if (!is_ansi_editor)
			{
				remote_line_wrap = parsed == AnsiSequenceParser::AutoWrapOn ? 1 : 0;
				if (cursor_pos.x >= site_settings.cols_per_page)
					cursor_pos.x = site_settings.cols_per_page - 1;
			}
		}
		else if (parsed == AnsiSequenceParser::Dispatch)
		{
			const char command = parser.Sequence()[parser.Length() - 1];
			if (command != 'm' && command != 'n')
			{
				FinishReceivedText();
				if (source_limit_stopped) return;
				sgr66_attribute.Reset();
				// The one-past-right position denotes a delayed wrap. Cursor/control
				// operations address the last cell instead; SGR keeps wrap pending.
				if (!is_ansi_editor && cursor_pos.x >= site_settings.cols_per_page)
					cursor_pos.x = site_settings.cols_per_page - 1;
			}
			// The parser only dispatches complete, bounded, plain numeric CSI
			// or single-character ESC commands understood by the legacy handler.
			memcpy(ansi_param, parser.Sequence(), parser.Length() + 1);
			pansi_param = ansi_param + parser.Length();
			if (command != 'n' || fromNetwork) ProcessAnsiEscapeSequence();
			*ansi_param = 0;
			pansi_param = ansi_param;
		}
		else if (parsed == AnsiSequenceParser::Text)
		{
			if (*buf < ' ') { FinishReceivedText(); sgr66_attribute.Reset(); }
			if (source_limit_stopped) return;
			switch (*buf)
			{
			case 13:	//Carrige Return
				if (is_ansi_editor)
					EditorCarriageRetiurn();
				cursor_pos.x = 0;
				break;
			case 10:	//line feed
				if (!is_ansi_editor)
				{
					if (cursor_pos.x >= site_settings.cols_per_page)
						cursor_pos.x = site_settings.cols_per_page - 1;
					LineFeed();
				}
				break;
			case 8:
				if (!is_ansi_editor && cursor_pos.x >= site_settings.cols_per_page)
					cursor_pos.x = site_settings.cols_per_page - 1;
				Back();
				break;
			case 7:
				if (::GetForegroundWindow() != view->parent->m_hWnd)
					view->doflash = 1;

				switch (AppConfig.sound)
				{
				case 1:
					//	From Putty
//				if ( IsWinNT() )		// Windows NT
//					Beep(400,40);
//				else								//Windows 9X
					MessageBeep(0xffffffff);
					break;
				case 2:
					PlaySound(NULL, NULL, SND_ASYNC);
					PlaySound(AppConfig.wavepath, NULL, SND_FILENAME | SND_ASYNC);
					break;
				}

				if (view->telnet != this)
				{
					int idx = view->parent->ConnToIndex(this);
					TCITEM tcitem;
					tcitem.mask = TCIF_IMAGE;
					tcitem.iImage = 5;
					view->parent->tab.SetItem(idx, &tcitem);
				}
				if (view->auto_switch)
				{
					view->parent->SwitchToConn(this);
					view->parent->SetForegroundWindow();
					view->parent->BringWindowToTop();
				}
				break;
			case 9:
				{
					UINT x = (cursor_pos.x / 8) * 8 + 8;
					cursor_pos.x = x;
					if (cursor_pos.x > site_settings.cols_per_page - 1)
						cursor_pos.x = site_settings.cols_per_page - 1;
					break;
				}
			default:	//normal plain text
				{
				const BYTE text_attr = sgr66_attribute.Take(cur_attr);
				if (cursor_pos.y > last_line || cursor_pos.y < first_line)
					break;

				if (*buf < ' ') break;
				const BYTE byte = *buf;
				if (view->GetCodePage() == CP_UTF8)
				{
					const bool hadPrefix = utf8_decoder.Pending();
					bool emitted = false;
					const bool accepted = utf8_decoder.TryFeed(byte, {text_attr, 0}, [&](const auto& batch) {
						emitted = true;
						WriteUtf8Glyph(batch.glyphs[0], hadPrefix ? utf8_prefix_link : osc_links.ActiveTarget(),
							batch.count == 2 ? &batch.glyphs[1] : nullptr);
						return !source_limit_stopped;
					});
					if (!accepted) return;
					if (utf8_decoder.Pending() && (!hadPrefix || emitted)) utf8_prefix_link = osc_links.ActiveTarget();
					else if (!utf8_decoder.Pending()) utf8_prefix_link.reset();
					break;
				}
				if (CUcs2Conv::IsBig5Pair(pending_big5, byte))
				{
					WriteReceivedGlyph(byte, text_attr, osc_links.ActiveTarget(), true);
					pending_big5 = 0;
					pending_big5_link.reset();
					break;
				}
				FinishBig5Text();
				if (!is_ansi_editor && view->GetCodePage() == 950 && Sgr66::Lead(byte))
				{
					pending_big5 = byte;
					pending_big5_attr = text_attr;
					pending_big5_link = osc_links.ActiveTarget();
				}
				else WriteReceivedGlyph(byte, text_attr, osc_links.ActiveTarget(), false);
			}
			}
		}
		buf++;
	}
	// Triggers retain their raw receive-data semantics; do not replay buffers.
	if (source_limit_stopped) return;
	CheckStrTrigger();
	if (sync_output.Active()) return;
	UpdateCursorPos();
	CheckHyperLinks();

//Update Lines
	if (this == view->telnet)
	{
		view->HideCaret();
		for (int i = first_line;i <= last_line;i++)
		{
			if (GetUpdateLine(i))
			{
				UpdateLine(i);
				RemoveUpdateLine(i);
			}
		}
		view->UpdateWindow();
		view->ShowCaret();

		if (is_getting_article)
			ContinueCopyArticle();
	}
}

void CTelnetConn::UpdateCursorPos()
{
	if (sync_output.Active()) return;
	if (this != ((CTermView*)view)->telnet)
		return;
	int y;
	if (is_ansi_editor)
	{
		y = cursor_pos.y - scroll_pos;
	}
	else
	{
		y = cursor_pos.y - first_line;
	}
	view->SetCursorPos(cursor_pos.x, y);
	if (y < 0 || y >= site_settings.lines_per_page)
		view->HideCaret();
	else
	{
		view->ShowCaret();
	}
}

void CTelnetConn::BeginSynchronizedOutput()
{
	if (is_ansi_editor || sync_output.Active()) return;
	CheckHyperLinks();
	if (sync_output.Begin(GetTickCount64(), screen + scroll_pos,
		site_settings.lines_per_page, GetLineBufLen(), &encoded_rows))
	{
		sync_scroll_pos = scroll_pos;
		osc_snapshot.Reset();
		for (int i = 0; i < site_settings.lines_per_page; ++i)
			osc_snapshot.CopyFrom(osc_links, sync_output.Row(i), 0, screen[scroll_pos + i],
				0, site_settings.cols_per_page, site_settings.cols_per_page);
		if (view->telnet == this) view->HideCaret();
	}
}

void CTelnetConn::RefreshSynchronizedOutput()
{
	osc_snapshot.Reset();
	for (int i = first_line; i <= last_line; ++i) SetUpdateWholeLine(i);
	if (view->telnet == this)
	{
		view->Invalidate(FALSE);
		UpdateCursorPos();
	}
}

void CTelnetConn::EndSynchronizedOutput()
{
	if (sync_output.End()) RefreshSynchronizedOutput();
}

void CTelnetConn::CheckSynchronizedOutputTimeout()
{
	if (source_limit_stopped) return;
	if (sync_output.Expire(GetTickCount64()))
		RefreshSynchronizedOutput(); // Display only; never advance copying.
}

void CTelnetConn::OnClose()
{
	if (source_limit_stopped) return;
	FinishReceivedText();
	if (source_limit_stopped) return;
	ansi_parser.Reset();
	*ansi_param = 0;
	pansi_param = ansi_param;
	const bool unfinishedFrame = !sync_output.CopyReady();
	ClearAllFlags();
	if (unfinishedFrame)
	{
		sync_output.Abort(); // Disconnect is not an authoritative frame end.
		RefreshSynchronizedOutput();
	}
	is_disconnected = true;

	Close();

	TCITEM tcitem;
	tcitem.mask = TCIF_IMAGE;
	tcitem.iImage = 1;

	int idx = view->parent->ConnToIndex(this);

	if (idx < view->parent->tab.GetItemCount())	//如果已經斷線卻未關閉畫面
	{
		view->parent->tab.SetItem(idx, &tcitem);	//改為紅色圖示
		//如果設定自訂重連，而且在時間內被斷線，且間隔時間已到
		if (site_settings.auto_reconnect &&
			time <= site_settings.connect_interval
			&& site_settings.reconnect_interval == 0)
		{
			view->ReConnect(this);	//重新連線
		}
		else
		{
			if (AppConfig.auto_close)	//如果設定為斷線自動關閉
			{
				view->parent->SwitchToConn(this);	//切換到已經斷線的畫面
				view->parent->CloseConn(view->parent->ConnToIndex(this));	//關閉畫面
			}
		}
	}
}


void CTelnetConn::UpdateLine(int line)
{
	CRect rc;
	rc.top = (line - scroll_pos) * view->lineh + view->top_margin;
	LPBYTE attrib = GetLineAttr(line);
	rc.left = view->left_margin + (*(attrib - 2) - 1) * view->chw;
	rc.right = view->left_margin + (*(attrib - 3) + 1) * view->chw;
	if (view->GetCodePage() == CP_UTF8)
	{
		rc.left = view->left_margin;
		rc.right = rc.left + site_settings.cols_per_page * view->chw;
	}
	rc.bottom = rc.top + view->lineh;
	::InvalidateRect(view->m_hWnd, rc, FALSE);
	rc.top = rc.bottom - 1;
	rc.left = view->left_margin;
	rc.right = view->left_margin + view->chw * site_settings.cols_per_page;
	::InvalidateRect(view->m_hWnd, rc, FALSE);
}

void CTelnetConn::ClearCurrentLine(int param)
{
	const int cols = site_settings.cols_per_page;
	const int x = static_cast<int>((std::clamp)(cursor_pos.x, 0L, static_cast<long>(cols)));
	char* row = screen[cursor_pos.y];
	if (param < 0 || param > 2) return;
	const int begin = param == 0 ? x : 0;
	const int end = param == 1 ? (std::min)(cols, x + 1) : cols;
	int linkBegin = begin, linkEnd = end;
	const bool mirror = !encoded_rows.At(row).Bytes().empty();
	if (begin == 0 && end == cols) encoded_rows.Erase(row);
	else if (!encoded_rows.At(row).Bytes().empty())
	{
		if (begin != end && !encoded_rows.TryEdit(row, [&](const TerminalCells::EncodedRow& old) {
			if (view->GetCodePage() == CP_UTF8)
			{
				const auto before = old.ReadUtf8(cols);
				if (before.At(begin).continuation) --linkBegin;
				if (end < cols && before.At(end).continuation) ++linkEnd;
				return old.ReplaceUtf8(cols, begin, end, {});
			}
			return old.ReplaceLegacy(cols, begin, end, {}, TerminalCells::WindowsRowDecoder(view->GetCodePage(), g_ucs2conv));
		})) return StopForSourceLimit();
	}
	cursor_pos.x = x;
	osc_links.Erase(row, linkBegin, linkEnd - linkBegin);
	memset(row + begin, ' ', end - begin);
	memset(GetLineAttr(cursor_pos.y) + begin, 7, end - begin);
	if (mirror) MirrorUtf8Row(row);
	if (param == 2) row[cols] = 0;
	SetUpdateWholeLine(cursor_pos.y);
}

void CTelnetConn::ClearScreen(int param)
{
	long a;

	for (a = 0;a < site_settings.line_count - site_settings.lines_per_page;a++)	//中間每行的指標都往前移動24行
	{
		//2004/2/1 終於想到更聰明的!!
		//將要丟棄的舊行緩衝區直接和要新配置的新頁緩衝區swap!!! 這樣最簡便 :
		char* tmpline = screen[a];
		screen[a] = screen[a+site_settings.lines_per_page];
		RemoveUpdateLine(a);

		screen [a+site_settings.lines_per_page] = tmpline;
//		InitNewLine(tmpline);
//		SetUpdateWholeLine(a+site_settings.lines_per_page);
	}

	for (a = site_settings.line_count - site_settings.lines_per_page;a < site_settings.line_count;a++)	//清空新的一頁
	{
		InitNewLine(screen[a]);
		SetUpdateWholeLine(a);
	}

//	現在所有畫面已經向上捲動一頁了

	switch (param)
	{
	case 2:
		break;
	case 0:
	default:
		for (a = site_settings.line_count - site_settings.lines_per_page;a < cursor_pos.y;a++)	//將舊的畫面複製一份過來
		{
			if (a < site_settings.lines_per_page)
				break;
			memcpy(screen[a], screen[a-site_settings.lines_per_page], GetLineBufLen());
			osc_links.Copy(screen[a], 0, screen[a-site_settings.lines_per_page], 0,
				site_settings.cols_per_page, site_settings.cols_per_page);
			SetUpdateWholeLine(a);
		}
		if (cursor_pos.y >= site_settings.lines_per_page)
		{
			memcpy(screen[cursor_pos.y], screen[cursor_pos.y-site_settings.lines_per_page], cursor_pos.x);
			osc_links.Copy(screen[cursor_pos.y], 0, screen[cursor_pos.y-site_settings.lines_per_page], 0,
				cursor_pos.x, site_settings.cols_per_page);
			memcpy(GetLineAttr(cursor_pos.y), GetLineAttr(cursor_pos.y - site_settings.lines_per_page), cursor_pos.x);
		}
		break;
	case 1:
		//將舊的畫面複製一份過來
		if (cursor_pos.y >= site_settings.lines_per_page)
		{
			memcpy(screen[cursor_pos.y],
				   screen[cursor_pos.y-site_settings.lines_per_page] + cursor_pos.x, site_settings.cols_per_page - cursor_pos.x);
			osc_links.Copy(screen[cursor_pos.y], 0, screen[cursor_pos.y-site_settings.lines_per_page], cursor_pos.x,
				site_settings.cols_per_page - cursor_pos.x, site_settings.cols_per_page);
			memcpy(GetLineAttr(cursor_pos.y),
				   GetLineAttr(cursor_pos.y - site_settings.lines_per_page) + cursor_pos.x, site_settings.cols_per_page - cursor_pos.x);
		}
		SetUpdateWholeLine(cursor_pos.y);
		for (a = cursor_pos.y + 1;a < site_settings.line_count;a++)
		{
			if (a < site_settings.lines_per_page)
				break;
			memcpy(screen[a], screen[a-site_settings.lines_per_page], GetLineBufLen());
			osc_links.Copy(screen[a], 0, screen[a-site_settings.lines_per_page], 0,
				site_settings.cols_per_page, site_settings.cols_per_page);
			SetUpdateWholeLine(a);
		}
		break;
	}
}

void CTelnetConn::InsertLines(int num)	//Insert blank line at current row (shift screen down)
{
	if (num <= 0)	num = 1;
	if (num > site_settings.lines_per_page)	num = site_settings.lines_per_page;

	int i;
	for (i = first_line + scr_bottom;i > cursor_pos.y;i--)
	{
		//swap(screen[i],screen[i-num]);
		char* tmpline = screen[i];
		screen[i] = screen[i-num];
		screen[i-num] = tmpline;
		SetUpdateWholeLine(i);
		InitNewLine(tmpline);
		SetUpdateWholeLine(i - num);
	}
}



void CTelnetConn::GotoXY(int line, int col)
{
	if (line <= 0)	line = 1;
	if (col <= 0)	col = 1;
	if (line > site_settings.lines_per_page)	line = site_settings.lines_per_page;
	if (col > site_settings.cols_per_page)	col = site_settings.cols_per_page;

	if (line <= site_settings.lines_per_page)
		cursor_pos.y = first_line + line - 1;
	else
		cursor_pos.y = first_line + site_settings.lines_per_page - 1;

	if (line < 1)
		cursor_pos.y = 0;

	if (col <= site_settings.cols_per_page)
		cursor_pos.x = col - 1;
	else
		cursor_pos.x = site_settings.cols_per_page - 1;

	if (col < 0)
		cursor_pos.x = 0;
}

void CTelnetConn::SetScrollRange(int pt, int pb)
{
	if (pt <= 0)	pt = 1;
	if (pt > site_settings.lines_per_page)	pt = site_settings.lines_per_page;
	if (pb <= 0)	pb = site_settings.lines_per_page;
	if (pb > site_settings.lines_per_page)	pb = site_settings.lines_per_page;

	// A scrolling region needs at least two rows. Invalid requests leave both
	// the previous margins and the cursor unchanged (VT100 DECSTBM).
	if (pt >= pb)
		return;
	scr_top = pt - 1;
	scr_bottom = pb - 1;
	GotoXY(1, 1); // Origin mode is not implemented; Home is screen-relative.
}

void CTelnetConn::LineFeed()
{
	int _firstl = first_line + scr_top;
	int _lastl = first_line + scr_bottom;
	if (cursor_pos.y != _lastl)
	{
		// A cursor below the region moves down without scrolling that region.
		if (cursor_pos.y < last_line)
			cursor_pos.y++;
	}
	else	//at the bottom of the scroll range
	{
		// Preserve scrollback only for a full-screen region, not a partial
		// region whose top happens to be the first visible row.
		if (scr_top == 0 && scr_bottom == site_settings.lines_per_page - 1)
			_firstl = 0;

		LPSTR tmpline = screen[_firstl];

		for (int i = _firstl;i < _lastl;i++)
		{
			screen[i] = screen[i+1];
			SetUpdateWholeLine(i);
		}
		screen[_lastl] = tmpline;
		InitNewLine(tmpline);
		SetUpdateWholeLine(_lastl);
	}
}


void CTelnetConn::ScrollUp()
{
//		scroll up
	int _top = first_line + scr_top;
	int _bottom = first_line + scr_bottom;
	char* tmp = screen[_top];
	for (int i = _top;i < _bottom;i++)
	{
		screen[i] = screen[i+1];
	}
//	screen[_bottom]=new_line(site_settings.cols_per_page,attrib[_bottom]);
	screen[_bottom] = tmp;
//	attrib[_bottom]=tmp;
	InitNewLine(tmp);

	if (((CTermView*)view)->telnet == this)
		view->Invalidate(FALSE);
}

void CTelnetConn::ScrollDown()
{
//		scroll down
	int _top = first_line + scr_top;
	int _bottom = first_line + scr_bottom;
	LPSTR tmpline = screen[_bottom];
	for (int i = _bottom;i > _top;i--)
	{
		screen[i] = screen[i-1];
		SetUpdateWholeLine(i);
	}
	screen[_top] = tmpline;
	InitNewLine(tmpline);
	SetUpdateWholeLine(_top);
}

void CTelnetConn::LineFeed(int param)
{
//		line feed
	if (param <= 0)
		param = 1;
	for (int i = 0;i < param;i++)
		LineFeed();
}


int find_sub_str(char* str, char* sub)
{
	char* pstr = str;
	int len = strlen(sub);
	while (*pstr)
	{
		if (strncmp(pstr, sub, len) == 0)
			return int(pstr -str);
		pstr++;
	}
	return -1;
}

void CTelnetConn::OnConnect(bool success)
{
	// Never carry an unfinished OSC/CSI or Telnet negotiation into a new session.
	ansi_parser.Reset();
	*ansi_param = 0;
	pansi_param = ansi_param;
	TCITEM item;
	item.mask = TCIF_IMAGE;
	if (!success)
	{
		CString con_failed;
		con_failed.LoadString(IDS_CON_FAILED);
		strncpy(screen[first_line], con_failed, con_failed.GetLength());

		ClearAllFlags();
		is_disconnected = true;

		int idx = view->parent->ConnToIndex(this);
		item.iImage = 1;
		view->parent->tab.SetItem(idx, &item);
		if (view->telnet == this)
			view->InvalidateRect(FALSE);
	}
	else
	{
		ClearAllFlags();
		is_connected = true;

		int idx = view->parent->ConnToIndex(this);
		item.iImage = 0;
		view->parent->tab.SetItem(idx, &item);
		CString str("s");
		str += name;
		str += '\t';
		str += address.URL();
		str += '\t';
		if (!cfg_path.IsEmpty())
			str += cfg_path.Left(cfg_path.GetLength() - name.GetLength());

		view->parent->AddToHistoryMenu(str);
		view->parent->AddToHistory(address.URL());
	}
}

int CTelnetConn::Send(const void *lpBuf, int nBufLen)
{
	idle_time = 0;
	if (!nBufLen)
		return 0;

	if (!is_ansi_editor)
	{
		if (conn_io)
			return conn_io->Send(lpBuf, nBufLen);
		return -1;
	}

	buf = (LPBYTE)lpBuf;
	last_byte = (LPBYTE)lpBuf + nBufLen;
	OnText();
	return 0;
}

const char* memstr(const char *src, const char *end, const char *key)
{
	int keylen = strlen(key);
	if (!keylen)
		return NULL;

	end -= keylen;
	for (; src <= end; src++)
	{
		if (memcmp(src, key, keylen) == 0)
			return src;
	}
	return NULL;
}

void CTelnetConn::CheckStrTrigger()
{
	//設定first=n，第n次才開始觸發，則每收到一次msg就first--
	if (site_settings.triggers.count > 0)
	{
//		for(int i=first_line;i<=last_line;i++)
		{
			CTriggerItem* item = site_settings.triggers.pfirst;
			CTriggerItem* pprev = NULL;
			while (item)
			{
//				if( strstr(screen[i],(LPSTR)(LPCTSTR)item->msg) )
				if (memstr((char*)buffer, (char*)last_byte , (LPSTR)(LPCTSTR)item->msg))
				{
					if (item->first > 1)
						item->first--;
					else
					{
						const char* enter;
						if (!key_map || !(enter = key_map->FindKey(VK_RETURN, 0)))
							enter = "\x0d";
						CString respond = UnescapeControlChars(LPCTSTR(item->respond) + 1);
						respond.Replace("\x0d\x0a", enter);
						respond.Replace("\x0d", enter);
						SendMacroString(respond);
						if (item->count > 0)	//使用觸發次數(不計算次數的count=0)
						{
							item->count--;
							if (item->count == 0)	//如果次數到了，就不再觸發，並且移除此觸發項目
							{
								if (pprev)	//如果有前一項
									pprev->pnext = item->pnext;
								else	//如果沒有前一項，表示是第一項
									site_settings.triggers.pfirst = item->pnext;
								if (item == site_settings.triggers.plast)	//如果是刪除最後一項
									site_settings.triggers.plast = pprev;
								delete item;
								site_settings.triggers.count--;
								item = pprev;
							}
						}
					}
				}
				pprev = item;	//紀錄前一個，以用於刪除項目時
				item = item ? item->pnext : NULL;	//next item
			}
		}
	}

}

void CTelnetConn::CreateBuffer()
{
	last_line = site_settings.line_count - 1;

	if (site_settings.line_count < site_settings.lines_per_page)
		site_settings.line_count = site_settings.lines_per_page;

	first_line = site_settings.line_count - site_settings.lines_per_page;
	scr_bottom = site_settings.lines_per_page - 1;

	cursor_pos.x = 0;
	if (is_ansi_editor)
	{
		cursor_pos.y = 0;
		scroll_pos = 0;
		first_line = 0;
	}
	else
	{
		cursor_pos.y = first_line;
		scroll_pos = site_settings.line_count - site_settings.lines_per_page;
	}

	screen = new LPSTR[site_settings.line_count*2];
//原本錯誤發生在這一行....
//	attrib=(LPBYTE*)&screen[site_settings.line_count];
//因為 screen的型態是LPSTR*,不是LPSTR,所以將screen+1 會等於screen+sizeof(LPSTR)=而非LPBYTE(screen)+1

	for (long i = 0;i < site_settings.line_count;i++)
		screen[i] = AllocNewLine();
}

inline void CTelnetConn::ProcessData(int len)
{
	last_byte = buffer + len;
	for (buf = buffer; buf < last_byte && !source_limit_stopped; buf++)
	{
		if (*((BYTE*)ansi_param) == IAC)
		{
			if ((pansi_param - ansi_param) < 63)	//檢查是否已經超出ansi_buffer,64th byte是結尾0
			{
				*pansi_param = *buf;
				pansi_param++;
			}
			if ((long(pansi_param - ansi_param) == 3 && ((BYTE)*(ansi_param + 1)) != SB) || *buf == SE)
			{
				OnIAC();
				*ansi_param = 0;	//清除IAC指令暫存
				pansi_param = ansi_param;
			}
		}
		else if (*buf == IAC)
		{
			if (!*ansi_param)
				//如果不是sub negotiation的結尾 (IAC SB...SB IAC)
			{
				*((BYTE*)pansi_param) = IAC;
				pansi_param++;
			}
			else
			{
				if ((pansi_param - ansi_param) < 63)	//檢查是否已經超出ansi_buffer,64th byte是結尾0
				{
					*pansi_param = *buf;
					pansi_param++;
				}
			}
		}
		else
			OnText(true);
	}
}

void CTelnetConn::LocalEcho(void* str, int len)
{
	buf = (LPBYTE)str;
	last_byte = (LPBYTE)buf + len;
	OnText();
}

static CString WrapPasteText(const char* str, int column, int limit, const char* enter, UINT codePage)
{
	CString result;
	const char* end = str + strlen(str);
	while (str < end)
	{
		if (end - str >= 2 && str[0] == '\r' && str[1] == '\n')
		{
			result += enter;
			column = 0;
			str += 2;
			continue;
		}
		int bytes = 1, width = 1;
		const BYTE first = static_cast<BYTE>(*str);
		if (first < 0x7f)
		{
			while (str + bytes < end && str[bytes] != '\r' && str[bytes] != ' ' &&
				static_cast<BYTE>(str[bytes]) < 0x80) ++bytes;
			if (str + bytes < end && str[bytes] == ' ') ++bytes;
			width = bytes;
		}
		else if (codePage == CP_UTF8)
		{
			bytes = static_cast<int>(Sgr66::CharacterLength(str, end - str, codePage));
			TerminalCells::Utf8Decoder decoder;
			auto measure = [&](char32_t scalar) { width = TerminalCells::Width(scalar); };
			for (int i = 0; i < bytes; ++i) decoder.Feed(static_cast<BYTE>(str[i]), measure);
			decoder.Finish(measure);
		}
		else
		{
			bytes = codePage == 950 ? static_cast<int>(Sgr66::CharacterLength(str, end - str, codePage)) :
				(end - str >= 2 && IsDBCSLeadByteEx(codePage, first) ? 2 : 1);
			width = bytes;
		}
		if (column + width > limit) { result += enter; column = 0; }
		result.Append(str, bytes);
		str += bytes;
		column += width;
	}
	return result;
}

int CTelnetConn::SendString(LPCTSTR str)
{
	const char* enter = NULL;
	const char* CRLF = "\x0d\x0a";
	if (key_map)
		enter = key_map->FindKey(VK_RETURN, 0);

	if (!enter)	enter = "\x0d";

	CString data;
	int l, limit;
	if (is_ansi_editor)
	{
		l = cursor_pos.x;
		if (view->paste_block)	//區塊貼上
		{
			char* eol;
			int x = cursor_pos.x;
			int rl = 0;
            while (eol = const_cast<char*>(strchr(str, 13)))
			{
				rl += Send(str, eol - str);
				cursor_pos.x = x;
				if (cursor_pos.y < last_line)
					cursor_pos.y++;
				else
					EditorLineFeed(NULL, NULL, 0);
				str = eol + 2;
				rl += 2;
			}
			if (*str)
				rl += Send(str, strlen(str));
			return rl;
		}
		if (!AppConfig.site_settings.paste_autowrap || strstr(str, "\x1b["))	//有控制碼
			return Send(str, strlen(str));
		else
			enter = CRLF;
		limit = AppConfig.ed_cols_per_page;
	}
	else	//不是ANSI Editor,是BBS
	{
		limit = AppConfig.site_settings.paste_autowrap_col;
		l = 0;
	}

	if (AppConfig.site_settings.paste_autowrap)	//如果使用自動換行
	{
		data = WrapPasteText(str, l, limit, enter, view->GetCodePage());
	}
	else	//如果不用自動換行
	{
		data = str;
		data.Replace("\x0d\x0a", enter);
	}

	l = data.GetLength();
	if (is_ansi_editor)
		return Send(LPCTSTR(data), l);
	const char* pdata = LPCTSTR(data);
	char* _pdata = NULL;
	if (site_settings.text_input_conv)	// Encoding conversion
	{
		switch (site_settings.text_input_conv)
		{
		case BIG52GB:
			_pdata = new char[l];
			view->chi_conv.Big52GB(pdata, _pdata, l);
			pdata = _pdata;
			break;
		case GB2BIG5:
			_pdata = new char[l];
			view->chi_conv.GB2Big5(pdata, _pdata, l);
			pdata = _pdata;
		}
	}

	int sl = 0;
	int r = 0;
	while (l > sl)
	{
		r = Send((pdata + sl), l - sl);
		if (r == SOCKET_ERROR)
		{
			sl = r;
			break;
		}
		sl += r;
	}

	if (_pdata)
		delete []_pdata;

	return sl;
}

BOOL CTelnetConn::Create()
{
	return TRUE;
}

void CTelnetConn::ShiftUtf8Cells(int count, bool insert)
{
	const int cols = site_settings.cols_per_page;
	const int x = (std::clamp)(static_cast<int>(cursor_pos.x), 0, cols);
	count = (std::min)((std::max)(1, count), cols - x);
	if (!count) return;
	char* row = screen[cursor_pos.y];
	const auto viewBefore = ReadUtf8Source(row).ReadUtf8(cols);
	if (!encoded_rows.TryEdit(row, [&](const TerminalCells::EncodedRow&) {
		return TerminalCells::EncodedRow::Shift(viewBefore, x, count, insert);
	})) return StopForSourceLimit();
	const int from = insert ? x : x + count;
	const int to = insert ? x + count : x;
	const int length = cols - x - count;
	// A wide character crossing either cut becomes blank on both sides.
	for (int cut : {x, insert ? cols - count : from})
		if (cut < cols && viewBefore.At(cut).continuation) osc_links.Erase(row, cut - 1, 2);
	osc_links.Copy(row, to, row, from, length, cols);
	osc_links.Erase(row, insert ? x : cols - count, count);
	MirrorUtf8Row(row);
	SetUpdateWholeLine(cursor_pos.y);
}

void CTelnetConn::InsertChar(int n)
{
	if (view->GetCodePage() == CP_UTF8) { ShiftUtf8Cells(n, true); return; }
	if (n <= 0)
		n = 1;
	if (cursor_pos.x + n > site_settings.cols_per_page)
		n = site_settings.cols_per_page - cursor_pos.x;
	LPSTR curstr = screen[cursor_pos.y];
	int l = site_settings.cols_per_page - cursor_pos.x - n;
	LPSTR p = curstr + cursor_pos.x + n;
	LPSTR eol = curstr + site_settings.cols_per_page;
	while (eol >= p)
	{
		*eol = *(eol - n);
		eol--;
	}
	memset(curstr + cursor_pos.x, ' ', n);
	osc_links.Copy(curstr, cursor_pos.x + n, curstr, cursor_pos.x, l, site_settings.cols_per_page);
	osc_links.Erase(curstr, cursor_pos.x, n);
	memset(GetLineAttr(cursor_pos.y) + cursor_pos.x, 7, n);
	SetUpdateLine(cursor_pos.y, (BYTE)cursor_pos.x);
	SetUpdateLine(cursor_pos.y, (BYTE)site_settings.cols_per_page);
}

void CTelnetConn::DeleteLines(int n)	//delete n lines
{
	if (n <= 0)
		n = 1;
	if (n > site_settings.lines_per_page)
		n = site_settings.lines_per_page;

	int y = cursor_pos.y;
	int yn = y + n;
	if (yn > last_line)
		yn = last_line + 1;

	LPSTR* tmplines = new LPSTR[n];
	while (y < yn)
	{
		tmplines[y-cursor_pos.y] = screen[y];
		y++;
	}
	for (y = cursor_pos.y;y <= last_line;y++)
	{
		if (y + n <= last_line)
		{
			screen[y] = screen[y+n];
			SetUpdateWholeLine(y);
		}
		else
		{
			screen[y] = tmplines[(y-cursor_pos.y)-(last_line+1-yn)];
			InitNewLine(screen[y]);
			SetUpdateWholeLine(y);
		}
	}
	delete []tmplines;
}

void CTelnetConn::GoUp(int p)	//Up
{
	cursor_pos.y -= p;
	if (cursor_pos.y < first_line)
		cursor_pos.y = first_line;
	if (view->telnet == this && is_ansi_editor && cursor_pos.y < scroll_pos)
	{
		view->OnVScroll(SB_LINEUP, 0, NULL);
	}
	UpdateCursorPos();
}

void CTelnetConn::GoDown(int p)	//Down
{
	cursor_pos.y += p;
	if (cursor_pos.y > last_line)
		cursor_pos.y = last_line;
	if (view->telnet == this && is_ansi_editor && cursor_pos.y >= scroll_pos + site_settings.lines_per_page)
	{
		view->OnVScroll(SB_LINEDOWN, 0, NULL);
	}
	UpdateCursorPos();
}
void CTelnetConn::GoRight(int p)	//Right
{
	/*	if(is_ansi_editor && IsEmptyLine(screen[cursor_pos.y]+cursor_pos.x,site_settings.cols_per_page-cursor_pos.x))
		{
			if(cursor_pos.y<last_line)
			{
				cursor_pos.x=0;
				GoDown(1);
			}
			return;
		}
	*/
	cursor_pos.x += p;
	const int right = site_settings.cols_per_page - (is_ansi_editor ? 0 : 1);
	if (cursor_pos.x > right)
		cursor_pos.x = right;
	UpdateCursorPos();
}
void CTelnetConn::GoLeft(int p)	//Left
{
	if (is_ansi_editor && cursor_pos.x == 0 && cursor_pos.y > 0)
	{
		GoUp(1);
		End();	//End Key
		return;
	}
	cursor_pos.x -= p;
	if (cursor_pos.x < 0)
	{
		cursor_pos.x = 0;
	}
	UpdateCursorPos();
}
void CTelnetConn::SaveCursorPos()
{
	old_cursor_pos = cursor_pos;
}
void CTelnetConn::RestoreCursorPos()
{
	cursor_pos = old_cursor_pos;
	UpdateCursorPos();
}

void CTelnetConn::DeleteChar(int n)	//Delete a character position (shift line to the left)
{
	if (!is_ansi_editor && view->GetCodePage() == CP_UTF8) { ShiftUtf8Cells(n, false); return; }
	Delete(n);
}
void CTelnetConn::NextPages(int n)
{
}

void CTelnetConn::BackTab(int n)
{
	cursor_pos.x -= cursor_pos.x % 8;
}

void CTelnetConn::SetMode(int p)
{
	switch (p)
	{
	case 4:
		insert_mode = 1;
		break;
	}
}
void CTelnetConn::ResetMode(int p)
{
	switch (p)
	{
	case 4:
		insert_mode = 0;
		break;
	}

}
void CTelnetConn::DeviceStatusReport(int p)
{
	if (p != 6 || is_ansi_editor || is_disconnected) return;
	const int rows = site_settings.lines_per_page;
	const int cols = site_settings.cols_per_page;
	if (rows < 1 || cols < 1) return;
	const long row = (std::clamp)(cursor_pos.y - first_line, 0L, static_cast<long>(rows - 1)) + 1;
	const long column = (std::clamp)(cursor_pos.x, 0L, static_cast<long>(cols - 1)) + 1;
	char report[32];
	const int length = sprintf_s(report, "\x1b[%ld;%ldR", row, column);
	if (length > 0) Send(report, length);
}

void CTelnetConn::PrevPages(int n)
{

}

inline void CTelnetConn::ProcessAnsiEscapeSequence()
{
//	int l=strlen(ansi_param);
	if (*ansi_param == '[')
	{
		pansi_param--;	//回到最後一個字元

		char *param = ansi_param + 1;
		BYTE type = *pansi_param;

		// Recognized private modes are handled by OnText, never by the legacy
		// numeric dispatcher. Keep this guard for unrecognized private sequences.
		if (*param == '?')
			return;
		if (type == 'n')
		{
			// CPR takes one numeric parameter. Test before the legacy parser
			// discards extra parameters or treats an empty field as zero.
			while (param < pansi_param && *param == '0') ++param;
			if (pansi_param - param == 1 && *param == '6')
				DeviceStatusReport(6);
			return;
		}
		if (type == 'r' && std::count(param, pansi_param, ';') > 1)
			return; // DECSTBM accepts at most two parameters.
		if (type == 'K' && std::find(param, pansi_param, ';') != pansi_param)
			return; // EL has a single mode.

		*pansi_param = ';';

		if (type == 'm')	//ansi color
		{
			LPSTR pparam = param;
			while (*pparam)
			{
				if (*pparam == ';')
				{
					*pparam = 0;
					SetCurrentAttributes(USHORT(atoi(param)));
					param = pparam + 1;
				}
				pparam++;
			}
		}
		else
		{
			int p1, p2;
			while (*param && *param != ';')
				param++;
			if (*param)
			{
				*param = 0;
				p1 = atoi(ansi_param + 1);
				param++;
				p2 = atoi(param);
			}
			else
				p1 = p2 = 0;

			switch (type)
			{
			case 'K':
				ClearCurrentLine(p1);
				break;
//------------ 2 parameters ---------------
			case 'H':
			case 'f':
				GotoXY(p1, p2);
				break;
			case 'r':
				SetScrollRange(p1, p2);
				break;
			case 'J':
				ClearScreen(p1);
				break;
			case 'E':
				LineFeed(p1);
				break;
			case 'L':
				InsertLines(p1);
				break;
			case 'A':
				if (p1 <= 0)
					p1 = 1;
				GoUp(p1);
				break;
			case 'B':
				if (p1 <= 0)
					p1 = 1;
				GoDown(p1);
				break;
			case 'C':
				if (p1 <= 0)
					p1 = 1;
				GoRight(p1);
				break;
			case 'D':
				if (p1 <= 0)
					p1 = 1;
				GoLeft(p1);
				break;
//新加入的ansi支援
			case 's':	//save cursor pos
				SaveCursorPos();
				break;
			case 'u':	//restore cursor pos
				RestoreCursorPos();
				break;
			case '@':	//insert char
				InsertChar(p1);
				break;
			case 'M':	//delete n line
				DeleteLines(p1);
				break;
			case 'P':	//delete char
				DeleteChar(p1);
				break;
				/*
							case 'U':	//next n page
								NextPages(p1);
								break;
							case 'V':	//previous n page
								PrevPages(p1);
								break;
				*/
			case 'Z':	//cursor back tab
				BackTab(p1);
				break;
			case 'h':	//set mode
				SetMode(p1);
				break;
			case 'l':	//reset mode
				ResetMode(p1);
				break;
			default:
				{
					Sleep(0);
				}
				break;
			}
		}
	}
	else	//如果沒有'['
	{
		switch (*buf)
		{
		case 'D':
			//				scroll up
			ScrollUp();
			break;
		case 'M':
			//				scroll down
			ScrollDown();
			break;
		case 'E':
			break;
		case '7':
			old_cursor_pos = cursor_pos;
			saved_attr = cur_attr;
			break;
		case '8':
			cursor_pos = old_cursor_pos;
			cur_attr = saved_attr;
			UpdateCursorPos();
			break;
		}	//end switch
	}
}

inline void CTelnetConn::SetCurrentAttributes(USHORT clr)
{
	if (clr == 66)
	{
		// Big5 rows already store an attribute per half. UTF-8 presentation
		// keeps its existing single-color fallback instead of coloring bytes.
		if (view->GetCodePage() == 950) sgr66_attribute.Capture(cur_attr);
		return;
	}
	if (clr < 48)
	{
		if (clr >= 40)
		{
			//背景色
			clr -= 40;
			if (!attr_flags&1)
				SetBgColor(cur_attr, (BYTE)clr);
			else
				SetFgColor(cur_attr, (BYTE)clr);
		}
		else if (clr >= 30)
		{
			//前景色
			clr -= 30;
			if (!attr_flags&1)
				SetFgColor(cur_attr, (BYTE)clr);
			else
				SetBgColor(cur_attr, (BYTE)clr);
		}
		else //clr<30
		{
			//其他屬性
			switch (clr)
			{
			case 0:		//清除
				cur_attr = 7;		//0000,0111b=7d
				attr_flags = 0;
				break;
			case 1:		//高亮度
				cur_attr |= 8;		//0000,1000b=8d
				break;
			case 5:		//閃爍
				cur_attr |= 128;		//1000,0000b=128d
				break;
			case 7:
				{
					attr_flags |= 1;	//反相
					BYTE fg = cur_attr & 7;	//儲存前景色
					cur_attr &= 248;		//1111,1000b=248d
					cur_attr |= ((cur_attr & 112) >> 4);	//將背景色存到前景	//0111,0000b=112d
					cur_attr &= 143;		//1000,1111b=143d;
					cur_attr |= (fg << 4);	//將前景色存到背景
				}
			}
		}
	}
}

char* CTelnetConn::ResizeLine(int line, int newl)
{
	LPBYTE attr = GetLineAttr(line);
	if (site_settings.cols_per_page == newl)
		return screen[line];

	DWORD d = *(DWORD*)(attr + site_settings.cols_per_page);
	char* str = screen[line];
	char* newstr = new char[GetLineBufLen(newl)];
	LPBYTE newatb = (LPBYTE)newstr + (GetLineBufLen(newl) / 2);
	if (site_settings.cols_per_page > newl)
	{
		memcpy(newstr, str, newl);
		memcpy(newatb, attr, newl);
	}
	else
	{
		memcpy(newstr, str, site_settings.cols_per_page);
		memcpy(newatb, attr, site_settings.cols_per_page);
		memset(newstr + site_settings.cols_per_page, ' ', newl - site_settings.cols_per_page);
		memset(newatb + site_settings.cols_per_page, 7, newl - site_settings.cols_per_page);
	}
	osc_links.Copy(newstr, 0, str, 0, (std::min)(static_cast<int>(site_settings.cols_per_page), newl), newl);
	osc_links.Clear(str);
	encoded_rows.Move(newstr, str);
	delete []str;
	*(DWORD*)(newatb + newl) = d;
	return newstr;
}


void CTelnetConn::ReSizeBuffer(long new_line_count, int new_cols_per_page, int new_lines_per_page)
{
	if (source_limit_stopped) return;
	if (new_cols_per_page < site_settings.cols_per_page && encoded_rows.StoredRows() &&
		!encoded_rows.TryCropAll([&](const TerminalCells::EncodedRow& row) {
			if (view->GetCodePage() == CP_UTF8) return row.ReadUtf8(new_cols_per_page);
			return row.ReadLegacy(new_cols_per_page, TerminalCells::WindowsRowDecoder(view->GetCodePage(), g_ucs2conv));
		})) return; // projection refused: preserve source, screen, cursor and active snapshot
	FinishReceivedText();
	// Saved row layout is invalid after a terminal-size change. Keep copying
	// blocked until a real sync end, even though presentation must resume.
	if (sync_output.Active()) sync_output.Abort();
	osc_snapshot.Reset();
	if (new_line_count > MAX_LINE_COUNT)
		new_line_count = MAX_LINE_COUNT;

	site_settings.lines_per_page = new_lines_per_page;
	scr_top = 0;
	scr_bottom = site_settings.lines_per_page - 1;

	if (new_line_count == site_settings.line_count)	//如果最大行數不變
	{
		if (site_settings.cols_per_page != new_cols_per_page)	//如果有改變每行字數才重新配置
			for (int i = 0;i < site_settings.line_count;i++)
				screen[i] = ResizeLine(i, new_cols_per_page);
	}
	else
	{
		LPSTR* new_screen = new LPSTR[new_line_count];
		if (new_line_count > site_settings.line_count)
		{
			for (int i = 0;i < site_settings.line_count;i++)
				new_screen[i] = ResizeLine(i, new_cols_per_page);

			delete []screen;
			screen = new_screen;

			site_settings.cols_per_page = new_cols_per_page;
			for (;site_settings.line_count < new_line_count;site_settings.line_count++)
				screen[site_settings.line_count] = AllocNewLine();
		}
		else	//if(new_line_count<site_settings.line_count)
		{
			for (int i = 0;i < new_line_count;i++)
				new_screen[i] = ResizeLine(i, new_cols_per_page);

			for (long removed = site_settings.line_count - 1; removed >= new_line_count; --removed)
			{
				encoded_rows.Erase(screen[removed]);
				osc_links.Clear(screen[removed]);
				delete []screen[removed];
			}

			delete []screen;
			screen = new_screen;
		}
	}
	site_settings.line_count = new_line_count;
	site_settings.cols_per_page = new_cols_per_page;
	if (!is_ansi_editor && cursor_pos.x >= new_cols_per_page)
		cursor_pos.x = new_cols_per_page - 1;
	first_line = site_settings.line_count - site_settings.lines_per_page;
	last_line = site_settings.line_count - 1;
	if (scroll_pos > first_line)
		scroll_pos = first_line;
	if (is_ansi_editor)
		first_line = 0;

	if (cursor_pos.y >= site_settings.line_count)
	{
		scroll_pos = site_settings.line_count - site_settings.lines_per_page;
		cursor_pos.y = last_line;
	}
	if (!is_ansi_editor && cursor_pos.y < first_line)
		cursor_pos.y = first_line;
	if (view->telnet == this)
	{
		view->SetScrollBar();
		view->InvalidateRect(FALSE);
	}
}


BOOL CTelnetConn::IsEmptyLine(LPSTR line, int len)
{
	LPSTR eol = line + len;
	while (line < eol)
	{
		if (*line != ' ')
			return FALSE;
		line++;
	}
	eol += 5 + site_settings.cols_per_page;
	line = eol - len;
	while (line < eol)
	{
		if (*line != 7)
			return FALSE;
		line++;
	}
	return TRUE;
}

inline void CTelnetConn::EditorLineFeed(LPSTR newline, LPBYTE newlineatb, int l)
{
	cursor_pos.y++;
	if (cursor_pos.y == site_settings.line_count)
	{
		ReSizeBuffer(site_settings.line_count + site_settings.lines_per_page, site_settings.cols_per_page, site_settings.lines_per_page);
		if (newline)
		{
			memcpy(screen[cursor_pos.y], newline, l);
			osc_links.Copy(screen[cursor_pos.y], 0, screen[cursor_pos.y-1], site_settings.cols_per_page-l,
				l, site_settings.cols_per_page);
			memcpy(GetLineAttr(cursor_pos.y), newlineatb, l);
		}
		if (view->telnet == this)
			view->OnVScroll(SB_LINEDOWN, 0, NULL);
		else
			scroll_pos++;
	}
	else
	{
		if (insert_mode)
		{
			if (!IsEmptyLine(screen[last_line], site_settings.cols_per_page))		//最後一行有資料
				ReSizeBuffer(site_settings.line_count + site_settings.lines_per_page, site_settings.cols_per_page, site_settings.lines_per_page);	//加大緩衝區

			LPSTR tmp = screen[last_line];
			for (int i = last_line;i > cursor_pos.y;i--)
				screen[i] = screen[i-1];

			screen[cursor_pos.y] = tmp;
			InitNewLine(tmp);
			if (newline)
			{
				memcpy(screen[cursor_pos.y], newline, l);
				osc_links.Copy(screen[cursor_pos.y], 0, screen[cursor_pos.y-1], site_settings.cols_per_page-l,
					l, site_settings.cols_per_page);
				memcpy(GetLineAttr(cursor_pos.y), newlineatb, l);
			}
		}

		if (cursor_pos.y == scroll_pos + site_settings.lines_per_page)	//如果在畫面的最後一行
		{
			if (view->telnet == this)
				view->OnVScroll(SB_LINEDOWN, 0, NULL);
			else
				scroll_pos++;
		}
		else
		{
			if (view->telnet == this)
				view->Invalidate(FALSE);
		}
	}
}

inline void CTelnetConn::EditorCarriageRetiurn()
{
	if (!insert_mode)
	{
		cursor_pos.x = 0;
		EditorLineFeed(NULL, NULL, 0);
		return;
	}

	if (cursor_pos.x == 0)
	{
		cursor_pos.x = site_settings.cols_per_page;
		cursor_pos.y--;
		EditorLineFeed(NULL, NULL, 0);
		cursor_pos.y++;
		if (cursor_pos.y == scroll_pos + site_settings.lines_per_page)	//如果在畫面的最後一行
		{
			if (view->telnet == this)
				view->OnVScroll(SB_LINEDOWN, 0, NULL);
			else
				scroll_pos++;
		}
	}
	else if (view->GetCodePage() == CP_UTF8)
	{
		const int cols = site_settings.cols_per_page;
		const int x = (std::min)(static_cast<int>(cursor_pos.x), cols);
		char* row = screen[cursor_pos.y];
		const auto original = ReadUtf8Source(row);
		const auto before = original.ReadUtf8(cols);
		int last = cols;
		while (last > x && before.At(last - 1).text == U" " && before.At(last - 1).style.attribute == 7) --last;
		EditorLineFeed(NULL, NULL, 0);
		if (last > x)
		{
			char* next = screen[cursor_pos.y];
			std::map<const void*, TerminalCells::EncodedRow> edits;
			edits.emplace(row, original.SliceUtf8(cols, 0, x));
			edits.emplace(next, original.SliceUtf8(cols, x, last));
			if (!encoded_rows.CommitRows(edits, [] {})) return StopForSourceLimit();
			osc_links.Copy(next, 0, row, x, last - x, cols);
			osc_links.Erase(row, x, cols - x);
			MirrorUtf8Row(row);
			MirrorUtf8Row(next);
		}
	}
	else if (IsEmptyLine(screen[cursor_pos.y] + cursor_pos.x, site_settings.cols_per_page - cursor_pos.x))
		EditorLineFeed(NULL, NULL, 0);
	else
	{
		int newlinel = site_settings.cols_per_page - cursor_pos.x;
		const void* originalRow = screen[cursor_pos.y];
		LPSTR newline = screen[cursor_pos.y] + cursor_pos.x;
		LPBYTE newlineatb = GetLineAttr(cursor_pos.y) + cursor_pos.x;
		EditorLineFeed(newline, newlineatb, newlinel);
		memset(newline, ' ', newlinel);
		osc_links.Erase(originalRow, site_settings.cols_per_page-newlinel, newlinel);
		memset(newlineatb, 7, newlinel);
	}
}

inline void CTelnetConn::EditorLineBack(LPSTR newline, LPBYTE newlineatb, int l)
{
	if (view->GetCodePage() == CP_UTF8)
	{
		const int cols = site_settings.cols_per_page;
		const int x = cursor_pos.x;
		char* previous = screen[cursor_pos.y];
		const auto source = ReadUtf8Source(newline);
		const auto cells = source.ReadUtf8(cols);
		int end = cols;
		while (end && cells.At(end - 1).text == U" " && cells.At(end - 1).style.attribute == 7) --end;
		int move = (std::min)(end, cols - x);
		if (move < end && cells.At(move).continuation) --move;
		if (move > 0)
		{
			std::map<const void*, TerminalCells::EncodedRow> edits;
			edits.emplace(previous, ReadUtf8Source(previous).ReplaceUtf8(cols, x, x + move, source.SliceUtf8(cols, 0, move)));
			edits.emplace(newline, source.SliceUtf8(cols, move, end));
			if (!encoded_rows.CommitRows(edits, [] {})) return StopForSourceLimit();
			osc_links.Copy(previous, x, newline, 0, move, cols);
			osc_links.Copy(newline, 0, newline, move, end - move, cols);
			osc_links.Erase(newline, end - move, cols - (end - move));
			MirrorUtf8Row(previous);
			MirrorUtf8Row(newline);
			SetUpdateWholeLine(cursor_pos.y);
			SetUpdateWholeLine(cursor_pos.y + 1);
		}
		if (move < end) return;
		l = 0;
	}
	if (l)
	{
		int space = site_settings.cols_per_page - cursor_pos.x;
		if (space >= l)
		{
			memcpy(screen[cursor_pos.y] + cursor_pos.x, newline, l);
			osc_links.Copy(screen[cursor_pos.y], cursor_pos.x, newline, 0, l, site_settings.cols_per_page);
			memcpy(GetLineAttr(cursor_pos.y) + cursor_pos.x, newlineatb, l);
		}
		else
		{
			l = site_settings.cols_per_page - space;
			memcpy(screen[cursor_pos.y] + cursor_pos.x, newline, space);
			osc_links.Copy(screen[cursor_pos.y], cursor_pos.x, newline, 0, space, site_settings.cols_per_page);
			osc_links.Copy(newline, 0, newline, space, l, site_settings.cols_per_page);
			osc_links.Erase(newline, l, space);
			memcpy(GetLineAttr(cursor_pos.y) + cursor_pos.x, newlineatb, space);
			memcpy(newline, newline + space, l);
			memcpy(newlineatb, newlineatb + space, l);
			memset(newline + l, ' ', space);
			memset(newlineatb + l, 7, space);
			return;
		}
	}
	//delete line

	for (int y = cursor_pos.y + 1;y < last_line;y++)
		screen[y] = screen[y+1];

	screen[last_line] = newline;

	memset(newline, ' ', site_settings.cols_per_page);
	osc_links.Clear(newline);
	encoded_rows.Erase(newline);
	*(DWORD*)(newline + site_settings.cols_per_page) = 0;
	memset(newlineatb, 7, site_settings.cols_per_page);
	*(DWORD*)(newlineatb + site_settings.cols_per_page) = 0;

	if (cursor_pos.y < scroll_pos)	//如果在畫面的第一行
	{
		if (view->telnet == this)
			view->OnVScroll(SB_LINEUP, 0, NULL);
		else
			scroll_pos--;
	}
	else
	{
		if (view->telnet == this)
			view->Invalidate(FALSE);
	}
}

void CTelnetConn::Back(int num)
{
	if (cursor_pos.x > 0)
	{
		cursor_pos.x -= num;
		if (is_ansi_editor || site_settings.localecho)
			Delete(num);	//Delete
	}
	else if (cursor_pos.y > 0)
	{
		if (is_ansi_editor)
		{
			LPSTR newline = screen[cursor_pos.y];
			LPBYTE newlineatb = GetLineAttr(cursor_pos.y);
			cursor_pos.y--;
			End();	//End
			LPSTR eonl = newline + site_settings.cols_per_page - 1;	
			LPBYTE eonla = newlineatb + site_settings.cols_per_page - 1;
			while (eonl >= newline && *eonl == ' ' && *eonla == 7)
				eonl--, eonla--;
			eonl++;
			EditorLineBack(newline, newlineatb, eonl - newline);
		}
	}
	UpdateCursorPos();
}

inline void CTelnetConn::SetUpdateLine(long line, BYTE curx)
{
	LPBYTE attrib = GetLineAttr(line);
	if (GetUpdateLine(line))	//如果這行已經設為等待更新
	{
		if (curx < *(attrib - 2))	//如果比start更小
			*(attrib - 2) = curx;
		if (curx > *(attrib - 3))	//如果比end更大
			*(attrib - 3) = curx;
	}
	else
	{
		*(attrib - 1) = 1;	//第一次設為等待更新
		*(attrib - 2) = *(attrib - 3) = curx;	//start和end=curx
	}
}

inline void CTelnetConn::RemoveUpdateLine(long line)
{
	*(DWORD*)(GetLineAttr(line) - 4) = 0;
}

inline BOOL CTelnetConn::GetUpdateLine(long line)
{
	return *(GetLineAttr(line) - 1);
}

inline void CTelnetConn::SetUpdateWholeLine(long line)
{
	LPBYTE attrib = GetLineAttr(line);
	*(attrib - 1) = 1;
	*(attrib - 2) = 0;
	*(attrib - 3) = (BYTE)site_settings.cols_per_page - 1;
}

void CTelnetConn::CheckHyperLinks()
{
	long bottom = scroll_pos + site_settings.lines_per_page;
	for (long i = scroll_pos;i < bottom;i++)
	{
		screen[i][site_settings.cols_per_page] = 0;
		if (GetUpdateLine(i))	//如果這行內容有變更，才重新找超連結
		{
			// 如果找到超連結，設定超連結旗標
			// 如果沒找到，清除超連結旗標
			int len;
			SetHyperLink(i, view->GetCodePage() == CP_UTF8 || osc_links.Overlaps(screen[i], 0, site_settings.cols_per_page) ||
				!!AppConfig.hyper_links.FindHyperLink(screen[i], len));
		}
	}
}


void CTelnetConn::Delete(int num)
{
	if (is_ansi_editor && view->GetCodePage() == CP_UTF8) { ShiftUtf8Cells(num, false); return; }
	if (num <= 0)	num = 1;
	LPSTR curstr = screen[cursor_pos.y];
	if (cursor_pos.x > site_settings.cols_per_page)
		cursor_pos.x = site_settings.cols_per_page;
	if (cursor_pos.x < 0)
		cursor_pos.x = 0;

	SetUpdateLine(cursor_pos.y, (BYTE)cursor_pos.x);

	if (cursor_pos.x + num > site_settings.cols_per_page)
		num = site_settings.cols_per_page - cursor_pos.x;

	int len = site_settings.cols_per_page - (cursor_pos.x + num);
	strncpy(curstr + cursor_pos.x , curstr + cursor_pos.x + num , len);
	osc_links.Copy(curstr, cursor_pos.x, curstr, cursor_pos.x + num, len, site_settings.cols_per_page);
	osc_links.Erase(curstr, cursor_pos.x + len, num);
	memmove(GetLineAttr(curstr) + cursor_pos.x,
			GetLineAttr(curstr) + cursor_pos.x + num, len);
	memset(curstr + cursor_pos.x + len, ' ', num);
	memset(GetLineAttr(curstr) + cursor_pos.x + len, 7, num);
	curstr[site_settings.cols_per_page] = 0;
	SetUpdateLine(cursor_pos.y, (BYTE)site_settings.cols_per_page);
}

void CTelnetConn::End()
{
	if (is_ansi_editor && view->GetCodePage() == CP_UTF8)
	{
		const int cols = site_settings.cols_per_page;
		const auto row = ReadUtf8Source(screen[cursor_pos.y]).ReadUtf8(cols);
		int end = cols;
		while (end && row.At(end-1).text == U" " && row.At(end-1).style.attribute == 7) --end;
		cursor_pos.x = end;
		UpdateCursorPos();
		return;
	}
	LPSTR curstr = screen[cursor_pos.y];
	int end = site_settings.cols_per_page - 1;
	LPBYTE attrib = GetLineAttr(cursor_pos.y);
	while (end >= 0 && curstr[end] == ' ' && attrib[end] == 7)
		end--;
	cursor_pos.x = end + 1;
	UpdateCursorPos();
}


void CTelnetConn::SendMacroString(CString str)
{
//	先檢查是否有巨集指令，像是暫停
	const char* _pstr = str;
    const char* pstr;
    for (pstr = _pstr; *pstr; pstr += get_chw(pstr))
	{
		if (*pstr == '#' && *(pstr + 1))
		{
			pstr++;
			switch (*pstr)
			{
                case 'p': {
                    POSITION pos;
					if (pstr > _pstr)
						Send(_pstr, int(pstr) - int(_pstr) - 1);

					pstr++;
					int pause = atoi(pstr);
                    for (pos = delay_send.GetHeadPosition(); pos ;delay_send.GetNext(pos))
					{
						if (pause > delay_send.GetAt(pos).time)
							break;
					}
					while (isdigit(*pstr))
						*pstr++;

					CTelnetConnDelayedSend ds;
					ds.str = pstr;
					ds.time = pause;
					if (pos)
						delay_send.InsertBefore(pos, ds);
					else
						delay_send.AddTail(ds);
					return;
				}
			case '#':
				if (pstr > _pstr)
					Send(_pstr, int(pstr) - int(_pstr));
				SendMacroString(pstr + 1);
				return;
			}
		}
	}
	if (pstr > _pstr)
		Send(_pstr, int(pstr) - int(_pstr));
}

void CTelnetConn::SendNaws()
{
	BYTE naws[] = {IAC, SB, NAWS, 0, 80, 0, 24, IAC, SE};
	naws[3] = HIBYTE(site_settings.cols_per_page);
	naws[4] = LOBYTE(site_settings.cols_per_page);
	naws[5] = HIBYTE(site_settings.lines_per_page);
	naws[6] = LOBYTE(site_settings.lines_per_page);
	Send(naws, sizeof(naws));
}


int CTelnetConn::IsEndOfArticleReached()
{
	return GetArticleProgress().state;
}

ArticleProgress::Progress CTelnetConn::GetArticleProgress()
{
	if (!sync_output.CopyReady()) return ArticleProgress::Progress();
	if (!screen || last_line < 0 || last_line >= site_settings.line_count ||
		site_settings.cols_per_page <= 0)
		return ArticleProgress::Progress();
	if (view->GetCodePage() == CP_UTF8)
	{
		const auto footer = ReadUtf8Source(screen[last_line]).ReadUtf8(site_settings.cols_per_page);
		const auto bytes = footer.RawText(0, footer.Columns());
		return ArticleProgress::Parse(bytes.data(), bytes.size());
	}
	return ArticleProgress::Parse(screen[last_line], site_settings.cols_per_page);
}

bool CTelnetConn::UseSgr66() const
{
	const UINT cp = view->GetCodePage();
	return (cp == 950 || cp == CP_UTF8) && Sgr66::Enabled(AppConfig.ansi_output_format,
		address.IsValid() && KeepAlive::IsPttHost(LPCTSTR(address.Server())), is_ansi_editor);
}

CString CTelnetConn::GetUtf8Range(long line, int begin, int finish, bool withColor)
{
	const auto row = ReadUtf8Source(screen[line]).ReadUtf8(site_settings.cols_per_page);
	std::string bytes = row.RawText(begin, finish);
	if (withColor)
	{
		std::vector<BYTE> attributes;
		for (int x = begin; x < finish; ++x)
		{
			const auto& cell = row.At(x);
			if (cell.continuation || x + static_cast<int>(cell.width) > finish) continue;
			const auto range = row.Source(x);
			attributes.insert(attributes.end(), (std::max)(size_t(1), range.end - range.begin), cell.style.attribute);
		}
		const int size = static_cast<int>(bytes.size());
		bytes = size ? Sgr66::EncodeRange(bytes.data(), attributes.data(), size, 0, size, CP_UTF8) : "\x1b[m";
	}
	else
		bytes.erase(bytes.find_last_not_of(' ') + 1);
	return CString(bytes.data(), static_cast<int>(bytes.size()));
}

CString CTelnetConn::GetArticleLine(long line, bool withColor)
{
	if (view->GetCodePage() == CP_UTF8)
		return GetUtf8Range(line, 0, site_settings.cols_per_page, withColor);
	return withColor ? GetLineWithAnsi(line) : CString(screen[line]);
}

CString CTelnetConn::GetLineWithAnsi(long line)
{
	if (UseSgr66())
	{
		const std::string encoded = Sgr66::EncodeRange(screen[line], GetLineAttr(line),
			site_settings.cols_per_page, 0, site_settings.cols_per_page, view->GetCodePage());
		return CString(encoded.data(), static_cast<int>(encoded.size()));
	}
	CString data("\x1b[m");

	LPSTR str = screen[line];
	LPSTR strend;
	LPBYTE attr = GetLineAttr(str);
	BYTE tmpatb = *attr;
	if (tmpatb != 7)
		data += AttrToStr(7, tmpatb);

	strend = str + site_settings.cols_per_page - 1;
	BYTE *atbend = GetLineAttr(line) + site_settings.cols_per_page - 1;
	while (strend > str && *strend == ' ' && GetAttrBkColor(*atbend) == 0)
		strend--, atbend--;
	if (strend == str && *strend == ' ' && GetAttrBkColor(*atbend) == 0)
		return "\x1b[m";

	while (str <= strend)
	{
		if (*attr != tmpatb)
		{
			data += AttrToStr(tmpatb, *attr);
			tmpatb = *attr;
		}
		if (*str)
			data += *str;
		str++;
		attr++;
	}
	if (tmpatb != 7)
		data += "\x1b[m";

	return data;
}

CString AttrToStr(BYTE prevatb, BYTE attr)
{
	CString ret = "\x1b[";
	if (attr == 7)
	{
		ret += 'm';
		return ret;
	}

	BYTE fg = attr & 7;
	BYTE bk = GetAttrBkColor(attr);
	BYTE blink = attr & 128;
	BYTE hilight = attr & 8;

	BYTE hilight_changed = 0;
	BYTE blink_changed = 0;
	BYTE fg_changed = 0;
	BYTE bk_changed = 0;

	if (fg != (prevatb&7))	//如果前景色改變
		fg_changed = 1;
	if (bk != GetAttrBkColor(prevatb))	//如果背景色改變
		bk_changed = 1;

	if (hilight != (prevatb&8))	//如果高亮度改變
	{
		hilight_changed = 1;
		if (!hilight)	//如果變成不亮,要重設所有屬性
		{
			blink_changed = fg_changed = bk_changed = 1;
			ret += ';';
		}
	}

	if (blink != (prevatb&128))	//如果閃爍改變
	{
		blink_changed = 1;
		if (!blink)	//如果變成不閃爍,要重設所有屬性
		{
			if (!(hilight_changed && !hilight))	//如果所有屬性還沒重設過才重設
			{
				ret += ';';
				hilight_changed = fg_changed = bk_changed = 1;
			}
		}
	}

	if (hilight_changed && hilight)
		ret += "1;";
	if (blink_changed && blink)
		ret += "5;";
	char num[4] = {0, 0, ';', 0};
	if (fg_changed)
	{
		*num = '3';
		*(num + 1) = '0' + fg;
		ret += num;
	}
	if (bk_changed)
	{
		*num = '4';
		*(num + 1) = '0' + bk;
		ret += num;
	}

	char* pret = LPSTR(LPCTSTR(ret));
	if (pret[ret.GetLength()-1] == ';')
		pret[ret.GetLength()-1] = 'm';
	else
		ret += 'm';
	return ret;
}

void CTelnetConn::CopyArticle(bool with_color, bool in_editor)
{
	if (is_getting_article || download_article_dlg) return;
	const ArticleProgress::Progress progress = GetArticleProgress();
	if (progress.state == ArticleProgress::Unavailable || scroll_pos < 0 ||
		scroll_pos >= last_line || progress.last - progress.first >= last_line - scroll_pos)
	{
		AfxMessageBox("無法辨識文章底列進度，請回到文章畫面並等待畫面完整後再試。", MB_OK | MB_ICONINFORMATION);
		return;
	}
	downloaded_article.Empty();
	current_download_line = progress.last;
	get_article_in_editor = in_editor;
	get_article_with_ansi = with_color;
	for (int y = scroll_pos; y < last_line; ++y)
	{
		downloaded_article += GetArticleLine(y, with_color);
		downloaded_article.TrimRight(" ");
		downloaded_article += "\r\n";
	}

	if (progress.state == ArticleProgress::Complete)
		CopyArticleComplete();
	else
	{
		is_getting_article = true;
		const char* key = key_map->FindKey(VK_DOWN, 0);
		SendString(key ? key : "^[[B");
		// A partial/unsupported footer must still leave a way to cancel.
		download_article_dlg = new CDownloadArticleDlg(this);
		download_article_dlg->DoModal();
	}
}

void CTelnetConn::ContinueCopyArticle()
{
	const ArticleProgress::Progress progress = GetArticleProgress();
	const int count = ArticleProgress::NewLines(progress, current_download_line,
		last_line - first_line);
	if (!count) return; // Wait for a valid, advanced footer; never blindly send Down.
	current_download_line = progress.last;
	for (int y = last_line - count; y < last_line; ++y)
	{
		downloaded_article += GetArticleLine(y, get_article_with_ansi);
		downloaded_article.TrimRight(" ");
		downloaded_article += "\r\n";
	}
	if (progress.state == ArticleProgress::Complete)
		CopyArticleComplete();
	else
	{
		const char* key = key_map->FindKey(VK_DOWN, 0);
		SendString(key ? key : "^[[B");
	}
}

void CTelnetConn::CopyArticleComplete(bool cancel)
{
	is_getting_article = false;

	if (!cancel)
	{

		if (get_article_in_editor)
		{
			view->OnAnsiEditor();
			CTelnetConn* editor = view->telnet;
			//FIXME: dirty hack to insert ansi string to the editor
			view->LockWindowUpdate();
			editor->Send((LPCTSTR)downloaded_article, downloaded_article.GetLength());
			editor->scroll_pos = 0;
			editor->cursor_pos.x = editor->cursor_pos.y = 0;
			editor->UpdateCursorPos();
			view->SetScrollBar();
			view->UnlockWindowUpdate();
		}
		else if (get_article_with_ansi)
			view->CopyColorText(downloaded_article);
		else
		{
			// Use the same station encoding/UAO mapping as selection copy and search.
			const CStringW unicode = view->DecodeText(downloaded_article);
			CClipboard::SetTextW(view->m_hWnd, unicode);
		}

		if (download_article_dlg)	// dismiss cancel dialog
			download_article_dlg->OnCancel();
	}
	get_article_in_editor = false;
	get_article_with_ansi = false;
	downloaded_article.Empty();
}
