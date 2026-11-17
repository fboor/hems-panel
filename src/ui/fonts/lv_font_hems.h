/**
 * Montserrat Medium at the sizes the panel uses, extended with the German
 * umlauts (Ä Ö Ü ä ö ü ß), the euro sign and the LVGL symbols in use.
 *
 * The generated .c files in this directory carry the exact lv_font_conv
 * options and licensing notes. Keep the declarations here in sync with
 * include/lv_conf.h, which points LV_FONT_DEFAULT at lv_font_hems_16.
 */
#ifndef LV_FONT_HEMS_H
#define LV_FONT_HEMS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <lvgl.h>

LV_FONT_DECLARE(lv_font_hems_11);
LV_FONT_DECLARE(lv_font_hems_14);
LV_FONT_DECLARE(lv_font_hems_16);
LV_FONT_DECLARE(lv_font_hems_20);
LV_FONT_DECLARE(lv_font_hems_36);
LV_FONT_DECLARE(lv_font_hems_48);

#ifdef __cplusplus
}
#endif

#endif /* LV_FONT_HEMS_H */
