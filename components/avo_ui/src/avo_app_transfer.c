/*
 * Ajustes > Enviar al reloj: opens the transfer portal (photo for Retrato,
 * firmware updates) and shows its address, PIN and progress. The portal
 * lives exactly as long as this page, and the screen stays on meanwhile.
 */
#include <stdio.h>
#include "avo_ui_internal.h"
#include "avo_settings_pages.h"

#define POLL_MS 300

static struct {
    lv_obj_t *body;
    lv_timer_t *timer;
    uint32_t version;
} tr;

static void delete_photo_cb(lv_event_t *e)
{
    (void)e;
    avo_hal_photo_delete();
    avo_hal_click();
    avo_toast(LV_SYMBOL_TRASH, "Foto de Retrato quitada");
}

static void show_face_cb(lv_event_t *e)
{
    (void)e;
    int retrato = avo_face_index_by_name("Retrato");
    if (retrato >= 0) {
        avo_settings()->face = (uint8_t)retrato;
        avo_settings_commit();
        avo_nav_face_select(retrato);
    }
    avo_nav_face();
}

static void big_value(lv_obj_t *parent, const char *caption, const char *value, const lv_font_t *font)
{
    const avo_palette_t *p = avo_pal();
    avo_label(parent, &avo_font_22, p->label2, caption);
    lv_obj_t *v = avo_label(parent, font, avo_theme_is_avocado() ? p->accent : avo_hue(AVO_HUE_MINT), value);
    lv_obj_set_width(v, lv_pct(100));
    lv_label_set_long_mode(v, LV_LABEL_LONG_MODE_WRAP);
}

static void render(const avo_portal_t *st)
{
    lv_obj_clean(tr.body);
    lv_obj_t *card = avo_glass(tr.body);
    lv_obj_set_size(card, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, 6, 0);
    char buf[64];
    switch (st->state) {
    case AVO_PORTAL_NO_WIFI:
        avo_label(card, &avo_font_26, avo_pal()->label, "Sin Wi-Fi");
        avo_note(tr.body, "Conecta el reloj en Ajustes › Wi-Fi a la misma red que tu iPhone y vuelve aquí.");
        break;
    case AVO_PORTAL_WAITING:
        big_value(card, "Abre en el iPhone", st->url, &avo_font_30);
        big_value(card, "PIN", st->pin, &avo_font_digits_76);
        avo_note(tr.body, "El iPhone debe estar en la misma red Wi-Fi. Desde esa página puedes enviar una foto "
                          "para la esfera Retrato o instalar una actualización. El portal solo existe mientras "
                          "esta pantalla está abierta, y cada vez usa un PIN nuevo.");
        break;
    case AVO_PORTAL_RECEIVING:
        snprintf(buf, sizeof buf, "%u %%", st->percent);
        big_value(card, "Recibiendo…", buf, &avo_font_digits_76);
        break;
    case AVO_PORTAL_PHOTO_OK: {
        avo_label(card, &avo_font_30, avo_pal()->good, LV_SYMBOL_OK "  Foto recibida");
        lv_obj_t *r = avo_row(tr.body, AVO_HUE_ROSE, LV_SYMBOL_IMAGE, "Ver en la esfera", NULL);
        lv_obj_add_event_cb(r, show_face_cb, LV_EVENT_CLICKED, NULL);
        break;
    }
    case AVO_PORTAL_UPDATE_OK:
        avo_label(card, &avo_font_30, avo_pal()->good, LV_SYMBOL_OK "  Actualización lista");
        avo_note(tr.body, "El reloj se reinicia con la nueva versión. Si algo fallara al arrancar, vuelve solo "
                          "a la versión anterior.");
        break;
    case AVO_PORTAL_ERROR:
        avo_label(card, &avo_font_26, avo_hue(AVO_HUE_EMBER), st->error[0] ? st->error : "Algo salió mal");
        avo_note(tr.body, "No se cambió nada en el reloj. Sal y vuelve a entrar para intentarlo de nuevo.");
        break;
    default:
        avo_label(card, &avo_font_26, avo_pal()->label2, "Abriendo…");
        break;
    }
    if (st->state != AVO_PORTAL_RECEIVING && st->state != AVO_PORTAL_UPDATE_OK) {
        avo_photo_t ph;
        if (avo_hal_photo(&ph)) {
            lv_obj_t *r = avo_row(tr.body, AVO_HUE_EMBER, LV_SYMBOL_TRASH, "Quitar foto de Retrato", NULL);
            lv_obj_add_event_cb(r, delete_photo_cb, LV_EVENT_CLICKED, NULL);
        }
    }
}

static void poll_cb(lv_timer_t *t)
{
    (void)t;
    avo_portal_t st;
    avo_hal_portal(&st);
    if (st.version != tr.version) {
        tr.version = st.version;
        render(&st);
    }
}

void avo_settings_transfer_page(lv_obj_t *scr)
{
    lv_obj_t *page = avo_subpage_create(scr, "Enviar al reloj", AVO_HUE_MINT);
    tr.body = lv_obj_create(page);
    lv_obj_remove_style_all(tr.body);
    lv_obj_set_size(tr.body, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(tr.body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(tr.body, 10, 0);
    avo_hal_portal_start();
    avo_nav_keep_awake(true);
    avo_portal_t st;
    avo_hal_portal(&st);
    tr.version = st.version;
    render(&st);
    tr.timer = lv_timer_create(poll_cb, POLL_MS, NULL);
}

void avo_settings_transfer_leave(void)
{
    if (tr.timer) {
        lv_timer_delete(tr.timer);
    }
    lv_memzero(&tr, sizeof tr);
    avo_hal_portal_stop();
    avo_nav_keep_awake(false);
}
