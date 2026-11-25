#include "clipboard.h"

#include <SDL3/SDL.h>

bool clipboard_set_text(const char* text)
{
    if (!SDL_SetClipboardText(text))
    {
        SDL_Log("Clipboard: %s", SDL_GetError());
        return false;
    }
    return true;
}
