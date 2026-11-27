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

bool web_pad_take_quick_menu()
{
    return false;
}

bool web_pad_take_main_menu()
{
    return false;
}

bool web_pad_take_game_dvr()
{
    return false;
}

bool web_pad_take_controllers()
{
    return false;
}

void web_pad_tell_page(bool playing, double aspect)
{
    (void)playing;
    (void)aspect;
}

void web_pad_set_vibrate(bool on)
{
    (void)on;
}

void web_page_set_theme(const RetroUiPalette* palette, const float look[4])
{
    (void)palette;
    (void)look;
}

void web_page_set_language(const char* code)
{
    (void)code;
}
