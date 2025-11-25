// TranslationSites.cpp: implementation of translation and query site configuration
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "TranslationSites.h"
#include "StrUtils.h"

#ifdef _DEBUG
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#define new DEBUG_NEW
#endif

//////////////////////////////////////////////////////////////////////
// Global instance
//////////////////////////////////////////////////////////////////////

CTranslationSiteManager g_TranslationSites;

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CTranslationSiteManager::CTranslationSiteManager()
{
}

CTranslationSiteManager::~CTranslationSiteManager()
{
	m_sites.RemoveAll();
}

//////////////////////////////////////////////////////////////////////
// Load sites from configuration file
//////////////////////////////////////////////////////////////////////

BOOL CTranslationSiteManager::Load(LPCTSTR configPath)
{
	CFile file;
	if (!file.Open(configPath, CFile::modeRead))
	{
		return FALSE;
	}

	m_sites.RemoveAll();

	DWORD len = (DWORD)file.GetLength();
	if (len == 0)
	{
		file.Close();
		return FALSE;
	}

	char* buf = new char[len + 1];
	file.Read(buf, len);
	file.Close();
	buf[len] = '\0';

	char* line = buf;
	char* nextline = NULL;

	for (; line; line = nextline)
	{
		nextline = strnextline(line);
		
		// Skip empty lines and comments
		while (*line == ' ' || *line == '\t')
			line++;
		if (*line == '\0' || *line == '#' || *line == ';')
			continue;

		// Parse: name=url or name=url,enabled
		char* eq = strchr(line, '=');
		if (!eq)
			continue;

		*eq = '\0';
		char* name = line;
		char* url = eq + 1;

		// Trim trailing spaces from name
		char* tail = eq - 1;
		while (tail >= name && (*tail == ' ' || *tail == '\t'))
		{
			*tail = '\0';
			tail--;
		}

		// Skip leading spaces in url
		while (*url == ' ' || *url == '\t')
			url++;

		if (*name == '\0' || *url == '\0')
			continue;

		// Check for enabled flag (,0 or ,1 at end)
		BOOL enabled = TRUE;
		char* comma = strrchr(url, ',');
		if (comma)
		{
			char* flag = comma + 1;
			while (*flag == ' ' || *flag == '\t')
				flag++;
			if (*flag == '0')
			{
				enabled = FALSE;
				*comma = '\0';
			}
			else if (*flag == '1')
			{
				*comma = '\0';
			}
		}

		// Trim trailing spaces from url
		tail = url + strlen(url) - 1;
		while (tail >= url && (*tail == ' ' || *tail == '\t'))
		{
			*tail = '\0';
			tail--;
		}

		if (*url)
		{
			CTranslationSite site(name, url, enabled);
			m_sites.Add(site);
		}
	}

	delete[] buf;
	return TRUE;
}

//////////////////////////////////////////////////////////////////////
// Save sites to configuration file
//////////////////////////////////////////////////////////////////////

BOOL CTranslationSiteManager::Save(LPCTSTR configPath)
{
	CFile file;
	if (!file.Open(configPath, CFile::modeCreate | CFile::modeWrite))
	{
		return FALSE;
	}

	CString header = "# Translation and Query Sites Configuration\r\n";
	header += "# Format: DisplayName=URLTemplate\r\n";
	header += "# Use {searchTerms} as placeholder for search text\r\n";
	header += "# Add ,0 at end to disable a site\r\n";
	header += "#\r\n\r\n";
	file.Write(LPCTSTR(header), header.GetLength());

	for (int i = 0; i < m_sites.GetSize(); i++)
	{
		const CTranslationSite& site = m_sites[i];
		CString line;
		if (site.enabled)
			line.Format("%s=%s\r\n", LPCTSTR(site.name), LPCTSTR(site.url));
		else
			line.Format("%s=%s,0\r\n", LPCTSTR(site.name), LPCTSTR(site.url));
		file.Write(LPCTSTR(line), line.GetLength());
	}

	file.Close();
	return TRUE;
}

//////////////////////////////////////////////////////////////////////
// Create default configuration if none exists
//////////////////////////////////////////////////////////////////////

void CTranslationSiteManager::CreateDefaultConfig(LPCTSTR configPath)
{
	// Check if file already exists
	CFile file;
	if (file.Open(configPath, CFile::modeRead))
	{
		file.Close();
		return;
	}

	// Add default sites
	m_sites.RemoveAll();
	
	// Translation sites
	m_sites.Add(CTranslationSite("Dr.eye 譯典通", "http://www.dreye.com/axis/ddict.jsp?ver=big5&srh_w={searchTerms}"));
	m_sites.Add(CTranslationSite("Google 翻譯", "https://translate.google.com/?sl=auto&tl=zh-TW&text={searchTerms}"));
	
	// Dictionary/Query sites
	m_sites.Add(CTranslationSite("Yahoo奇摩字典", "https://tw.dictionary.search.yahoo.com/search?p={searchTerms}"));
	m_sites.Add(CTranslationSite("維基百科", "https://zh.wikipedia.org/wiki/{searchTerms}"));
	m_sites.Add(CTranslationSite("Google 搜尋", "https://www.google.com/search?q={searchTerms}"));

	// Save the default configuration
	Save(configPath);
}

//////////////////////////////////////////////////////////////////////
// Get URL for a specific site with search term substituted
//////////////////////////////////////////////////////////////////////

CString CTranslationSiteManager::GetUrl(int index, LPCTSTR searchTerm)
{
	if (index < 0 || index >= m_sites.GetSize())
		return "";

	CString url = m_sites[index].url;
	url.Replace("{searchTerms}", searchTerm);
	return url;
}

//////////////////////////////////////////////////////////////////////
// Get the translation URL (first enabled site)
//////////////////////////////////////////////////////////////////////

CString CTranslationSiteManager::GetTranslationUrl(LPCTSTR searchTerm)
{
	for (int i = 0; i < m_sites.GetSize(); i++)
	{
		if (m_sites[i].enabled)
		{
			return GetUrl(i, searchTerm);
		}
	}
	// Fallback if no sites are configured
	CString url = "https://translate.google.com/?sl=auto&tl=zh-TW&text=";
	url += searchTerm;
	return url;
}

//////////////////////////////////////////////////////////////////////
// Get site by index
//////////////////////////////////////////////////////////////////////

const CTranslationSite* CTranslationSiteManager::GetSite(int index) const
{
	if (index < 0 || index >= m_sites.GetSize())
		return NULL;
	return &m_sites[index];
}

//////////////////////////////////////////////////////////////////////
// Get enabled site count
//////////////////////////////////////////////////////////////////////

int CTranslationSiteManager::GetEnabledCount() const
{
	int count = 0;
	for (int i = 0; i < m_sites.GetSize(); i++)
	{
		if (m_sites[i].enabled)
			count++;
	}
	return count;
}

//////////////////////////////////////////////////////////////////////
// Create translation/query sites submenu
//////////////////////////////////////////////////////////////////////

HMENU CTranslationSiteManager::CreateTranslationSitesMenu()
{
	HMENU menu = CreatePopupMenu();
	
	int menuIndex = 0;
	for (int i = 0; i < m_sites.GetSize() && menuIndex < 16; i++)
	{
		if (!m_sites[i].enabled)
			continue;
			
		InsertMenu(menu, menuIndex, MF_BYPOSITION | MF_STRING, 
			ID_TRANSLATIONSITE00 - menuIndex, m_sites[i].name);
		menuIndex++;
	}
	
	return menu;
}
