#include "qr_view.h"

#include <cmath>

#include "imgui.h"
#include "qrcodegen.h"

static_assert(QR_VIEW_BUFFER == qrcodegen_BUFFER_LEN_FOR_VERSION(QR_VIEW_MAX_VERSION),
              "the QR buffer size matches qrcodegen's");

bool qr_view_set_text(QrView* view, const char* text)
{
    if (view->text == text)
    {
        return view->ok;
    }
    view->text = text;
    uint8_t temp[QR_VIEW_BUFFER];
    view->ok = qrcodegen_encodeText(text, temp, view->modules, qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN,
                                    QR_VIEW_MAX_VERSION, qrcodegen_Mask_AUTO, true);
    return view->ok;
}

void qr_view_draw(const QrView* view)
{
    if (!view->ok)
    {
        return;
    }
    int size = qrcodegen_getSize(view->modules);
    float module = std::floor(ImGui::GetFontSize() / 3.0f);
    if (module < 2.0f)
    {
        module = 2.0f;
    }
    const int quiet = 4;
    float side = static_cast<float>(size + quiet * 2) * module;
    ImVec2 origin = ImGui::GetCursorScreenPos();
    origin.x = std::floor(origin.x);
    origin.y = std::floor(origin.y);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin, ImVec2(origin.x + side, origin.y + side), IM_COL32(255, 255, 255, 255));
    for (int y = 0; y < size; y++)
    {
        // Runs of dark modules on a row as one rectangle each.
        int x = 0;
        while (x < size)
        {
            if (!qrcodegen_getModule(view->modules, x, y))
            {
                x++;
                continue;
            }
            int start = x;
            while (x < size && qrcodegen_getModule(view->modules, x, y))
            {
                x++;
            }
            float left = origin.x + static_cast<float>(start + quiet) * module;
            float top = origin.y + static_cast<float>(y + quiet) * module;
            draw->AddRectFilled(ImVec2(left, top), ImVec2(left + static_cast<float>(x - start) * module, top + module),
                                IM_COL32(0, 0, 0, 255));
        }
    }
    ImGui::Dummy(ImVec2(side, side));
}
