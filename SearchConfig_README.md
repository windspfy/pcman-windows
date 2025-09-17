# PCMan Search and Translation Configuration

This document describes the new configurable search engine and translation features added to PCMan.

## New Features

### 1. Configurable Search Engines

Search engines are now loaded from an external configuration file instead of being hard-coded. The configuration file is located at:

```
Config/SearchEngines.ini
```

#### Format

The configuration file uses a simple INI-style format:

```ini
; Search Engine Configuration
; Format: name=url
; Lines starting with ; or # are comments

Google=https://www.google.com/search?q={searchTerms}
百度=https://www.baidu.com/s?wd={searchTerms}
Yahoo=https://search.yahoo.com/search?p={searchTerms}
Bing=https://www.bing.com/search?q={searchTerms}
Wikipedia=https://zh.wikipedia.org/wiki/{searchTerms}
```

#### URL Placeholders

- `{searchTerms}` - will be replaced with the selected text (URL encoded)
- `{searchterm}` - alternative placeholder format

#### Default Configuration

If the configuration file doesn't exist, PCMan will create it with default search engines automatically on first run.

### 2. Translation Toggle

A new configuration option has been added to enable/disable the instant translation feature:

```ini
[General]
enable_translation=1
```

When `enable_translation=0`, the translation option will not appear in the right-click context menu.

## Implementation Details

### New Files Added

- `Lite/SearchEngineConfig.h` - Header for search engine configuration management
- `Lite/SearchEngineConfig.cpp` - Implementation of search engine configuration management
- `Config/SearchEngines.ini` - Default search engine configuration file

### Modified Files

- `Lite/AppConfig.h` - Added `enable_translation` configuration option
- `Lite/AppConfig.cpp` - Added configuration handling for translation toggle
- `Lite/SearchPlugin.h` - Added new methods for config-based search menus
- `Lite/SearchPlugin.cpp` - Implemented config-based search functionality
- `Lite/TermView.cpp` - Updated context menu logic to use new features
- `Lite/PCMan.cpp` - Added initialization of search engine configuration
- `Combo/SearchBar.cpp` - Updated to use new config-based search menu
- Project files updated to include new source files

### Backwards Compatibility

The implementation maintains backwards compatibility:

- If the search engine configuration file doesn't exist, default engines are created
- The original XML-based search plugins still work as a fallback
- Translation feature is enabled by default

## Usage

### Adding Custom Search Engines

1. Open `Config/SearchEngines.ini` in a text editor
2. Add a new line in the format: `Name=URL`
3. Use `{searchTerms}` as placeholder for the search text
4. Save the file
5. Restart PCMan

### Disabling Translation

1. Open the main configuration file (usually `Config/Config.ini`)
2. Find the `[General]` section
3. Set `enable_translation=0`
4. Save and restart PCMan

### Example Custom Search Engine

```ini
GitHub=https://github.com/search?q={searchTerms}
Stack Overflow=https://stackoverflow.com/search?q={searchTerms}
```