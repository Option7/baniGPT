#include <ArduinoJson.h>
#include <FS.h>
#include <SPIFFS.h>
#include <WiFi.h>
#include <lvgl.h>

#define CONFIG_PATH "/config.json"

M5GFX display;

// ==== GLOBAL VARIABLES ====
struct AppConfig {
  int brightness;
  bool ai_mode;
  String ssid;
  String password;
} appConfig;

static lv_obj_t *lbl_brightness;
static lv_obj_t *slider_brightness;
static lv_obj_t *switch_ai;
static lv_obj_t *ta_ssid;
static lv_obj_t *ta_pass;

static lv_obj_t *wifi_dropdown;
static lv_obj_t *wifi_pass_field;
static lv_obj_t *wifi_status_label;
static lv_obj_t *wifi_keyboard;

// ==== SAVE CONFIG ====
void save_config() {
  DynamicJsonDocument doc(256);
  doc["brightness"] = appConfig.brightness;
  doc["ai_mode"] = appConfig.ai_mode;
  doc["ssid"] = appConfig.ssid;
  doc["password"] = appConfig.password;

  File f = SPIFFS.open(CONFIG_PATH, FILE_WRITE);
  if (!f) {
    Serial.println("Failed to open config file for writing");
    return;
  }
  serializeJson(doc, f);
  f.close();
  Serial.println("Config saved!");
}

// ==== LOAD CONFIG ====
void load_config() {
  if (!SPIFFS.exists(CONFIG_PATH)) {
    Serial.println("No config found, using defaults");
    appConfig = {70, false, "", ""};
    save_config();
    return;
  }
  File f = SPIFFS.open(CONFIG_PATH, FILE_READ);
  if (!f) {
    Serial.println("Failed to open config file for reading");
    return;
  }
  DynamicJsonDocument doc(256);
  DeserializationError err = deserializeJson(doc, f);
  if (err) {
    Serial.println("Failed to parse config, using defaults");
    appConfig = {70, false, "", ""};
    return;
  }
  appConfig.brightness = doc["brightness"] | 70;
  appConfig.ai_mode = doc["ai_mode"] | false;
  appConfig.ssid = String((const char*)doc["ssid"]);
  appConfig.password = String((const char*)doc["password"]);
  Serial.println("Config loaded!");
  f.close();
}

// ==== APPLY SETTINGS ====
void apply_config() {
  //analogWrite(5, map(appConfig.brightness, 0, 100, 0, 255)); // Example pin for backlight PWM
  if (appConfig.ai_mode)
    Serial.println("AI Mode: ON");
  else
    Serial.println("AI Mode: OFF");
}

void populate_wifi_dropdown() {
  lv_dropdown_clear_options(wifi_dropdown);
  lv_dropdown_add_option(wifi_dropdown, "Scanning...", LV_DROPDOWN_POS_LAST);

  int n = WiFi.scanNetworks();
  if (n == 0) {
    lv_dropdown_clear_options(wifi_dropdown);
    lv_dropdown_add_option(wifi_dropdown, "No networks found", LV_DROPDOWN_POS_LAST);
  } else {
    lv_dropdown_clear_options(wifi_dropdown);
    for (int i = 0; i < n; i++) {
      lv_dropdown_add_option(wifi_dropdown, WiFi.SSID(i).c_str(), LV_DROPDOWN_POS_LAST);
    }
  }
}


void connectWiFi() {
  if (appConfig.ssid == "") return;

  WiFi.begin(appConfig.ssid.c_str(), appConfig.password.c_str());
  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 15) {
    delay(500);
    retries++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    //lv_label_set_text(wifi_status_label, "Wi-Fi: Connected ✅");
  } else {
    //lv_label_set_text(wifi_status_label, "Wi-Fi: Failed ❌");
  }
}


// === Connect to Wi-Fi ===
void connect_to_wifi(lv_event_t *e) {
  char ssid_buf[64];
  lv_dropdown_get_selected_str(wifi_dropdown, ssid_buf, sizeof(ssid_buf));
  String ssid = String(ssid_buf);
  String pass = lv_textarea_get_text(wifi_pass_field);

  lv_label_set_text(wifi_status_label, "Connecting...");
  WiFi.begin(ssid.c_str(), pass.c_str());

  unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 8000) {
    delay(300);
  }

  if (WiFi.status() == WL_CONNECTED) {
    lv_label_set_text_fmt(wifi_status_label, "Connected: %s", WiFi.localIP().toString().c_str());
    appConfig.ssid = ssid;
    appConfig.password = pass;
    save_config();
  } else {
    lv_label_set_text(wifi_status_label, "Failed to connect");
  }
}
// ==== EVENT CALLBACK ====
static void settings_event_cb(lv_event_t *e) {
  lv_obj_t *target = lv_event_get_target(e);
  if (target == slider_brightness) {
    appConfig.brightness = lv_slider_get_value(target);
    int val = map(appConfig.brightness, 0, 255, 0, 100);
    lv_label_set_text_fmt(lbl_brightness, "Brightness: %d%%", val);
    if (appConfig.brightness < 10){
      appConfig.brightness = 10;
    }
    Serial.println(appConfig.brightness);
    display.setBrightness(appConfig.brightness);
    
    //analogWrite(5, map(appConfig.brightness, 0, 100, 0, 255));
    //uint8_t brightness = lv_slider_get_value(ui_Slider1) ;
    //display.setBrightness(brightness);
  } else if (target == switch_ai) {
    appConfig.ai_mode = lv_obj_has_state(switch_ai, LV_STATE_CHECKED);
  } 
  //else if (target == ta_ssid || target == ta_pass) {
   // appConfig.wifi_ssid = lv_textarea_get_text(ta_ssid);
   // appConfig.wifi_pass = lv_textarea_get_text(ta_pass);
  //}
  save_config();
}

// ==== SETTINGS PAGE ====
void create_settings_page() {
  lv_obj_clean(lv_scr_act());

  lv_obj_t *tabview = lv_tabview_create(lv_scr_act(), LV_DIR_TOP, 40);
  lv_obj_t *tab_display = lv_tabview_add_tab(tabview, "Display");
  lv_obj_t *tab_ai = lv_tabview_add_tab(tabview, "AI Mode");
  lv_obj_t *tab_wifi = lv_tabview_add_tab(tabview, "Wi-Fi");

  // Display Tab
  lbl_brightness = lv_label_create(tab_display);
 // lv_label_set_text(lbl_brightness, "Brightness");
  lv_label_set_text_fmt(lbl_brightness, "Brightness: %d%%", appConfig.brightness);
  lv_obj_align(lbl_brightness, LV_ALIGN_TOP_MID, 0, 10);

  slider_brightness = lv_slider_create(tab_display);
  //lv_obj_set_width(slider_brightness, 500);
  lv_obj_set_size(slider_brightness, 500, 40); // wider and thicker
  lv_slider_set_range(slider_brightness, 0, 255);
  lv_slider_set_value(slider_brightness, appConfig.brightness, LV_ANIM_OFF);
  lv_obj_align(slider_brightness, LV_ALIGN_TOP_MID, 0, 40);
  lv_obj_add_event_cb(slider_brightness, settings_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

  // Style the slider for better visibility
  static lv_style_t style_slider;
  lv_style_init(&style_slider);
  lv_style_set_bg_color(&style_slider, lv_palette_main(LV_PALETTE_BLUE));
  lv_style_set_bg_grad_color(&style_slider, lv_palette_lighten(LV_PALETTE_BLUE, 2));
  lv_style_set_bg_grad_dir(&style_slider, LV_GRAD_DIR_HOR);
  lv_style_set_radius(&style_slider, 20);
  lv_style_set_pad_all(&style_slider, 4);
  lv_style_set_outline_width(&style_slider, 2);
  lv_style_set_outline_color(&style_slider, lv_color_black());
  lv_obj_add_style(slider_brightness, &style_slider, LV_PART_MAIN);

  static lv_style_t style_knob;
  lv_style_init(&style_knob);
  lv_style_set_bg_color(&style_knob, lv_palette_main(LV_PALETTE_GREEN));
  lv_style_set_radius(&style_knob, LV_RADIUS_CIRCLE);
  lv_style_set_pad_all(&style_knob, 8);
  lv_obj_add_style(slider_brightness, &style_knob, LV_PART_KNOB);


  

  // AI Tab
  lv_obj_t *lbl_ai = lv_label_create(tab_ai);
  lv_label_set_text(lbl_ai, "Enable AI Mode");
  lv_obj_align(lbl_ai, LV_ALIGN_TOP_MID, 0, 10);

  switch_ai = lv_switch_create(tab_ai);
  lv_obj_align(switch_ai, LV_ALIGN_TOP_MID, 0, 50);
  if (appConfig.ai_mode)
    lv_obj_add_state(switch_ai, LV_STATE_CHECKED);
  lv_obj_add_event_cb(switch_ai, settings_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

  // Wi-Fi Tab
  lv_obj_t *lbl = lv_label_create(tab_wifi);
  lv_label_set_text(lbl, "Wi-Fi Settings");
  lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, 10);

  // --- Dropdown for SSID ---
  wifi_dropdown = lv_dropdown_create(tab_wifi);
  lv_obj_set_size(wifi_dropdown, 400, 45);
  lv_obj_align(wifi_dropdown, LV_ALIGN_TOP_MID, 0, 50);
  lv_dropdown_set_text(wifi_dropdown, "Select Wi-Fi SSID");
  populate_wifi_dropdown();

  // --- Password Field ---
  wifi_pass_field = lv_textarea_create(tab_wifi);
  lv_textarea_set_password_mode(wifi_pass_field, true);
  lv_textarea_set_placeholder_text(wifi_pass_field, "Wi-Fi Password");
  lv_obj_set_size(wifi_pass_field, 400, 45);
  lv_obj_align(wifi_pass_field, LV_ALIGN_TOP_MID, 0, 110);

  // === Create Keyboard (hidden by default) ===
  wifi_keyboard = lv_keyboard_create(tab_wifi);
  lv_obj_set_size(wifi_keyboard, 720, 250);
  lv_obj_align(wifi_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_add_flag(wifi_keyboard, LV_OBJ_FLAG_HIDDEN);

  // === Show keyboard when focused ===
  lv_obj_add_event_cb(wifi_pass_field, [](lv_event_t *e) {
    lv_keyboard_set_textarea(wifi_keyboard, wifi_pass_field);
    lv_obj_clear_flag(wifi_keyboard, LV_OBJ_FLAG_HIDDEN);
  }, LV_EVENT_FOCUSED, NULL);

  // === Hide keyboard when defocused ===
  lv_obj_add_event_cb(wifi_pass_field, [](lv_event_t *e) {
    lv_obj_add_flag(wifi_keyboard, LV_OBJ_FLAG_HIDDEN);
  }, LV_EVENT_DEFOCUSED, NULL);

  // --- Connect Button ---
  lv_obj_t *btn_connect = lv_btn_create(tab_wifi);
  lv_obj_set_size(btn_connect, 150, 45);
  lv_obj_align(btn_connect, LV_ALIGN_TOP_MID, 0, 180);
  lv_obj_t *lbl_btn = lv_label_create(btn_connect);
  lv_label_set_text(lbl_btn, "Connect");
  lv_obj_center(lbl_btn);
  lv_obj_add_event_cb(btn_connect, connect_to_wifi, LV_EVENT_CLICKED, NULL);

  // --- Wi-Fi Status Label ---
  wifi_status_label = lv_label_create(tab_wifi);
  lv_label_set_text(wifi_status_label, "Status: Not connected");
  lv_obj_align(wifi_status_label, LV_ALIGN_TOP_MID, 0, 240);
}

// ==== INITIALIZATION (Call in setup()) ====
void init_settings() {
  //if (!SPIFFS.begin(true)) {
  //  Serial.println("SPIFFS mount failed!");
  //  return;
  //}
  load_config();
  apply_config();
  connectWiFi();
}
