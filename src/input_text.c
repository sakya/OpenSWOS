/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 * SWOS DOS Reversed by Zlatko Karakaš (https://github.com/zlatkok)
 */
/*
 * input_text.c — PrintFormatted + InputText family
 * Ported from SWOS2.s (Chunk 36)
 */

#include "input_text.h"
#include "menu_system.h"
#include "string_utils.h"
#ifdef SWOS_MIYOO_BUILD
#include "blitter.h"
#include "display.h"
#include "game_utils.h"
#include "text_render.h"
#include <SDL.h>
#include <string.h>
#endif
#ifdef SWOS_WASM_BUILD
#include <emscripten.h>
#endif

/* The web page (wasm/shell.html) is told a text field is open, and puts up a phone's on-screen
 * keyboard (its hidden field; what is typed comes back through InputSDL_WebChar); a phone build
 * brings up the system's (what is typed comes back as SDL_TEXTINPUT, input_sdl.c). */
static void web_text_field(int on)
{
#ifdef SWOS_WASM_BUILD
    EM_ASM({ if (Module.swosTextInput) Module.swosTextInput($0); }, on);
#elif defined(SWOS_MOBILE_BUILD)
    extern void Mobile_TextInput(int on);     /* (a phone: its system keyboard, src/mobile.c) */
    Mobile_TextInput(on);
#else
    (void)on;
#endif
}

#ifdef SWOS_SDL_BUILD
/* RAW text (InputText_SetRaw): a password's characters as typed, small letters and symbols
 * too, kept in s_raw while the field draws each as '*' -- or, shown, as the menu font can. */
extern void InputSDL_RawText(int on);
extern int  InputSDL_RawChar(void);
static int     s_rawMode, s_rawShown;
static uint8_t s_raw[100];

void InputText_SetRaw(int32_t on, int32_t shown)
{
    s_rawMode  = on;
    s_rawShown = shown;
}

static uint8_t raw_face(uint8_t c)
{
    if (!s_rawShown)
        return '*';
    if (c >= 'a' && c <= 'z')
        return (uint8_t)(c - 'a' + 'A');
    return (c >= ' ' && c <= 'Z') ? c : '?';
}
#endif

/* ---- External globals ---- */
extern volatile uint16_t _panicReasonCode;
extern uint8_t _inMenuKeyboardBuffer[100];
extern uint16_t _currentKey;
extern volatile uint16_t _shortFire;
extern uint16_t _deadVar02;
extern uint16_t _deadInputTextVar;
extern uint16_t _allowExtraCharsFlag;
extern uint8_t _inputAllowedControlKeys[];

/* ---- External functions ---- */
extern void PanicHaltLoop(void);
extern void MenuCheckControls(void);
extern void CallDrawMenuItem(MenuEntry *entry);
extern void DrawMenuItemBackgroundMasked(uint16_t x, uint16_t y, uint16_t w, uint16_t h);

/* Redraw the entry being edited. An entry with a background clears its own box
 * (DrawMenuItem fills it first), and every text input in SWOS itself has one. The
 * SWOS 2020 menus have text inputs WITHOUT one -- the log-in screen's NAME and
 * SERVER boxes -- because that game redraws the whole menu every frame; drawn over
 * the old text, a shortened string left the longer one showing through it. So put
 * the menu picture back under the box first, the way a transparent entry is
 * cleared. */
static void inputTextRedraw(MenuEntry *entry)
{
    if (entry->background == NO_BACKGROUND)
        DrawMenuItemBackgroundMasked(entry->x, entry->y, entry->dwWidth, entry->dwHeight);
    CallDrawMenuItem(entry);
}

/* ============================================================
 * PrintFormatted
 * Format string printer: %0..%6 = decimal numbers, %a/%b/%c = strings.
 * Uses goto-based fall-through to match 68k digit emission behavior
 * (once we enter at a digit level, all lower digits are always emitted).
 * ============================================================ */
__attribute__((used, externally_visible))
void PrintFormatted(const uint8_t *fmt, uint8_t *out,
                     uint16_t n0, uint16_t n1, uint16_t n2, uint16_t n3,
                     uint16_t n4, uint16_t n5, uint16_t n6,
                     const uint8_t *strA, const uint8_t *strB, const uint8_t *strC)
{
    for (;;) {
        /* Copy bytes until '%' or NUL */
        while (*fmt != '%') {
            uint8_t c = *fmt++;
            *out++ = c;
            if (c == 0) {
                return;
            }
        }

        /* Skip '%' and read the specifier character */
        fmt++;
        uint8_t spec = *fmt++;

        /* String substitution: %a, %b, %c */
        if (spec == 'a' || spec == 'b' || spec == 'c') {
            const uint8_t *src;
            if (spec == 'a')
                src = strA;
            else if (spec == 'b')
                src = strB;
            else
                src = strC;

            while (*src != 0) {
                *out++ = *src++;
            }
            continue;
        }

        /* Numeric substitution: %0..%6 */
        uint16_t val;
        if (spec == '0')      val = n0;
        else if (spec == '1') val = n1;
        else if (spec == '2') val = n2;
        else if (spec == '3') val = n3;
        else if (spec == '4') val = n4;
        else if (spec == '5') val = n5;
        else if (spec == '6') val = n6;
        else {
            _panicReasonCode = 0x5916u;
            PanicHaltLoop();
            return;
        }

        /* Convert number to decimal digits.
         * Asm uses fall-through: once we enter at a digit level,
         * all lower digits are always emitted (including zeros).
         * e.g. val=50 → "50", val=1050 → "1050", val=100 → "100" */
        if (val < 10)    goto emit_units;
        if (val < 100)   goto emit_tens;
        if (val < 1000)  goto emit_hundreds;
        if (val < 10000) goto emit_thousands;

        /* 10000+ : andi.l #$FFFF,d7; divu.w #10000 */
        *out++ = (uint8_t)(val / 10000) + '0';
        val = val % 10000;

    emit_thousands:
        *out++ = (uint8_t)(val / 1000) + '0';
        val = val % 1000;

    emit_hundreds:
        *out++ = (uint8_t)(val / 100) + '0';
        val = val % 100;

    emit_tens:
        *out++ = (uint8_t)(val / 10) + '0';
        val = val % 10;

    emit_units:
        *out++ = (uint8_t)val + '0';
    }
}

/* ============================================================
 * InputTextFilterKey
 * Filters currentKey to allowed charset.
 * Returns: key value if allowed, 0 if not.
 * ============================================================ */
/* The characters a name may have beyond A-Z, 0-9 and the space (the SWOS 96/97 Mod 4.1, Marco's
 * idea; docs/WORLD_2026_27.md §6l): - . / ' ? + ( ) : , and _ (drawn as a space: ROBERTO_CARLOS is
 * one word, the surname shown whole). A file name (allowExtraCharsFlag) takes - ' ( ) _ but never
 * . or / (the extension, the folder). The font has every one of them (the charset's own glyphs). */
static int32_t extra_char(uint8_t c)
{
    static const char kName[] = "-./'?+():,_", kFile[] = "-'()_";
    const char *set = _allowExtraCharsFlag ? kFile : kName;
    for (; *set; set++)
        if ((uint8_t)*set == c)
            return 1;
    return 0;
}

/* A character typed (the keyboard's own layout, SHIFT included): its capital, or 0 if a
 * name cannot have it. */
static uint8_t typed_char(int32_t ch)
{
    if (ch >= 'a' && ch <= 'z')
        ch -= 'a' - 'A';
    if ((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == ' ')
        return (uint8_t)ch;
    return ch > ' ' && ch < 0x7F && extra_char((uint8_t)ch) ? (uint8_t)ch : 0;
}

#ifdef SWOS_MIYOO_BUILD
enum { OSK_ROWS = 6, OSK_COLUMNS = 10, OSK_CELL_W = 29, OSK_CELL_H = 24 };

static uint8_t s_osk_frame[PITCH_VIEW_MAX_WIDTH * SCREEN_HEIGHT];
static char s_osk_keys[OSK_ROWS][OSK_COLUMNS];
static int s_osk_count[OSK_ROWS], s_osk_row, s_osk_column, s_osk_preferred_column, s_osk_open;
extern volatile uintptr_t _frontbufferBasePtr;
extern uint8_t _bigCharsTable[];
extern uint8_t _smallCharsTable[];
extern uint16_t _quitToDos;
extern int16_t _menuControlsDirection;
extern void InputSDL_ClearKeyState(void);
extern void InputSDL_WebChar(int32_t ch);
extern void WaitForVSync(void);

static void osk_open(void)
{
    static const char *const rows[OSK_ROWS] = {
        "ABCDEFGHIJ", "KLMNOPQRST", "UVWXYZ-.'?", "0123456789", "/+():,_", " "
    };

    for (int row = 0; row < OSK_ROWS; row++) {
        s_osk_count[row] = 0;
        for (const char *p = rows[row]; *p; p++)
            if (typed_char(*p))
                s_osk_keys[row][s_osk_count[row]++] = *p;
    }
    s_osk_row = s_osk_column = s_osk_preferred_column = 0;
    InputSDL_ClearKeyState();
    s_osk_open = 1;
}

static void osk_close(void)
{
    if (s_osk_open) {
        s_osk_open = 0;
        InputSDL_ClearKeyState();
    }
}

int InputText_MiyooKey(int32_t key, int pressed)
{
    int ch;
    if (!s_osk_open)
        return 0;

    switch (key) {
    case SDLK_SPACE: ch = s_osk_keys[s_osk_row][s_osk_column]; break; /* A */
    case SDLK_LCTRL: ch = 8; break;     /* B */
    case SDLK_RETURN: ch = 13; break;   /* Start */
    /* Either rear trigger cancels, including when the firmware swaps L2 and R2. */
    case SDLK_TAB:
    case SDLK_BACKSPACE: ch = 27; break;
    case SDLK_RCTRL: return 1;          /* Select still participates in the quit chord. */
    default: return 0;
    }
    if (pressed)
        InputSDL_WebChar(ch);
    return 1;
}

const uint8_t *InputText_MiyooFrame(void)
{
    return s_osk_open ? s_osk_frame : NULL;
}

static void osk_move(int direction)
{
    if (direction < 0)
        return;
    int columns = s_osk_count[s_osk_row];
    switch (direction & 6) {
    case 0: s_osk_row = (s_osk_row + OSK_ROWS - 1) % OSK_ROWS; break;
    case 4: s_osk_row = (s_osk_row + 1) % OSK_ROWS; break;
    case 6:
        if (columns > 1)
            s_osk_preferred_column = (s_osk_column + columns - 1) % columns;
        break;
    case 2:
        if (columns > 1)
            s_osk_preferred_column = (s_osk_column + 1) % columns;
        break;
    }
    columns = s_osk_count[s_osk_row];
    s_osk_column = s_osk_preferred_column < columns ? s_osk_preferred_column : columns - 1;
}

static void osk_button(int x, int y, int w, int h, int color, int selected)
{
    FillRectangle(x, y, w, h, selected ? 2 : 1);
    FillRectangle(x + 1, y + 1, w - 2, h - 2, color);
    FillRectangle(x + 1, y + h - 1, w - 1, 1, 0);
    FillRectangle(x + w - 1, y + 1, 1, h - 1, 0);
}

static void osk_draw(const uint8_t *pos)
{
    static const char *const hints[] = { "A TYPE", "B DELETE", "START OK", "L2 CANCEL" };
    static const uint8_t hint_colors[] = { 9, 9, 14, 12 };
    uintptr_t saved_fb = _frontbufferBasePtr;
    int saved_offset = _menu_draw_x_offset;
    uint8_t preview[100];
    int len = 0;

    /* Use the game's glyph blitter on a separate frame so the caller's menu stays intact. */
    _frontbufferBasePtr = (uintptr_t)s_osk_frame;
    _menu_draw_x_offset = (_screen_width - SCREEN_WIDTH) / 2;
    memset(s_osk_frame, 45, (size_t)_screen_width * _screen_height);
    osk_button(18, 14, 300, 26, 7, 0);
    DrawMenuTextCentered(168, 23, 2, (uint8_t *)"ENTER TEXT", _bigCharsTable);
    osk_button(18, 49, 300, 26, 0, 0);

    for (const uint8_t *p = _inMenuKeyboardBuffer; p < pos; p++) {
        uint8_t c = typed_char(*p);
        preview[len++] = c ? c : '?';
    }
    preview[len] = 0;
    uint8_t *visible = preview;
    while (GetTextWidth(visible, _bigCharsTable) > 280)
        visible++;
    int end = DrawMenuText(25, 58, 2, visible, _bigCharsTable);
    FillRectangle((uint16_t)end, 57, 2, 11, 2);

    for (int row = 0; row < OSK_ROWS; row++) {
        int cell_w = row == OSK_ROWS - 1 ? 116 : OSK_CELL_W;
        int x = (SCREEN_WIDTH - s_osk_count[row] * cell_w) / 2;
        int y = 84 + row * OSK_CELL_H;
        for (int col = 0; col < s_osk_count[row]; col++, x += cell_w) {
            uint8_t label[2] = { (uint8_t)s_osk_keys[row][col], 0 };
            uint8_t *text = label[0] == ' ' ? (uint8_t *)"SPACE" : label;
            int cx = x + (cell_w - 2) / 2;
            int selected = row == s_osk_row && col == s_osk_column;
            osk_button(x, y, cell_w - 2, OSK_CELL_H - 2, selected ? 10 : 9, selected);
            if (label[0] == '_')
                FillRectangle(cx - 3, y + 15, 7, 1, 2);
            else
                DrawMenuTextCentered(cx, y + 7, 2, text, _bigCharsTable);
        }
    }
    for (int i = 0; i < 4; i++) {
        int x = 18 + i * 75;
        osk_button(x, 239, 73, 20, hint_colors[i], 0);
        DrawMenuTextCentered(x + 36, 246, 2, (uint8_t *)hints[i], _smallCharsTable);
    }
    _menu_draw_x_offset = saved_offset;
    _frontbufferBasePtr = saved_fb;
}
#endif

#ifndef SWOS_SDL_BUILD
/* The Amiga: SHIFT held types the symbol on the key (a US / UK keyboard's), as the Mod's DOS
 * game types its keyboard's (the Amiga's LUT gives only the unshifted character). */
extern volatile uint8_t _gAmigaShiftHeld;    /* (the keyboard interrupt's: interrupts.c) */
static uint16_t shifted(uint16_t key)
{
    static const char kFrom[] = "1234567890-=;',./", kTo[] = "!@#$%^&*()_+:\"<>?";
    for (int32_t i = 0; kFrom[i]; i++)
        if ((uint8_t)kFrom[i] == key)
            return (uint8_t)kTo[i];
    return key;
}
#endif

__attribute__((used, externally_visible))
uint16_t InputTextFilterKey(void)
{
    uint16_t key = _currentKey;
#ifndef SWOS_SDL_BUILD
    if (_gAmigaShiftHeld && key > ' ' && key < 0x7F)
        key = shifted(key);
#endif
    if (key > ' ' && key < 0x7F && !(key >= 'A' && key <= 'Z') && !(key >= '0' && key <= '9'))
        return extra_char((uint8_t)key) ? key : 0;

    /* A-Z allowed */
    if (key >= 'A' && key <= 'Z')
        return key;

    /* 0-9 allowed */
    if (key >= '0' && key <= '9')
        return key;

    /* When allowExtraCharsFlag is set, '.' and '/' are rejected
     * (intercepted before the table scan where they'd normally match) */
    if (_allowExtraCharsFlag != 0) {
        if ((uint8_t)key == '.' || (uint8_t)key == '/')
            return 0;
    }

    /* Scan allowed control keys table (terminated by 0xFF) */
    {
        uint8_t *p = _inputAllowedControlKeys;
        while (*p != 0xFF) {
            if (*p == (uint8_t)key)
                return key;
            p++;
        }
    }

    return 0;
}

/* ============================================================
 * InsertCharacter
 * Inserts ch at pos within inMenuKeyboardBuffer, shifting bytes right.
 * Note: GetStringLength stub returns a0 = buffer + len (at NUL),
 * so the shift starts from the NUL and moves everything right.
 * ============================================================ */
__attribute__((used, externally_visible, optimize("no-tree-loop-distribute-patterns")))
void InsertCharacter(uint8_t *pos, uint8_t ch)
{
    uint16_t len = GetStringLength(_inMenuKeyboardBuffer);
    uint8_t *p = _inMenuKeyboardBuffer + len; /* points to NUL (matching asm stub: adda.w d0,a0) */

    /* Shift bytes right from p down to pos (do-while matches asm: shift first, then check) */
    do {
        *(p + 1) = *p;
        p--;
    } while (p >= pos);

    /* Write ch at pos (p is now one below pos, so p+1 == pos) */
    *(p + 1) = ch;
}

/* ============================================================
 * InputTextDeleteCharAtCursor
 * Deletes char at pos, shifting remaining chars left.
 * ============================================================ */
__attribute__((used, externally_visible, optimize("no-tree-loop-distribute-patterns")))
void InputTextDeleteCharAtCursor(uint8_t *pos)
{
    if (*pos == 0)
        return;

    uint8_t *p = pos;
    do {
        *p = *(p + 1);
        p++;
    } while (*(p - 1) != 0);
}

/* A text field is being typed into: a redefined key is a letter here, not a control
 * (input_sdl.c). */
volatile uint16_t _gTextInputActive;

__attribute__((optimize("no-tree-loop-distribute-patterns")))
static uint16_t inputTextBody(uint16_t maxLenPlus1, uint8_t *entryRaw);

/* ============================================================
 * InputText
 * Interactive text editor using the menu system.
 * maxLenPlus1: max input length + 1 (for NUL), must be <= 99
 * entryRaw: pointer to MenuEntry (passed as uint8_t* from asm)
 * Returns: 0 = committed (enter/fire), 1 = cancelled (escape)
 * ============================================================ */
__attribute__((used, externally_visible))
uint16_t InputText(uint16_t maxLenPlus1, uint8_t *entryRaw)
{
    uint16_t r;
    _gTextInputActive = 1;
    web_text_field(1);
    r = inputTextBody(maxLenPlus1, entryRaw);
    web_text_field(0);
    _gTextInputActive = 0;
    return r;
}

static uint16_t inputTextBody(uint16_t maxLenPlus1, uint8_t *entryRaw)
{
    MenuEntry *entry = (MenuEntry *)entryRaw;

    _deadVar02 = 1;

    if (maxLenPlus1 > 99) {
        _panicReasonCode = 0x1197u;
        PanicHaltLoop();
        return 0;
    }

    uint16_t maxLen = maxLenPlus1 - 1;

    /* Save original string pointer, copy to edit buffer */
    uint8_t *origStr = (uint8_t *)entry->fg;
    StringCopy(origStr, _inMenuKeyboardBuffer);
    entry->fg = (uintptr_t)_inMenuKeyboardBuffer;

    /* Cursor starts at end of string (at the NUL terminator).
     * Asm: GetStringLength stub sets a0 = buffer + len (at NUL). */
    uint16_t initLen = GetStringLength(_inMenuKeyboardBuffer);
    uint8_t *pos = _inMenuKeyboardBuffer + initLen;
#ifdef SWOS_SDL_BUILD
    if (s_rawMode) {
        uint16_t i;
        StringCopy(origStr, s_raw);
        for (i = 0; i < initLen; i++)
            _inMenuKeyboardBuffer[i] = raw_face(s_raw[i]);
        InputSDL_RawText(1);
    }
#endif

    _deadInputTextVar = 0;
#ifdef SWOS_SDL_BUILD
    if (!s_rawMode)
        InputSDL_RawText(1);            /* (the characters as typed, the keyboard's layout and SHIFT) */
#endif
#ifdef SWOS_MIYOO_BUILD
    if (!s_rawMode)
        osk_open();
#endif

    for (;;) {
#ifdef SWOS_MIYOO_BUILD
        if (_quitToDos)
            goto cancel;
#endif
        MenuCheckControls();

#ifdef SWOS_MIYOO_BUILD
        if (s_osk_open)
            osk_move(_menuControlsDirection);
        else
#endif
        if (_shortFire != 0)
            goto commit;

#ifdef SWOS_SDL_BUILD
        if (!s_rawMode) {               /* the characters typed (SDL_TEXTINPUT), A..Z, 0..9 and the extras */
            int ch;
            while ((ch = InputSDL_RawChar()) >= 0) {
                const uint8_t c = typed_char(ch);
                if (c && GetStringLength(_inMenuKeyboardBuffer) < maxLen) {
                    InsertCharacter(pos, c);
                    pos++;
                }
            }
            /* LEFT / RIGHT move the cursor (SHIFT types now): the arrows reach the game as its
             * controls, not as keys */
#ifdef SWOS_MIYOO_BUILD
            if (!s_osk_open)
#endif
            {
                extern int16_t _menuControlsDirection;
                if (_menuControlsDirection == 6 && pos > _inMenuKeyboardBuffer)
                    pos--;
                else if (_menuControlsDirection == 2 && *pos != 0)
                    pos++;
            }
        }
#endif

#ifdef SWOS_SDL_BUILD
        if (s_rawMode) {                /* the characters typed, beside what is drawn for them */
            int ch;
            while ((ch = InputSDL_RawChar()) >= 0) {
                uint16_t len = GetStringLength(_inMenuKeyboardBuffer);
                uint16_t at = (uint16_t)(pos - _inMenuKeyboardBuffer);
                if (len == maxLen)
                    break;
                for (uint16_t i = (uint16_t)(len + 1); i > at; i--)
                    s_raw[i] = s_raw[i - 1];
                s_raw[at] = (uint8_t)ch;
                InsertCharacter(pos, raw_face((uint8_t)ch));
                pos++;
            }
        }
#endif
        uint16_t key = InputTextFilterKey();
#ifdef SWOS_SDL_BUILD
        if (key >= 0x20 && key < 0x7E)
            key = 0;                    /* (the game's own keys give the capitals: typed above) */
#endif

        if (key == 0) {
            goto insert_cursor;
        }

        if (key == 13) { /* Enter — commit */
            goto commit;
        }

        if (key == 19) { /* LEFT (the Amiga's arrow key) — move cursor left */
            if (pos > _inMenuKeyboardBuffer) {
                pos--;
            }
            continue;
        }

        if (key == 18) { /* RIGHT — move cursor right */
            if (*pos != 0) {
                pos++;
            }
            continue;
        }

        if (key == 27) /* Escape — cancel */
            goto cancel;

        if (key == 127) { /* Backspace — delete char to the left */
            if (pos > _inMenuKeyboardBuffer) {
                pos--;
#ifdef SWOS_SDL_BUILD
                if (s_rawMode)
                    StringCopy(s_raw + (pos - _inMenuKeyboardBuffer) + 1, s_raw + (pos - _inMenuKeyboardBuffer));
#endif
                InputTextDeleteCharAtCursor(pos);
            }
            goto insert_cursor;
        }

        if (key == 126) { /* 0x7E — delete at cursor (like Delete key) */
#ifdef SWOS_SDL_BUILD
            if (s_rawMode && *pos)
                StringCopy(s_raw + (pos - _inMenuKeyboardBuffer) + 1, s_raw + (pos - _inMenuKeyboardBuffer));
#endif
            InputTextDeleteCharAtCursor(pos);
            goto insert_cursor;
        }

        /* Printable character (0x20..0x7F) — insert if not full */
        if (key >= 0x20 && key <= 0x7F) {
            uint16_t curLen = GetStringLength(_inMenuKeyboardBuffer);
            if (curLen != maxLen) {
                InsertCharacter(pos, (uint8_t)key);
                pos++;
            }
        }

    insert_cursor:
#ifdef SWOS_MIYOO_BUILD
        if (s_osk_open) {
            osk_draw(pos);
            WaitForVSync();
            ReadTimerDelta();
            continue;
        }
#endif
        /* Show cursor: insert 0xFF at pos, draw, then remove it */
        InsertCharacter(pos, 0xFF);
        inputTextRedraw(entry);
        InputTextDeleteCharAtCursor(pos);
    }

cancel:
#ifdef SWOS_MIYOO_BUILD
    osk_close();
#endif
#ifdef SWOS_SDL_BUILD
    InputSDL_RawText(0);
#endif
    entry->fg = (uintptr_t)origStr;
    inputTextRedraw(entry);
    _deadVar02 = 0;
    return 1;

commit:
#ifdef SWOS_MIYOO_BUILD
    osk_close();
#endif
#ifdef SWOS_SDL_BUILD
    InputSDL_RawText(0);
    if (s_rawMode) {
        InputSDL_RawText(0);
        StringCopy(s_raw, origStr);     /* the characters, not their faces */
        entry->fg = (uintptr_t)origStr;
        inputTextRedraw(entry);
        _deadVar02 = 0;
        return 0;
    }
#endif
    StringCopy(_inMenuKeyboardBuffer, origStr);
    entry->fg = (uintptr_t)origStr;
    inputTextRedraw(entry);
    _deadVar02 = 0;
    return 0;
}
