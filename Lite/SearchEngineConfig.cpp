#include "stdafx.h"
#include "SearchEngineConfig.h"
#include "StrUtils.h"

// Global instance
CSearchEngineConfig SearchEngineConfig;

CSearchEngineConfig::CSearchEngineConfig()
{
}

CSearchEngineConfig::~CSearchEngineConfig()
{
}

bool CSearchEngineConfig::OnDataExchange(bool load)
{
	if (load)
	{
		// Load configuration
		Clear();
		
		// First try to load from config file
		CFile file;
		CString configPath = GetFilePath();
		if (file.Open(configPath, CFile::modeRead | CFile::typeBinary))
		{
			char buffer[1024];
			UINT bytesRead;
			CString content;
			
			while ((bytesRead = file.Read(buffer, sizeof(buffer) - 1)) > 0)
			{
				buffer[bytesRead] = '\0';
				content += buffer;
			}
			file.Close();
			
			// Parse INI-style content
			CString line;
			int pos = 0;
			int engineIndex = 0;
			
			while (pos < content.GetLength())
			{
				int nextPos = content.Find('\n', pos);
				if (nextPos == -1) nextPos = content.GetLength();
				
				line = content.Mid(pos, nextPos - pos);
				line.TrimLeft();
				line.TrimRight();
				
				if (!line.IsEmpty() && line[0] != ';' && line[0] != '#')
				{
					// Look for engine entries in format: name=url
					int equalPos = line.Find('=');
					if (equalPos > 0)
					{
						CString name = line.Left(equalPos);
						CString url = line.Mid(equalPos + 1);
						name.TrimRight();
						url.TrimLeft();
						
						if (!name.IsEmpty() && !url.IsEmpty())
						{
							SearchEngineEntry engine(name, url, "", true);
							AddEngine(engine);
						}
					}
				}
				
				pos = nextPos + 1;
			}
		}
		
		// If no engines loaded, use defaults
		if (GetCount() == 0)
		{
			LoadDefaults();
		}
	}
	else
	{
		// Save configuration
		CFile file;
		CString configPath = GetFilePath();
		
		if (file.Open(configPath, CFile::modeCreate | CFile::modeWrite | CFile::typeBinary))
		{
			CString header = "; Search Engine Configuration\r\n"
						   "; Format: name=url\r\n"
						   "; Lines starting with ; or # are comments\r\n\r\n";
			file.Write(header, header.GetLength());
			
			for (int i = 0; i < GetCount(); i++)
			{
				const SearchEngineEntry& engine = GetEngine(i);
				if (engine.enabled)
				{
					CString line;
					line.Format("%s=%s\r\n", engine.name, engine.url);
					file.Write(line, line.GetLength());
				}
			}
			file.Close();
		}
	}
	
	return true;
}

void CSearchEngineConfig::LoadDefaults()
{
	// Add default search engines
	AddEngine(SearchEngineEntry("Google", "https://www.google.com/search?q={searchTerms}"));
	AddEngine(SearchEngineEntry("百度", "https://www.baidu.com/s?wd={searchTerms}"));
	AddEngine(SearchEngineEntry("Yahoo", "https://search.yahoo.com/search?p={searchTerms}"));
	AddEngine(SearchEngineEntry("Bing", "https://www.bing.com/search?q={searchTerms}"));
	AddEngine(SearchEngineEntry("Wikipedia", "https://zh.wikipedia.org/wiki/{searchTerms}"));
}