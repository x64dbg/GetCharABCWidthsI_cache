# GetCharABCWidthsI_cache

This plugin will hook the `GetCharABCWidthsI` function in `gdi32.dll` and cache the results. This is because `QWindowsFontEngine::getGlyphBearings` doesn't cache internally. A post with more details has been posted on the [x64dbg blog](http://x64dbg.com/blog/2017/03/18/caching.html).

Before:

![before](https://i.imgur.com/SZ2rMJz.png)

After:

![after](https://i.imgur.com/NfAk0nX.png)