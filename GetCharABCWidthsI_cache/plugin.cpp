#include "plugin.h"
#include "minhook/MinHook.h"
#include <unordered_map>

#ifdef _WIN64
#pragma comment(lib, "minhook/libMinHook.x64.lib")
#else
#pragma comment(lib, "minhook/libMinHook.x86.lib")
#endif //_WIN64

typedef BOOL(WINAPI *p_GetCharABCWidthsI)(
    HDC hdc,
    UINT giFirst,
    UINT cgi,
    LPWORD pgi,
    LPABC pabc);

struct FontData
{
    unsigned int count = 1;
    unsigned int hits = 0;
    unsigned int misses = 0;
    std::unordered_map<UINT, ABC> cache;
};

static p_GetCharABCWidthsI original_GetCharABCWidthsI = nullptr;
static std::unordered_map<HGDIOBJ, FontData> fontData;

static BOOL WINAPI hook_GetCharABCWidthsI(
    __in HDC hdc,
    __in UINT giFirst,
    __in UINT cgi,
    __in_ecount_opt(cgi) LPWORD pgi,
    __out_ecount(cgi) LPABC pabc)
{
    //Don't cache if called from a different thread
    static DWORD tid = GetCurrentThreadId();
    if(tid != GetCurrentThreadId())
        return original_GetCharABCWidthsI(hdc, giFirst, cgi, pgi, pabc);

    //Get the current font object and get a (new) pointer to the cache
    auto font = GetCurrentObject(hdc, OBJ_FONT);
    auto foundData = fontData.find(font);
    if(foundData == fontData.end())
        foundData = fontData.insert({ font, FontData() }).first;
    else
        foundData->second.count++;

    //Functions to lookup/store glyph index data with the cache
    bool allCached = true;
    auto lookupGlyphIndex = [&](UINT index, ABC & result)
    {
        auto found = foundData->second.cache.find(index);
        if(found == foundData->second.cache.end())
            return allCached = false;
        result = found->second;
        return true;
    };
    auto storeGlyphIndex = [&](UINT index, ABC & result)
    {
        foundData->second.cache[index] = result;
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
        foundData->second.hits++;
        return TRUE;
    }

    foundData->second.misses++;

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
    if(MH_CreateHook(&GetCharABCWidthsI, &hook_GetCharABCWidthsI, (LPVOID*)&original_GetCharABCWidthsI) != MH_OK)
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
