#include "state_widget.h"

#include "hud_view.h"

static bool shown(const StateWidget* widget, StateIcon icon, bool fact)
{
    return fact || widget->debug_show[static_cast<uint32_t>(icon)];
}

void state_widget_add_hud(const StateWidget* widget, const StateFacts* facts, HudStack* stack, uint64_t now_ms)
{
    if (shown(widget, StateIcon::pause, facts->paused))
    {
        hud_view_add_icon(stack, HudIcon::pause, "");
    }
    if (shown(widget, StateIcon::rewind, facts->rewinding))
    {
        hud_view_add_icon(stack, HudIcon::rewind, "");
    }
    bool playing_on = facts->played_from_ms != 0 && now_ms >= facts->played_from_ms &&
                      now_ms - facts->played_from_ms < STATE_WIDGET_PLAY_MS;
    playing_on = playing_on || (facts->resumed_ms != 0 && now_ms >= facts->resumed_ms &&
                                now_ms - facts->resumed_ms < STATE_WIDGET_RESUMED_MS);
    if (shown(widget, StateIcon::play, playing_on))
    {
        hud_view_add_icon(stack, HudIcon::play, "");
    }
    if (shown(widget, StateIcon::fast_forward, facts->fast_forwarding))
    {
        hud_view_add_icon(stack, HudIcon::fast_forward, "");
    }
}
