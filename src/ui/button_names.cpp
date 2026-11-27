#include "button_names.h"

#include <cctype>
#include <cstdio>
#include <cstring>

#include "input_layouts.h"

static void upper(char* text)
{
    for (char* c = text; *c != '\0'; c++)
    {
        *c = static_cast<char>(std::toupper(static_cast<unsigned char>(*c)));
    }
}

static const char* face_label(ButtonFaceLabel label, const char* fallback)
{
    switch (label)
    {
    case ButtonFaceLabel::a:        return "A";
    case ButtonFaceLabel::b:        return "B";
    case ButtonFaceLabel::x:        return "X";
    case ButtonFaceLabel::y:        return "Y";
    case ButtonFaceLabel::cross:    return BUTTON_NAME_CROSS;
    case ButtonFaceLabel::circle:   return BUTTON_NAME_CIRCLE;
    case ButtonFaceLabel::square:   return BUTTON_NAME_SQUARE;
    case ButtonFaceLabel::triangle: return BUTTON_NAME_TRIANGLE;
    case ButtonFaceLabel::unknown:  return fallback;
    }
    return fallback;
}

// The shoulders and triggers by the pad's family: index 0 L, 1 R, 2 L2, 3 R2.
static const char* shoulder_label(ButtonFamily family, uint32_t index)
{
    static const char* const OTHER[4]       = {"L", "R", "L2", "R2"};
    static const char* const XBOX[4]        = {"LB", "RB", "LT", "RT"};
    static const char* const PLAYSTATION[4] = {"L1", "R1", "L2", "R2"};
    static const char* const NINTENDO[4]    = {"L", "R", "ZL", "ZR"};
    switch (family)
    {
    case ButtonFamily::xbox:        return XBOX[index];
    case ButtonFamily::playstation: return PLAYSTATION[index];
    case ButtonFamily::nintendo:    return NINTENDO[index];
    case ButtonFamily::other:       return OTHER[index];
    }
    return OTHER[index];
}

// The layout control behind a source, for its label (count = none).
static InputMapControl layout_control(InputSource source)
{
    if (source.type == InputSourceType::button)
    {
        switch (static_cast<InputButton>(source.code))
        {
        case InputButton::south:          return InputMapControl::south;
        case InputButton::east:           return InputMapControl::east;
        case InputButton::west:           return InputMapControl::west;
        case InputButton::north:          return InputMapControl::north;
        case InputButton::back:           return InputMapControl::back;
        case InputButton::guide:          return InputMapControl::guide;
        case InputButton::start:          return InputMapControl::start;
        case InputButton::left_stick:     return InputMapControl::left_stick;
        case InputButton::right_stick:    return InputMapControl::right_stick;
        case InputButton::left_shoulder:  return InputMapControl::left_shoulder;
        case InputButton::right_shoulder: return InputMapControl::right_shoulder;
        default:                          return InputMapControl::count;
        }
    }
    if (source.type == InputSourceType::axis_plus && source.code == static_cast<uint16_t>(InputAxis::left_trigger))
    {
        return InputMapControl::left_trigger;
    }
    if (source.type == InputSourceType::axis_plus && source.code == static_cast<uint16_t>(InputAxis::right_trigger))
    {
        return InputMapControl::right_trigger;
    }
    return InputMapControl::count;
}

void button_names_source(const InputLayout* layout, const InputProfile* profile, const ButtonNamesPad* pad,
                         InputSource source, char* out, size_t out_size)
{
    if (profile != nullptr && source.type == InputSourceType::button && source.code < INPUT_BUTTON_COUNT &&
        !profile->button_labels[source.code].empty())
    {
        std::snprintf(out, out_size, "%s", profile->button_labels[source.code].c_str());   // a name given to it
        return;
    }
    InputMapControl control = layout_control(source);
    if (layout != nullptr && control != InputMapControl::count && !layout->labels[static_cast<uint32_t>(control)].empty())
    {
        std::snprintf(out, out_size, "%s", layout->labels[static_cast<uint32_t>(control)].c_str());
        return;
    }
    ButtonNamesPad unknown = {};
    if (pad == nullptr)
    {
        pad = &unknown;
    }
    const char* text = "?";
    char key[32] = {};
    switch (source.type)
    {
    case InputSourceType::key:
    {
        const char* name = input_key_name(source.code);
        if (name != nullptr)
        {
            std::snprintf(key, sizeof(key), "%s", name);
        }
        else
        {
            std::snprintf(key, sizeof(key), "KEY %u", static_cast<unsigned>(source.code));
        }
        upper(key);
        text = key;
        break;
    }
    case InputSourceType::button:
        switch (static_cast<InputButton>(source.code))
        {
        case InputButton::south:          text = face_label(pad->south, "BOTTOM BUTTON"); break;
        case InputButton::east:           text = face_label(pad->east, "RIGHT BUTTON"); break;
        case InputButton::west:           text = face_label(pad->west, "LEFT BUTTON"); break;
        case InputButton::north:          text = face_label(pad->north, "TOP BUTTON"); break;
        case InputButton::back:           text = "SELECT"; break;
        case InputButton::guide:          text = "HOME"; break;
        case InputButton::start:          text = "START"; break;
        case InputButton::left_stick:     text = "L3"; break;
        case InputButton::right_stick:    text = "R3"; break;
        case InputButton::left_shoulder:  text = shoulder_label(pad->family, 0); break;
        case InputButton::right_shoulder: text = shoulder_label(pad->family, 1); break;
        case InputButton::dpad_up:        text = "D-PAD UP"; break;
        case InputButton::dpad_down:      text = "D-PAD DOWN"; break;
        case InputButton::dpad_left:      text = "D-PAD LEFT"; break;
        case InputButton::dpad_right:     text = "D-PAD RIGHT"; break;
        case InputButton::misc1:          text = "MISC 1"; break;
        case InputButton::right_paddle1:  text = "RIGHT PADDLE 1"; break;
        case InputButton::left_paddle1:   text = "LEFT PADDLE 1"; break;
        case InputButton::right_paddle2:  text = "RIGHT PADDLE 2"; break;
        case InputButton::left_paddle2:   text = "LEFT PADDLE 2"; break;
        case InputButton::touchpad:       text = "TOUCHPAD"; break;
        case InputButton::misc2:          text = "MISC 2"; break;
        case InputButton::misc3:          text = "MISC 3"; break;
        case InputButton::misc4:          text = "MISC 4"; break;
        case InputButton::misc5:          text = "MISC 5"; break;   // MAP CONTROLLER names them L4 / R4 when they are
        case InputButton::misc6:          text = "MISC 6"; break;   // the extra shoulders (the profile's labels)
        case InputButton::count:          break;
        }
        break;
    case InputSourceType::mouse:
        switch (static_cast<InputMouseButton>(source.code))
        {
        case InputMouseButton::left:       text = "MOUSE LEFT"; break;
        case InputMouseButton::right:      text = "MOUSE RIGHT"; break;
        case InputMouseButton::middle:     text = "MOUSE MIDDLE"; break;
        case InputMouseButton::x1:         text = "MOUSE BACK"; break;
        case InputMouseButton::x2:         text = "MOUSE FORWARD"; break;
        case InputMouseButton::wheel_up:   text = "WHEEL UP"; break;
        case InputMouseButton::wheel_down: text = "WHEEL DOWN"; break;
        case InputMouseButton::count:      break;
        }
        break;
    case InputSourceType::axis_plus:
    case InputSourceType::axis_minus:
    {
        bool plus = source.type == InputSourceType::axis_plus;
        switch (static_cast<InputAxis>(source.code))
        {
        case InputAxis::left_x:        text = plus ? "LEFT STICK RIGHT" : "LEFT STICK LEFT"; break;
        case InputAxis::left_y:        text = plus ? "LEFT STICK DOWN" : "LEFT STICK UP"; break;
        case InputAxis::right_x:       text = plus ? "RIGHT STICK RIGHT" : "RIGHT STICK LEFT"; break;
        case InputAxis::right_y:       text = plus ? "RIGHT STICK DOWN" : "RIGHT STICK UP"; break;
        case InputAxis::left_trigger:  text = shoulder_label(pad->family, 2); break;
        case InputAxis::right_trigger: text = shoulder_label(pad->family, 3); break;
        case InputAxis::count:         break;
        }
        break;
    }
    case InputSourceType::none:
        text = "";
        break;
    }
    std::snprintf(out, out_size, "%s", text);
}

bool button_names_first_source(const InputBindingSet* bindings, InputAction action, InputSource* source)
{
    *source = InputSource{};
    const InputSource* sources = bindings->sources[static_cast<uint32_t>(action)];
    for (uint32_t s = 0; s < INPUT_SOURCES_PER_ACTION; s++)
    {
        if (sources[s].type != InputSourceType::none)
        {
            *source = sources[s];
            return true;
        }
    }
    return false;
}

void button_names_pair(const char* left, const char* right, char* out, size_t out_size)
{
    if (std::strcmp(left, "PAGEUP") == 0 && std::strcmp(right, "PAGEDOWN") == 0)
    {
        std::snprintf(out, out_size, "PG UP DN");
        return;
    }
    std::snprintf(out, out_size, "%s %s", left, right);
}
