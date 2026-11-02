#include "voicechanger_page.h"

#include "i18n.h"
#include "ui_widgets.h"

using namespace ui;

namespace vcpage {

void Page::Paint(Graphics& g, const RectF& area, float s, const Fonts& f)
{
    // Platzhalter: hier entsteht der Voicechanger. Alles unterhalb dieses Blocks kann frei ersetzt werden.
    const float cx = area.X + area.Width / 2, cy = area.Y + area.Height / 2;
    FillCircle(g, cx, cy - 46 * s, 40 * s, C(col::accent, 36));
    DrawIcon(g, Icon::Voice, RectF(cx - 22 * s, cy - 68 * s, 44 * s, 44 * s), C(col::accent), s);
    Text(g, Tr(L"Voicechanger"), *f.title, RectF(area.X, cy + 8 * s, area.Width, 30 * s), C(col::text), StringAlignmentCenter);
    Text(g, Tr(L"Hier entstehen Stimmeffekte für dein Mikrofon – bald verfügbar."), *f.body, RectF(area.X + 40 * s, cy + 42 * s, area.Width - 80 * s, 24 * s), C(col::sub),
         StringAlignmentCenter);
}

} // namespace vcpage
