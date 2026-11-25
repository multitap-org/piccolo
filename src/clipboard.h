#pragma once

// Text to the system clipboard. SDL's on desktop and Android; in the browser
// SDL has none of its own (it would keep the text inside the page), so
// clipboard_web.cpp asks the browser (navigator.clipboard), which allows it
// right after a click, as when a button calls this.
//
// True when the text is there. The browser answers later (a promise), so
// there it's true only when the write was asked right after a user gesture
// (a click, a key), the only time it's reliably allowed; at any other time
// it's still tried, but false.
bool clipboard_set_text(const char* text);
