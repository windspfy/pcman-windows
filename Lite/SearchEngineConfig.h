#ifndef __SEARCHENGINECONFIG_H__
#define __SEARCHENGINECONFIG_H__

#include <afxcoll.h>
#include "ConfigFile.h"

// Search engine configuration entry
struct SearchEngineEntry
{
	CString name;
	CString url;
	CString description;
	bool enabled;
	
	SearchEngineEntry() : enabled(true) {}
	SearchEngineEntry(const CString& n, const CString& u, const CString& d = "", bool e = true)
		: name(n), url(u), description(d), enabled(e) {}
};

// Configuration class for search engines
class CSearchEngineConfig : public CConfigFile
{
public:
	CSearchEngineConfig();
	virtual ~CSearchEngineConfig();
	
	// Load/Save configuration
	virtual bool OnDataExchange(bool load);
	
	// Access methods
	int GetCount() const { return m_engines.GetSize(); }
	const SearchEngineEntry& GetEngine(int index) const { return m_engines[index]; }
	void AddEngine(const SearchEngineEntry& engine) { m_engines.Add(engine); }
	void RemoveEngine(int index) { m_engines.RemoveAt(index); }
	void Clear() { m_engines.RemoveAll(); }
	
	// Default search engines
	void LoadDefaults();
	
private:
	CArray<SearchEngineEntry, SearchEngineEntry&> m_engines;
	
	// Helper methods for config file operations
	void SaveEngine(CString& section, const SearchEngineEntry& engine, int index);
	void LoadEngine(char* section, int index);
};

extern CSearchEngineConfig SearchEngineConfig;

#endif // __SEARCHENGINECONFIG_H__