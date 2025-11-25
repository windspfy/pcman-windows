// TranslationSites.h: interface for translation and query site configuration
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_TRANSLATIONSITES_H__INCLUDED_)
#define AFX_TRANSLATIONSITES_H__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include <afxtempl.h>

// Structure to hold a single translation/query site configuration
struct CTranslationSite
{
	CString name;       // Display name in menu
	CString url;        // URL template with {searchTerms} placeholder
	BOOL enabled;       // Whether this site is enabled

	CTranslationSite() : enabled(TRUE) {}
	CTranslationSite(LPCTSTR _name, LPCTSTR _url, BOOL _enabled = TRUE)
		: name(_name), url(_url), enabled(_enabled) {}
};

// Menu item IDs for translation sites (after search plugins)
enum TranslationSiteMenuId
{
	ID_TRANSLATIONSITE_MENU = 65400,
	ID_TRANSLATIONSITE00 = ID_TRANSLATIONSITE_MENU - 1,
	ID_TRANSLATIONSITE01 = ID_TRANSLATIONSITE00 - 1,
	ID_TRANSLATIONSITE02 = ID_TRANSLATIONSITE01 - 1,
	ID_TRANSLATIONSITE03 = ID_TRANSLATIONSITE02 - 1,
	ID_TRANSLATIONSITE04 = ID_TRANSLATIONSITE03 - 1,
	ID_TRANSLATIONSITE05 = ID_TRANSLATIONSITE04 - 1,
	ID_TRANSLATIONSITE06 = ID_TRANSLATIONSITE05 - 1,
	ID_TRANSLATIONSITE07 = ID_TRANSLATIONSITE06 - 1,
	ID_TRANSLATIONSITE08 = ID_TRANSLATIONSITE07 - 1,
	ID_TRANSLATIONSITE09 = ID_TRANSLATIONSITE08 - 1,
	ID_TRANSLATIONSITE10 = ID_TRANSLATIONSITE09 - 1,
	ID_TRANSLATIONSITE11 = ID_TRANSLATIONSITE10 - 1,
	ID_TRANSLATIONSITE12 = ID_TRANSLATIONSITE11 - 1,
	ID_TRANSLATIONSITE13 = ID_TRANSLATIONSITE12 - 1,
	ID_TRANSLATIONSITE14 = ID_TRANSLATIONSITE13 - 1,
	ID_TRANSLATIONSITE15 = ID_TRANSLATIONSITE14 - 1,
	ID_TRANSLATIONSITE_LAST = ID_TRANSLATIONSITE15,
};

// Manager class for translation and query sites
class CTranslationSiteManager
{
public:
	CTranslationSiteManager();
	~CTranslationSiteManager();

	// Load sites from configuration file
	BOOL Load(LPCTSTR configPath);
	
	// Save sites to configuration file
	BOOL Save(LPCTSTR configPath);
	
	// Create default configuration if none exists
	void CreateDefaultConfig(LPCTSTR configPath);

	// Get URL for a specific site with search term substituted
	CString GetUrl(int index, LPCTSTR searchTerm);
	
	// Get the translation URL (first translation site)
	CString GetTranslationUrl(LPCTSTR searchTerm);
	
	// Get site count
	int GetCount() const { return (int)m_sites.GetSize(); }
	
	// Get site by index
	const CTranslationSite* GetSite(int index) const;
	
	// Get enabled site count
	int GetEnabledCount() const;
	
	// Create translation/query sites submenu
	HMENU CreateTranslationSitesMenu();

private:
	CArray<CTranslationSite, CTranslationSite&> m_sites;
};

// Global instance
extern CTranslationSiteManager g_TranslationSites;

// Configuration filename constant
extern const char TRANSLATION_SITES_FILENAME[];

// Maximum number of translation sites in menu
const int MAX_TRANSLATION_SITES_IN_MENU = 16;

#endif // !defined(AFX_TRANSLATIONSITES_H__INCLUDED_)
