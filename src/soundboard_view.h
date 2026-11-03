#pragma once
// Soundboard-Oberflaeche (Raster aus frei belegbaren Knoepfen, Werkzeugleiste, Editor) - gemeinsam genutzt vom Reiter "Sounds" im Hauptfenster und vom
// eigenen Soundboard-Fenster. Alle Aufrufe kommen aus dem Fenster-Thread des jeweiligen Hosts; die Ansicht haelt nur Anzeigezustand (Bildlauf, Hover,
// Editor-Entwurf), die Daten liegen in der Engine.
#include <windows.h>

#include <objidl.h>

#include <cstdint>
#include <string>
#include <vector>

#include "ui_theme.h"

namespace sbv {

// Was der Host der Ansicht bei jedem Aufruf mitgibt
struct Host {
    HWND               hwnd = nullptr;
    float              s    = 1; // Skalierung
    int                tick = 0; // zaehlt mit dem Zeichentakt (Cursor-Blinken)
    bool               popout = false; // im eigenen Fenster: "Immer im Vordergrund" statt "Abkoppeln"
    const Gdiplus::Font *body = nullptr, *small = nullptr, *tiny = nullptr, *head = nullptr;
};

class View {
public:
    // Zeichnet Werkzeugleiste, Raster und (falls offen) den Editor in `area`.
    void Paint(Gdiplus::Graphics& g, const Gdiplus::RectF& area, const Host& h);

    // Mausereignisse in Client-Koordinaten des Hosts. true = von der Ansicht verbraucht.
    bool MouseDown(int x, int y, const Host& h);
    void MouseMove(int x, int y, const Host& h);
    void MouseUp(int x, int y, const Host& h);
    bool RightDown(int x, int y, const Host& h);
    bool Wheel(int x, int y, int delta, const Host& h);
    // Tastatur: Zeichen (WM_CHAR) und Tasten (WM_KEYDOWN). true = verbraucht (z.B. Esc schliesst den Editor).
    bool Char(wchar_t ch, const Host& h);
    bool Key(WPARAM vk, const Host& h);
    // Dateien auf das Fenster gezogen (Position in Client-Koordinaten). true = verbraucht.
    bool Drop(int x, int y, const std::vector<std::wstring>& files, const Host& h);

    // Der Nutzer hat "Abkoppeln" gedrueckt (nur im Hauptfenster): der Host oeffnet das Soundboard-Fenster
    bool TakePopRequest() { const bool r = popRequest_; popRequest_ = false; return r; }
    bool EditorOpen() const { return editor_.open; }
    // Tastatur gehoert der Ansicht: Editor offen oder das Suchfeld hat den Fokus (der Host leitet dann WM_CHAR/WM_KEYDOWN weiter)
    bool WantsKeys() const { return editor_.open || searchFocus_; }
    bool Dragging() const { return drag_.active || sliderDrag_ != 0; }
    void CloseEditor() { editor_.open = false; }
    // Ziehen/Regler abbrechen (Maus-Capture verloren)
    void Cancel() { drag_ = DragState(); sliderDrag_ = 0; }
    // Meldung (z.B. "Datei nicht lesbar") unten im Fenster, verschwindet nach kurzer Zeit
    const std::wstring& Toast() const { return toast_; }

private:
    struct Cell {
        Gdiplus::RectF r;
        int            slot = -1;
    };
    struct Editor {
        bool         open = false;
        int          slot = -1;
        std::wstring name, path;
        bool         nameEdited = false; // false: Name folgt dem Dateinamen
        uint32_t     color  = 0;
        int          volPct = 100;
        bool         nameSel = false;
    } editor_;
    struct DragState {
        bool active = false, moved = false;
        int  from = -1, startX = 0, startY = 0, curX = 0, curY = 0;
    } drag_;

    struct Geo {
        Gdiplus::RectF area, bar, foot, gridArea;
        int            rows = 0, cols = 0;
        float          cellW = 0, cellH = 0, gap = 0, content = 0; // Hoehe aller Zeilen
        // Werkzeugleiste
        Gdiplus::RectF searchField, exportBtn, importBtn, rowsLabel, rowsMinus, rowsVal, rowsPlus, colsLabel, colsMinus, colsVal, colsPlus, showKeys, showKeysLabel, volLabel, volume, volVal, selfHear, selfHearLabel, stop, aux;
        // Editor
        Gdiplus::RectF panel, nameField, fileField, fileBtn, swatch[12], volSlider, keyBtn, delBtn, cancelBtn, okBtn, testBtn;
    };
    Geo  MakeGeo(const Gdiplus::RectF& area, const Host& h) const;
    int  CellAt(const Geo& g, int x, int y) const; // Slot unter der Maus oder -1
    void OpenEditor(int slot, const std::wstring& path = L"");
    void SaveEditor();
    void PickFile(const Host& h);
    void ExportBoard(const Host& h);
    void ImportBoard(const Host& h);
    bool Matches(const std::wstring& name) const;
    void Assign(const std::vector<std::wstring>& files, int slot);
    void Flash(int slot);
    void SetToast(const std::wstring& t) { toast_ = t; toastUntil_ = GetTickCount64() + 3500; }
    float MaxScroll(const Geo& g) const;
    int  HitEditor(const Geo& g, int x, int y) const; // Index in der Trefferliste des Editors, -1 = ausserhalb

    float                 scroll_ = 0;
    int                   hoverSlot_ = -1, hoverBar_ = -1, hoverEd_ = -1;
    bool                  popRequest_ = false;
    std::wstring          search_;          // Suchtext (leer = alle Knoepfe normal)
    bool                  searchFocus_ = false;
    int                   sliderDrag_ = 0;      // 1 = Gesamtlautstaerke, 2 = Knopf-Lautstaerke im Editor
    std::vector<float>    flash_ = std::vector<float>(400, 0.0f); // Aufleuchten nach dem Klick
    ULONGLONG             lastTick_ = 0;
    std::wstring          toast_;
    ULONGLONG             toastUntil_ = 0;
    ULONGLONG             keyCacheAt_ = 0;
    std::vector<std::wstring> keyText_; // gebundene Tastenkuerzel der Knoepfe 1..64 (Zwischenspeicher)
    Geo                   geo_;           // Layout des letzten Zeichnens (fuer Mausereignisse)
};

// Vom Plugin: Tastenkuerzel der Soundboard-Knoepfe 1..64 in TeamSpeak festlegen bzw. anzeigen.
//   bind(slot)  oeffnet den Aufnahme-Dialog von TeamSpeak fuer "sound<slot+1>"
//   text(slot)  liefert das gebundene Kuerzel (UTF-8, leer = keins)
//   all()       oeffnet die Hotkey-Einstellungen von TeamSpeak
void SetHotkeyHandlers(void (*bind)(int slot), std::string (*text)(int slot), void (*all)());

} // namespace sbv
