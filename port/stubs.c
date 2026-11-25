/*
 * Stand-ins for the parts of the port that don't exist yet. Each says what would replace it.
 */
#include "r5900.h"
#include "runtime.h"

/* crash reports: no heap bookkeeping yet (Saint Seiya's port/hle/heap.c knows its game's allocator) */
void heap_explain(const char *what, u32 addr) { (void)what; (void)addr; }


/* 16:9 (Saint Seiya's port/hle/widescreen.c): later, from the camera code (PCSX2's widescreen patch) */
int widescreen_on(void) { return 0; }

/* texts drawn with a host font (Saint Seiya's port/hle/textfont.c): not for this game yet */
int textfont_active(void) { return 0; }
u32 *textfont_replace(const u8 *texels, u32 tw, u32 th, const u32 *pal, u32 *w_out, u32 *h_out, u32 *rect, u32 *fill) {
    (void)texels; (void)tw; (void)th; (void)pal; (void)w_out; (void)h_out; (void)rect; (void)fill;
    return 0;
}

/* Saint Seiya's training mode and fight-only rendering (its port/hle/training.c): hooks the renderer calls */
#include <SDL3/SDL.h>
u16 training_pad(u16 buttons) { return buttons; }
int training_key(int scancode) { (void)scancode; return 0; }
int training_hud(SDL_Surface *surf) { (void)surf; return 0; }
int fight_on(void) { return 0; }
void widescreen_toggle(void) {}
