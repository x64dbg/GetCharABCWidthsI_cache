# GetCharABCWidthsI_cache

This plugin will hook the `GetCharABCWidthsI` function in `gdi32.dll` and cache the results. This is because `QWindowsFontEngine::getGlyphBearings` doesn't cache internally.

Before:

![before](https://i.imgur.com/SZ2rMJz.png)

After:

![after](https://i.imgur.com/NfAk0nX.png)