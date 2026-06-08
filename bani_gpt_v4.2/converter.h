#include <lvgl.h>
#include <Arduino.h>

// Forward declarations
void convert_units(lv_event_t * e);

lv_obj_t *from_dropdown;
lv_obj_t *to_dropdown;
lv_obj_t *input_field;
lv_obj_t *result_label;

float to_meters(const char *unit) {
  if (strcmp(unit, "m") == 0) return 1.0;
  if (strcmp(unit, "cm") == 0) return 0.01;
  if (strcmp(unit, "mm") == 0) return 0.001;
  if (strcmp(unit, "km") == 0) return 1000.0;
  return 1.0;
}

void convert_units(lv_event_t * e) {
  if (!from_dropdown || !to_dropdown || !input_field || !result_label) {
    Serial.println("Error: converter objects not initialized");
    return;
  }

  char from_buf[16];
  char to_buf[16];
  lv_dropdown_get_selected_str(from_dropdown, from_buf, sizeof(from_buf));
  lv_dropdown_get_selected_str(to_dropdown, to_buf, sizeof(to_buf));

  const char * input_text = lv_textarea_get_text(input_field);
  if (!input_text || strlen(input_text) == 0) {
    lv_label_set_text(result_label, "Enter a value first");
    return;
  }

  float value = atof(input_text);
  float meters = value * to_meters(from_buf);
  float converted = meters / to_meters(to_buf);

  static char buf[64];
  snprintf(buf, sizeof(buf), "%.3f %s = %.3f %s", value, from_buf, converted, to_buf);
  lv_label_set_text(result_label, buf);
}

void unit_conversion_popup() {
  lv_obj_clean(lv_scr_act());

  lv_obj_t *popup = lv_obj_create(lv_scr_act());
  lv_obj_set_size(popup, 640, 420);
  lv_obj_center(popup);

  lv_obj_t *lbl = lv_label_create(popup);
  lv_label_set_text(lbl, "Unit Converter");
  lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, 8);

  input_field = lv_textarea_create(popup);
  lv_obj_set_size(input_field, 200, 60);
  lv_obj_align(input_field, LV_ALIGN_TOP_MID, 0, 50);
  lv_textarea_set_placeholder_text(input_field, "Enter value");
  lv_textarea_set_one_line(input_field, true);

  lv_obj_t *kb = lv_keyboard_create(lv_scr_act());
  lv_keyboard_set_textarea(kb, input_field);
  lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);

  from_dropdown = lv_dropdown_create(popup);
  lv_dropdown_set_options(from_dropdown, "mm\ncm\nm\nkm");
  lv_obj_align(from_dropdown, LV_ALIGN_LEFT_MID, 80, -40);

  to_dropdown = lv_dropdown_create(popup);
  lv_dropdown_set_options(to_dropdown, "mm\ncm\nm\nkm");
  lv_obj_align(to_dropdown, LV_ALIGN_RIGHT_MID, -80, -40);

  lv_obj_t *btn = lv_btn_create(popup);
  lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -70);
  lv_obj_t *lbl_btn = lv_label_create(btn);
  lv_label_set_text(lbl_btn, "Convert");
  lv_obj_center(lbl_btn);
  lv_obj_add_event_cb(btn, convert_units, LV_EVENT_CLICKED, NULL);

  result_label = lv_label_create(popup);
  lv_label_set_text(result_label, "Result will appear here");
  lv_obj_align(result_label, LV_ALIGN_BOTTOM_MID, 0, -120);
}
