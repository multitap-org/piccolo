#include "net_caps_ui.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "piccolo_text.h"
#include "retro_ui.h"
#include "retro_ui_controls.h"
#include "ui_toolkit.h"

static const Str CAP_LABELS[NET_CAP_COUNT] = {Str::cap_gamepad, Str::cap_stream, Str::cap_settings, Str::cap_share};

const char* net_caps_ui_label(const Localization* strings, NetCaps cap)
{
    for (uint32_t i = 0; i < NET_CAP_COUNT; i++)
    {
        if (cap == (1u << i))
        {
            return piccolo_text(strings, CAP_LABELS[i]);
        }
    }
    return piccolo_text(strings, Str::cap_none);
}

void net_caps_ui_list(const Localization* strings, NetCaps caps, char* out, size_t size)
{
    size_t used = 0;
    out[0] = '\0';
    for (uint32_t i = 0; i < NET_CAP_COUNT && used < size; i++)
    {
        if ((caps & (1u << i)) == 0)
        {
            continue;
        }
        int written = std::snprintf(out + used, size - used, "%s%s", used > 0 ? ", " : "",
                                    piccolo_text(strings, CAP_LABELS[i]));
        used += written > 0 ? static_cast<size_t>(written) : 0;
    }
    if (used == 0)
    {
        std::snprintf(out, size, "%s", piccolo_text(strings, Str::cap_none));
    }
}

const char* net_caps_ui_kind(const Localization* strings, NetAppKind kind)
{
    switch (kind)
    {
    case NetAppKind::piccolo:
        return piccolo_text(strings, Str::kind_piccolo);
    case NetAppKind::piccoletto:
        return piccolo_text(strings, Str::kind_piccoletto);
    case NetAppKind::piccolino:
        return piccolo_text(strings, Str::kind_piccolino);
    }
    return piccolo_text(strings, Str::kind_piccolo);
}

NetCaps net_caps_ui_kind_offers(NetAppKind kind)
{
    switch (kind)
    {
    case NetAppKind::piccolo:
        return NET_CAPS_ALL;
    case NetAppKind::piccoletto:
        return 0;
    case NetAppKind::piccolino:
        return NET_CAP_SHARE | NET_CAP_SETTINGS;
    }
    return 0;
}

NetCaps net_caps_ui_kind_uses(NetAppKind kind)
{
    switch (kind)
    {
    case NetAppKind::piccolo:
        return NET_CAP_SHARE | NET_CAP_SETTINGS;
    case NetAppKind::piccoletto:
        return NET_CAP_GAMEPAD | NET_CAP_STREAM | NET_CAP_SETTINGS;
    case NetAppKind::piccolino:
        return 0;
    }
    return 0;
}

bool net_caps_ui_checkboxes(const char* id, const Localization* strings, NetCaps choices, bool signed_build,
                            NetCaps* value, NetCaps* asked)
{
    bool changed = false;
    retro_ui_push_id(id);
    float left = retro_ui_cursor_x();
    float half = std::floor((retro_ui_width_left() - retro_ui_item_spacing()) * 0.5f);
    uint32_t shown = 0;
    for (uint32_t i = 0; i < NET_CAP_COUNT; i++)
    {
        NetCaps bit = 1u << i;
        if ((choices & bit) == 0)
        {
            continue;
        }
        if (shown % 2 == 1)
        {
            retro_ui_same_line_at(left + half + retro_ui_item_spacing());
        }
        bool ticked = (*value & bit) != 0;
        retro_ui_push_index(i);
        if (retro_ui_check("cap", piccolo_text(strings, CAP_LABELS[i]), &ticked))
        {
            if (!ticked)
            {
                *value &= ~bit;
                changed = true;
            }
            else if (!signed_build && (bit & (NET_CAP_SHARE | NET_CAP_SETTINGS)) != 0)
            {
                *asked = bit;   // the red question first (net_caps_ui_unsigned)
            }
            else
            {
                *value |= bit;
                changed = true;
            }
        }
        retro_ui_pop_id();
        shown++;
    }
    retro_ui_pop_id();
    return changed;
}

NetCaps net_caps_ui_unsigned(NetCapsUnsignedAsk* ask, const Localization* strings)
{
    if (ask->cap == 0)
    {
        return 0;
    }
    char text[1024];
    std::snprintf(text, sizeof(text), "%s %s\n\n%s\n\n%s", ask->name.c_str(), piccolo_text(strings, Str::unsigned_runs),
                  piccolo_text(strings, ask->cap == NET_CAP_SETTINGS ? Str::unsigned_settings : Str::unsigned_share),
                  piccolo_text(strings, Str::unsigned_trust));
    float screen_width = 0.0f;
    float screen_height = 0.0f;
    ui_toolkit_screen_size(&screen_width, &screen_height);
    RetroUiQuestion question = {};
    question.id = "unsigned_build";
    question.title = piccolo_text(strings, Str::unsigned_build);
    question.text = text;
    question.yes = piccolo_text(strings, Str::unsigned_grant);
    question.no = piccolo_text(strings, Str::unsigned_cancel);
    question.width = static_cast<float>(static_cast<int>(screen_width * 0.4f));
    // The theme's red twin, for this question only (Andrés: a red hue that
    // matches the theme). Escape (B on a gamepad) answers CANCEL.
    RetroUiPalette red;
    retro_ui_danger_palette(retro_ui_palette(), &red);
    retro_ui_push_palette(&red);
    RetroUiAnswer answer = retro_ui_question(&question, true);
    retro_ui_pop_palette();
    if (answer == RetroUiAnswer::none)
    {
        return 0;
    }
    NetCaps granted = answer == RetroUiAnswer::yes ? ask->cap : 0;
    ask->cap = 0;
    return granted;
}
