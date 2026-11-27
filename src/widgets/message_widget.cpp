#include "message_widget.h"

#include <cstdio>
#include <cstring>

#include "hud_view.h"

void message_widget_post(MessageWidget* widget, const char* text, uint64_t now_ms)
{
    if (text == nullptr || text[0] == '\0')
    {
        return;
    }
    for (uint32_t i = 0; i < widget->count; i++)
    {
        if (std::strncmp(widget->entries[i].text, text, MESSAGE_WIDGET_TEXT - 1) == 0)
        {
            widget->entries[i].until_ms = now_ms + MESSAGE_WIDGET_SHOW_MS;   // the same again: shown longer
            return;
        }
    }
    if (widget->count == MESSAGE_WIDGET_MAX)
    {
        for (uint32_t i = 1; i < widget->count; i++)
        {
            widget->entries[i - 1] = widget->entries[i];
        }
        widget->count--;
    }
    MessageEntry* entry = &widget->entries[widget->count];
    std::snprintf(entry->text, sizeof(entry->text), "%s", text);
    entry->since_ms = now_ms;
    entry->until_ms = now_ms + MESSAGE_WIDGET_SHOW_MS;
    widget->count++;
}

void message_widget_add_hud(MessageWidget* widget, HudStack* stack, uint64_t now_ms)
{
    // What's gone, out (in order).
    uint32_t kept = 0;
    for (uint32_t i = 0; i < widget->count; i++)
    {
        if (widget->entries[i].until_ms > now_ms)
        {
            widget->entries[kept] = widget->entries[i];
            kept++;
        }
    }
    widget->count = kept;

    for (uint32_t i = 0; i < widget->count; i++)
    {
        const MessageEntry* entry = &widget->entries[i];
        HudCard card = {};
        hud_view_card_text(&card, entry->text);
        card.since_ms = entry->since_ms;
        uint64_t left = entry->until_ms - now_ms;
        card.alpha = left < MESSAGE_WIDGET_FADE_MS
                         ? static_cast<float>(left) / static_cast<float>(MESSAGE_WIDGET_FADE_MS)
                         : 1.0f;
        hud_view_add_card(stack, &card);
    }
}
