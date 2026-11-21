#include "web_pad.h"

// Desktop and Android: no page, no page pad (see web_pad.h).

void web_pad_attach(InputSystem* input)
{
    (void)input;
}

bool web_pad_active()
{
    return false;
}

bool web_pad_take_menu()
{
    return false;
}

void web_pad_tell_page(bool playing, double aspect)
{
    (void)playing;
    (void)aspect;
}
