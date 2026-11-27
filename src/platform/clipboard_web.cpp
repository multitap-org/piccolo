#include "clipboard.h"

#include <emscripten/emscripten.h>

// On the page's main thread (piccolo's loop runs there). Only on https pages
// (or localhost): elsewhere navigator.clipboard doesn't exist and nothing
// happens. 1 when the write was asked during a user gesture
// (navigator.userActivation: a click or a key a moment ago), the only time
// the browser reliably allows it; a browser that can't tell counts as 0.
EM_JS(int, clipboard_web_write, (const char* text), {
    if (!navigator.clipboard || !navigator.clipboard.writeText)
    {
        return 0;
    }
    var gesture = navigator.userActivation ? navigator.userActivation.isActive : false;
    navigator.clipboard.writeText(UTF8ToString(text)).catch(function(error) {
        console.warn('piccolo: clipboard: ' + error);
    });
    return gesture ? 1 : 0;
});

bool clipboard_set_text(const char* text)
{
    return clipboard_web_write(text) != 0;
}
