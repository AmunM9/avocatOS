/* Ajustes > Sonido, Ajustes > Música and Ajustes > Gestos (with a live test
 * of the gestures). */
#include <stdio.h>
#include "avo_ui_internal.h"
#include "avo_settings_pages.h"

#define PROBE_SHOW_MS 2500

static void commit_bool(lv_event_t *e, bool *field)
{
    *field = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    avo_settings_commit();
    avo_hal_click();
}

/* ================================================================= Sonido */

static lv_obj_t *s_vol_lbl;

static void sounds_sw_cb(lv_event_t *e) { commit_bool(e, &avo_settings()->sounds); }

static void volume_cb(lv_event_t *e)
{
    lv_obj_t *s = lv_event_get_target(e);
    avo_settings()->volume = (uint8_t)lv_slider_get_value(s);
    lv_label_set_text_fmt(s_vol_lbl, "%u %%", avo_settings()->volume);
    if (lv_event_get_code(e) == LV_EVENT_RELEASED) {
        avo_settings_commit(); /* once per drag, not per pixel: spares the flash */
        avo_hal_sound_play(AVO_SOUND_NOTIFY);
    }
}

static void test_cb(lv_event_t *e)
{
    (void)e;
    avo_hal_sound_play(AVO_SOUND_NOTIFY);
}

void avo_settings_sound_page(lv_obj_t *scr)
{
    lv_obj_t *page = avo_subpage_create(scr, "Sonido", AVO_HUE_EMBER);
    avo_row_switch(page, AVO_HUE_EMBER, LV_SYMBOL_BELL, "Sonidos", avo_settings()->sounds, sounds_sw_cb, NULL);
    avo_note(page, "Clics, avisos y cargador. Las alarmas, el temporizador y las llamadas "
                   "suenan aunque esté desactivado, porque el reloj no vibra.");

    avo_section(page, "Volumen");
    lv_obj_t *card = avo_glass(page);
    lv_obj_set_size(card, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(card, 16, 0);
    avo_label(card, &avo_font_26, avo_pal()->label2, LV_SYMBOL_VOLUME_MID);
    lv_obj_t *s = avo_slider(card, 0, 100, avo_settings()->volume);
    lv_obj_set_flex_grow(s, 1);
    lv_obj_set_width(s, 1);
    lv_obj_add_event_cb(s, volume_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s, volume_cb, LV_EVENT_RELEASED, NULL);
    s_vol_lbl = avo_label(card, &avo_font_22, avo_pal()->label, "");
    lv_obj_set_width(s_vol_lbl, 62);
    lv_label_set_text_fmt(s_vol_lbl, "%u %%", avo_settings()->volume);

    lv_obj_t *r = avo_row(page, AVO_HUE_EMBER, LV_SYMBOL_PLAY, "Probar", NULL);
    lv_obj_add_event_cb(r, test_cb, LV_EVENT_CLICKED, NULL);
}

/* ================================================================= Música */

static void artwork_sw_cb(lv_event_t *e) { commit_bool(e, &avo_settings()->artwork); }

void avo_settings_music_page(lv_obj_t *scr)
{
    lv_obj_t *page = avo_subpage_create(scr, "Música", AVO_HUE_ROSE);
    avo_row_switch(page, AVO_HUE_ROSE, LV_SYMBOL_IMAGE, "Portadas", avo_settings()->artwork, artwork_sw_cb, NULL);
    avo_note(page, "Muestra la portada del álbum en Reproduciendo. Se busca por Wi-Fi en Apple (iTunes) "
                   "con el título y el artista. Sin Wi-Fi, o si está desactivado, se muestran solo la "
                   "canción y los controles.");
}

/* ================================================================= Gestos */

static struct {
    lv_obj_t *probe;
    lv_timer_t *clear;
} g_ui;

static void tap_sw_cb(lv_event_t *e) { commit_bool(e, &avo_settings()->double_tap); }
static void flick_sw_cb(lv_event_t *e) { commit_bool(e, &avo_settings()->wrist_flick); }

static const char *PROBE_IDLE = "Prueba aquí: haz un gesto";

static void clear_cb(lv_timer_t *t)
{
    (void)t;
    g_ui.clear = NULL;
    if (g_ui.probe) {
        lv_label_set_text(g_ui.probe, PROBE_IDLE);
        lv_obj_set_style_text_color(g_ui.probe, avo_pal()->label2, 0);
    }
}

static void probe_cb(bool flick)
{
    if (!g_ui.probe) {
        return;
    }
    lv_label_set_text(g_ui.probe, flick ? LV_SYMBOL_OK "  Giro de muñeca" : LV_SYMBOL_OK "  Doble toque");
    lv_obj_set_style_text_color(g_ui.probe, avo_pal()->good, 0);
    avo_hal_click();
    if (g_ui.clear) {
        lv_timer_reset(g_ui.clear);
    } else {
        g_ui.clear = lv_timer_create(clear_cb, PROBE_SHOW_MS, NULL);
        lv_timer_set_repeat_count(g_ui.clear, 1);
    }
}

void avo_settings_gestures_page(lv_obj_t *scr)
{
    lv_obj_t *page = avo_subpage_create(scr, "Gestos", AVO_HUE_MINT);
    avo_row_switch(page, AVO_HUE_MINT, AVO_SYM_TAP, "Doble toque", avo_settings()->double_tap, tap_sw_cb, NULL);
    avo_note(page, "Toca dos veces el reloj con un dedo, en el borde o en una zona sin botones: "
                   "contesta una llamada, pospone la alarma, detiene el temporizador o pausa la música.");
    avo_row_switch(page, AVO_HUE_MINT, AVO_SYM_FLICK, "Giro de muñeca", avo_settings()->wrist_flick, flick_sw_cb, NULL);
    avo_note(page, "Gira la muñeca hacia fuera y vuelve rápido: descarta avisos, silencia una llamada "
                   "y vuelve a la esfera.");
    lv_obj_t *card = avo_glass(page);
    lv_obj_set_size(card, lv_pct(100), 90);
    g_ui.probe = avo_label(card, &avo_font_26, avo_pal()->label2, PROBE_IDLE);
    lv_obj_center(g_ui.probe);
    avo_motion_set_probe(probe_cb);
}

void avo_settings_gestures_leave(void)
{
    avo_motion_set_probe(NULL);
    if (g_ui.clear) {
        lv_timer_delete(g_ui.clear);
    }
    lv_memzero(&g_ui, sizeof g_ui);
}
