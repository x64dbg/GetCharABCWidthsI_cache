#include "plugin.h"
#include "minhook/MinHook.h"
#include <unordered_map>

typedef HGDIOBJ (WINAPI *p_SelectObject)(
    HDC hdc, 
    HGDIOBJ h);

typedef BOOL(WINAPI *p_GetCharABCWidthsI)(
    HDC hdc,
    UINT giFirst,
    UINT cgi,
    LPWORD pgi,
    LPABC pabc);

typedef BOOL(WINAPI *p_SystemParametersInfoW)(
	UINT  uiAction,
	UINT  uiParam,
	PVOID pvParam,
	UINT  fWinIni
	);

static p_SelectObject original_SelectObject = nullptr;
static p_GetCharABCWidthsI original_GetCharABCWidthsI = nullptr;
static p_SystemParametersInfoW original_SystemParametersInfoW = nullptr;

struct FontData
{
    unsigned int count = 0;
    unsigned int hits = 0;
    unsigned int misses = 0;
    std::unordered_map<UINT, ABC> cache;
};

static HGDIOBJ curHdc = nullptr;
static FontData* curFont = nullptr;
static std::unordered_map<HGDIOBJ, FontData> fontData;
static DWORD tid = 0;

static bool checkThread()
{
    return tid == GetCurrentThreadId();
}

static HGDIOBJ WINAPI hook_SelectObject(
    HDC hdc,
    HGDIOBJ h)
{
    auto result = original_SelectObject(hdc, h);
    if(checkThread())
    {
        auto found = fontData.find(h);
        if(found != fontData.end())
        {
            curHdc = hdc;
            curFont = &found->second;
        }
    }
    return result;
}

static BOOL WINAPI hook_GetCharABCWidthsI(
    __in HDC hdc,
    __in UINT giFirst,
    __in UINT cgi,
    __in_ecount_opt(cgi) LPWORD pgi,
    __out_ecount(cgi) LPABC pabc)
{
    //Initialize the thread ID.
    if(tid == 0)
        tid = GetCurrentThreadId();

    //Don't cache if called from a different thread
    if(!checkThread())
        return original_GetCharABCWidthsI(hdc, giFirst, cgi, pgi, pabc);

    //Get the current font object and get a (new) pointer to the cache
    if(!curFont || curHdc != hdc)
    {
        auto hFont = GetCurrentObject(hdc, OBJ_FONT);
        auto found = fontData.find(hFont);
        if(found == fontData.end())
            found = fontData.insert({ hFont, FontData() }).first;
        curFont = &found->second;
    }
    curFont->count++;

    //Copy the current FontData pointer (https://github.com/x64dbg/GetCharABCWidthsI_cache/issues/1)
    auto font = curFont;

    //Functions to lookup/store glyph index data with the cache
    bool allCached = true;
    auto lookupGlyphIndex = [&allCached, font](UINT index, ABC & result)
    {
        auto found = font->cache.find(index);
        if(found == font->cache.end())
            return allCached = false;
        result = found->second;
        return true;
    };
    auto storeGlyphIndex = [font](UINT index, ABC & result)
    {
        font->cache[index] = result;
    };

    //A pointer to an array that contains glyph indices.
    //If this parameter is NULL, the giFirst parameter is used instead.
    //The cgi parameter specifies the number of glyph indices in this array.
    if(pgi == NULL)
    {
        for(UINT i = 0; i < cgi; i++)
            if(!lookupGlyphIndex(giFirst + i, pabc[i]))
                break;
    }
    else
    {
        for(UINT i = 0; i < cgi; i++)
            if(!lookupGlyphIndex(pgi[i], pabc[i]))
                break;
    }

    //If everything was cached we don't have to call the original
    if(allCached)
    {
        font->hits++;
        return TRUE;
    }

    font->misses++;

    //Call original function
    auto result = original_GetCharABCWidthsI(hdc, giFirst, cgi, pgi, pabc);
    if(!result)
        return FALSE;

    //A pointer to an array that contains glyph indices.
    //If this parameter is NULL, the giFirst parameter is used instead.
    //The cgi parameter specifies the number of glyph indices in this array.
    if(pgi == NULL)
    {
        for(UINT i = 0; i < cgi; i++)
            storeGlyphIndex(giFirst + i, pabc[i]);
    }
    else
    {
        for(UINT i = 0; i < cgi; i++)
            storeGlyphIndex(pgi[i], pabc[i]);
    }

    return TRUE;
}

BOOL WINAPI hook_SystemParametersInfoW(
	_In_    UINT  uiAction,
	_In_    UINT  uiParam,
	_Inout_ PVOID pvParam,
	_In_    UINT  fWinIni
)
{
	switch (uiAction)
	{
	case SPI_GETCLIENTAREAANIMATION:
		break;
	case SPI_GETKEYBOARDCUES:
		break;
	case SPI_SETFONTSMOOTHINGCONTRAST:
		break;
	case SPI_GETNONCLIENTMETRICS: //perf issues
	{
		static NONCLIENTMETRICSW cache = { 0 };
		if (cache.cbSize == 0)
		{
			auto retVal = original_SystemParametersInfoW(uiAction, uiParam, pvParam, fWinIni);
			if (retVal)
				memcpy(&cache, pvParam, uiParam);
		}
		else 
		{
			memcpy(pvParam, &cache, uiParam);
			return TRUE;
		}
	}	
	}
	return original_SystemParametersInfoW(uiAction, uiParam, pvParam, fWinIni);
}

static bool cbCharABCCounter(int argc, char* argv[])
{
    _plugin_logprintf("font count: %d\n", int(fontData.size()));
    for(auto & it : fontData)
    {
        _plugin_logprintf("\nHGDIOBJ: %p\n", it.first);
        FontData & data = it.second;
        _plugin_logprintf("count: %u, hits: %u, misses: %u\n", data.count, data.hits, data.misses);
    }
    return true;
}

//Initialize your plugin data here.
bool pluginInit(PLUG_INITSTRUCT* initStruct)
{
    if(!_plugin_registercommand(pluginHandle, "abcdata", cbCharABCCounter, false))
        return false;
    if(MH_Initialize() != MH_OK)
        return false;
    if(MH_CreateHook(&SelectObject, &hook_SelectObject, (LPVOID*)&original_SelectObject) != MH_OK)
        return false;
    if(MH_CreateHook(&GetCharABCWidthsI, &hook_GetCharABCWidthsI, (LPVOID*)&original_GetCharABCWidthsI) != MH_OK)
        return false;
	if (MH_CreateHook(&SystemParametersInfoW, &hook_SystemParametersInfoW, (LPVOID*)&original_SystemParametersInfoW) != MH_OK)
		return false;
    if(MH_EnableHook(MH_ALL_HOOKS) != MH_OK)
        return false;
    return true;
}

//Deinitialize your plugin data here (clearing menus optional).
bool pluginStop()
{
    MH_DisableHook(MH_ALL_HOOKS);
    MH_Uninitialize();
    return true;
}

//Do GUI/Menu related things here.
void pluginSetup()
{
}
