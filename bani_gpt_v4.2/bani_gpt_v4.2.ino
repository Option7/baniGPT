#include <M5GFX.h>
#include <lvgl.h>
#include <SPIFFS.h>
#include <WiFi.h> 
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <vector>
#include <time.h>
#include <M5Unified.h>
#include "tinyexpr.h"
#include <math.h>
#include "my_font_36.c"
#include "converter.h"
#include "send_icon1.h"      // Custom send button bitmap
#include "user_avatar.h"    // User avatar bitmap
#include "bani_avatar.h"    // Bani avatar bitmap
#include "settings.h"

LV_FONT_DECLARE(my_font_36);


// --------------------
// Display Setup
// --------------------
//M5GFX display;
static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf[720 * 60];  // buffer for 60 rows

// -------- Screen config --------
#define SCREEN_WIDTH 720
#define SCREEN_HEIGHT 1280

// -------- Gemini API --------
const char* GEMINI_API_KEY = "AIzaSyCuhmTUtkBihWxY-Oz54hmpeF1Gb6UPjVY";
const char* GEMINI_ENDPOINT = "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.5-flash:generateContent";
                                //"https://generativelanguage.googleapis.com/v1beta/models/gemini-pro:generateContent";


#define SDIO2_CLK GPIO_NUM_12
#define SDIO2_CMD GPIO_NUM_13
#define SDIO2_D0  GPIO_NUM_11
#define SDIO2_D1  GPIO_NUM_10
#define SDIO2_D2  GPIO_NUM_9
#define SDIO2_D3  GPIO_NUM_8
#define SDIO2_RST GPIO_NUM_15


lv_obj_t *ta_filename;
lv_obj_t *ta_content;
lv_obj_t *note_list;
lv_obj_t *note_kb;  // keyboard object

String currentNotePath = "";



// -------- Clock objects --------
bool alarmActive = false;

const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 0;        // adjust for your timezone
const int daylightOffset_sec = 0;    // adjust for daylight saving

lv_obj_t *time_label;
lv_obj_t *date_label;
lv_obj_t *alarm_label;

int alarmHour = -1;
int alarmMinute = -1;
bool alarmTriggered = false;

// For rollers
lv_obj_t *hourRoller, *minRoller;

// -------- Chat objects --------
lv_obj_t *chat_cont;
lv_obj_t *ta;
lv_obj_t *kb;
lv_obj_t *send_btn;
lv_obj_t *typing_row = nullptr;

// -------- Wi-Fi objects --------
lv_obj_t *wifi_page;
lv_obj_t *chat_page;
lv_obj_t *ssid_ta;
lv_obj_t *pass_ta;
lv_obj_t *wifi_kb;

// Chat history
struct ChatMessage {
    String text;
    bool is_user;
};
std::vector<ChatMessage> chat_history;


// ---------- CONFIG ----------
static const int CALC_HISTORY_MAX = 40;
static bool useDegrees = true;       // DEG/RAD mode
static bool fractionMode = false;    // show fractions when true

// ---------- Forward (integration) ----------
lv_obj_t* create_calculator_screen(); // returns screen pointer to load with lv_scr_load()


// Forward declarations
void create_clock_page();
void create_set_time_page();
void create_set_alarm_page();

// --------------------
// Utility: Back to Menu
// --------------------
void create_main_menu(); // forward declaration

static void back_to_menu(lv_event_t *e) {
  lv_obj_clean(lv_scr_act());
  create_main_menu();
}


// === Save or Update Note ===
void save_note_event(lv_event_t * e) {
    const char *filename = lv_textarea_get_text(ta_filename);
    const char *content  = lv_textarea_get_text(ta_content);

    if (strlen(filename) > 0) {
        String path = "/notes/" + String(filename) + ".txt";
        File f = SPIFFS.open(path, FILE_WRITE);
        if (f) {
            f.print(content);
            f.close();
            currentNotePath = path;
            Serial.println("Note saved/updated: " + path);
        } else {
            Serial.println("Failed to save note!");
        }
    }
}

// === Open Note ===
void open_note(const char* filename) {
    // Ensure filename has proper path
    String path = filename;
    if (!path.startsWith("/notes/")) {
        path = "/notes/" + path;
    }

    if (!path.endsWith(".txt")) {
        path += ".txt";
    }

    // Try to open the file
    File f = SPIFFS.open(path, FILE_READ);
    if (!f) {
        Serial.printf("❌ Failed to open file: %s\n", path.c_str());
        return;
    }

    // Read the content
    String content = f.readString();
    f.close();

    Serial.printf("✅ Opened %s (length=%d)\n", path.c_str(), content.length());

    // Create screen
    lv_obj_t* scr_note = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_note, lv_color_hex(0xf5f5f5), 0);
    lv_obj_set_scrollbar_mode(scr_note, LV_SCROLLBAR_MODE_OFF);

    // HEADER
    lv_obj_t* header = lv_obj_create(scr_note);
    lv_obj_set_size(header, lv_pct(100), 45);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x0078D7), 0);
    lv_obj_set_style_pad_all(header, 5, 0);
    lv_obj_set_style_border_width(header, 0, 0);

    // BACK BUTTON
    lv_obj_t* btn_back = lv_btn_create(header);
    lv_obj_set_size(btn_back, 40, 35);
    lv_obj_align(btn_back, LV_ALIGN_LEFT_MID, 5, 0);
    lv_obj_set_style_bg_color(btn_back, lv_color_hex(0x005bb5), 0);

    lv_obj_t* lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, LV_SYMBOL_LEFT);
    lv_obj_center(lbl_back);
    lv_obj_add_event_cb(btn_back, [](lv_event_t* e) {
        browse_notes_page();
    }, LV_EVENT_CLICKED, NULL);

    // TITLE
    String fname = path;
    fname.replace("/notes/", "");
    fname.replace(".txt", "");

    lv_obj_t* lbl_title = lv_label_create(header);
    lv_label_set_text(lbl_title, fname.c_str());
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(lbl_title, lv_color_white(), 0);
    lv_obj_align(lbl_title, LV_ALIGN_LEFT_MID, 55, 0);

    // TEXT VIEW
    lv_obj_t* ta_view = lv_textarea_create(scr_note);
    lv_obj_set_size(ta_view, lv_pct(100), lv_obj_get_height(scr_note) - 55);
    lv_obj_align(ta_view, LV_ALIGN_BOTTOM_MID, 0, -5);
    lv_textarea_set_text(ta_view, content.c_str());
    lv_textarea_set_cursor_click_pos(ta_view, false);
    lv_obj_set_scroll_dir(ta_view, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(ta_view, LV_SCROLLBAR_MODE_AUTO);
    lv_textarea_set_one_line(ta_view, false);
    lv_obj_set_style_text_font(ta_view, &lv_font_montserrat_26, 0);
    lv_obj_set_style_bg_color(ta_view, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_border_color(ta_view, lv_color_hex(0xcccccc), 0);
    lv_obj_set_style_border_width(ta_view, 1, 0);
       // lv_obj_set_scroll_dir(chat_cont, LV_DIR_VER);
       // lv_obj_set_scrollbar_mode(chat_cont, LV_SCROLLBAR_MODE_AUTO);
       // lv_obj_set_flex_flow(chat_cont, LV_FLEX_FLOW_COLUMN);
    // Make read-only
    lv_obj_clear_flag(ta_view, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_state(ta_view, LV_STATE_DISABLED);

    // Load new screen
    lv_scr_load_anim(scr_note, LV_SCR_LOAD_ANIM_FADE_IN, 300, 0, true);
}




// === Delete Note ===
void delete_note(const char* path) {
    String fullPath = String(path);

    // Ensure the correct directory prefix
    if (!fullPath.startsWith("/notes/")) {
        fullPath = "/notes/" + fullPath;
    }

    if (SPIFFS.exists(fullPath)) {
        if (SPIFFS.remove(fullPath)) {
            Serial.println("✅ Deleted: " + fullPath);
        } else {
            Serial.println("❌ Failed to delete: " + fullPath);
        }
    } else {
        Serial.println("⚠️ File not found: " + fullPath);
    }

    // Refresh notes list after deletion
    browse_notes_page();
}


// === Populate Notes List with Delete Buttons ===
void load_notes_list() {
    lv_obj_clean(lv_scr_act()); // Clear previous content

    lv_obj_t *note_list = lv_list_create(lv_scr_act());
    lv_obj_set_size(note_list, 720, 1200);
    lv_obj_align(note_list, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_set_flex_flow(note_list, LV_FLEX_FLOW_COLUMN);

    File root = SPIFFS.open("/notes");
    if (!root || !root.isDirectory()) {
        Serial.println("No /notes directory found.");
        lv_obj_t *lbl = lv_label_create(lv_scr_act());
        lv_label_set_text(lbl, "No notes found.");
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_24, 0);
        lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);
        return;
    }

    File file = root.openNextFile();
    if (file) {
        if (!file.isDirectory()) {
            String path = file.name();  // e.g. "/notes/test.txt"
            String fname = path;
            fname.replace("/notes/", "");

            // Row container
            lv_obj_t *row = lv_obj_create(note_list);
            lv_obj_set_size(row, 700, 45);
            lv_obj_set_style_text_font(row, &lv_font_montserrat_24, 0);
            lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
            lv_obj_set_style_pad_all(row, 5, 0);
            lv_obj_set_style_border_width(row, 0, 0);

            // File button (opens note)
            lv_obj_t *btn_file = lv_btn_create(row);
            lv_obj_set_size(btn_file, 580, 40);
            lv_obj_t *label_file = lv_label_create(btn_file);
            lv_label_set_text(label_file, fname.c_str());
            lv_obj_center(label_file);

            // Open note event
            char *stored_path = strdup(path.c_str());  // persist safely
            lv_obj_add_event_cb(btn_file, [](lv_event_t * e) {
                const char *path = (const char *)lv_event_get_user_data(e);
                open_note(path);
            }, LV_EVENT_CLICKED, stored_path);


            lv_obj_t *btn_back = lv_btn_create(lv_scr_act());
            lv_obj_align(btn_back, LV_ALIGN_BOTTOM_MID, 0, -10);
            lv_obj_t *label = lv_label_create(btn_back);
            lv_label_set_text(label, "Back to Editor");
            lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
            lv_obj_center(label);
            lv_obj_add_event_cb(btn_back, [](lv_event_t * e){
                create_note_page();
            }, LV_EVENT_CLICKED, NULL);

            // Delete button
            lv_obj_t *btn_del = lv_btn_create(row);
            lv_obj_set_size(btn_del, 80, 40);
            lv_obj_t *label_del = lv_label_create(btn_del);
            lv_label_set_text(label_del, "Del");
            lv_obj_center(label_del);

            // Delete event
            lv_obj_add_event_cb(btn_del, [](lv_event_t * e) {
                const char *path = (const char *)lv_event_get_user_data(e);
                delete_note(path);
                load_notes_list();  // refresh list
            }, LV_EVENT_CLICKED, stored_path);
        }
        file = root.openNextFile();
    }  

    else if (!file){

        Serial.println("No /notes directory found.");
        lv_obj_t *lbl = lv_label_create(lv_scr_act());
        lv_label_set_text(lbl, "No notes found.");
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_24, 0);
        lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);
        //return;

        lv_obj_t *btn_back = lv_btn_create(lv_scr_act());
        lv_obj_align(btn_back, LV_ALIGN_BOTTOM_MID, 0, -10);
        lv_obj_t *label = lv_label_create(btn_back);
        lv_label_set_text(label, "Back to Editor");
        lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
        lv_obj_center(label);
        lv_obj_add_event_cb(btn_back, [](lv_event_t * e){
                create_note_page();
        }, LV_EVENT_CLICKED, NULL);

    }
    root.close();
}


// === Note Taking / Editing Page ===
// === Create Note Page with keyboard ===
void create_note_page() {
    lv_obj_clean(lv_scr_act());

    // Container
    lv_obj_t *note = lv_obj_create(lv_scr_act());
    lv_obj_set_size(note, 720, 1280);
    lv_obj_center(note);
    lv_obj_clear_flag(note, LV_OBJ_FLAG_SCROLLABLE);

    // === Keyboard (create first to ensure existence) ===
    note_kb = lv_keyboard_create(note);
    lv_obj_set_size(note_kb, 720, 350);
    lv_obj_align(note_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(note_kb, NULL);
    lv_obj_add_flag(note_kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_text_font(note_kb, &lv_font_montserrat_36, 0);

   

    // === Filename TextArea ===
    ta_filename = lv_textarea_create(note);
    lv_obj_set_size(ta_filename, 720, 75);
    lv_obj_align(ta_filename, LV_ALIGN_TOP_MID, 0, 60);
    lv_textarea_set_placeholder_text(ta_filename, "File name");
    lv_obj_set_style_text_font(ta_filename, &lv_font_montserrat_24, 0);

    // === Content TextArea ===
    ta_content = lv_textarea_create(note);
    lv_obj_set_size(ta_content, 720, 700);
    lv_obj_align(ta_content, LV_ALIGN_CENTER, 0, -90);
    lv_textarea_set_placeholder_text(ta_content, "Write note here...");
    lv_textarea_set_one_line(ta_content, false);
    lv_obj_set_style_text_font(ta_content, &lv_font_montserrat_24, 0);

    lv_obj_t *back_btn = lv_btn_create(note);
    lv_obj_set_size(back_btn, 120, 60);
    lv_obj_align(back_btn, LV_ALIGN_TOP_LEFT, 0, -10);
    lv_obj_add_event_cb(back_btn, back_to_menu, LV_EVENT_CLICKED, NULL);

    lv_obj_t *back_btn_label = lv_label_create(back_btn);
    lv_label_set_text(back_btn_label, "Back");
    lv_obj_set_style_text_font(back_btn_label, &lv_font_montserrat_28, 0);
    lv_obj_center(back_btn_label);

    // === Save Button ===
    lv_obj_t *btn_save = lv_btn_create(note);
    lv_obj_set_size(btn_save, 190, 60);
    lv_obj_align(btn_save, LV_ALIGN_TOP_MID, 0, -10);
    lv_obj_t *label1 = lv_label_create(btn_save);
    lv_label_set_text(label1, "Save/Update");
    lv_obj_set_style_text_font(label1, &lv_font_montserrat_28, 0);
    lv_obj_center(label1);
    lv_obj_add_event_cb(btn_save, save_note_event, LV_EVENT_CLICKED, NULL);

    // === Browse Button ===
    lv_obj_t *btn_browse = lv_btn_create(note);
    lv_obj_set_size(btn_browse, 120, 60);
    lv_obj_align(btn_browse, LV_ALIGN_TOP_RIGHT, 0, -10);
    lv_obj_t *label2 = lv_label_create(btn_browse);
    lv_label_set_text(label2, "Browse");
    lv_obj_set_style_text_font(label2, &lv_font_montserrat_28, 0);
    lv_obj_center(label2);
    lv_obj_add_event_cb(btn_browse, [](lv_event_t * e){
        lv_obj_clean(lv_scr_act());
        browse_notes_page();
    }, LV_EVENT_CLICKED, NULL);

   // lv_keyboard_set_textarea(note_kb, ta_filename);
   // lv_keyboard_set_textarea(note_kb, ta_content);
    //lv_obj_clear_flag(note_kb, LV_OBJ_FLAG_HIDDEN);
//*
    lv_obj_add_event_cb(ta_filename, [](lv_event_t *e) {
        lv_keyboard_set_textarea(note_kb, ta_filename);
        lv_obj_clear_flag(note_kb, LV_OBJ_FLAG_HIDDEN);
    }, LV_EVENT_FOCUSED, NULL);

    lv_obj_add_event_cb(ta_content, [](lv_event_t *e) {
        lv_keyboard_set_textarea(note_kb, ta_content);
        lv_obj_clear_flag(note_kb, LV_OBJ_FLAG_HIDDEN);
    }, LV_EVENT_FOCUSED, NULL);

    lv_obj_add_event_cb(ta_filename, [](lv_event_t *e) {
        lv_obj_add_flag(note_kb, LV_OBJ_FLAG_HIDDEN);
    }, LV_EVENT_DEFOCUSED, NULL);

    lv_obj_add_event_cb(ta_content, [](lv_event_t *e) {
        lv_obj_add_flag(note_kb, LV_OBJ_FLAG_HIDDEN);
    }, LV_EVENT_DEFOCUSED, NULL);
      //*/
}

// === Browse Page ===
void browse_notes_page() {
    lv_obj_clean(lv_scr_act());
    load_notes_list();
   
}

// ---------- Helpers: wrapped math for tinyexpr ----------
static double fn_sin(double x)  { return useDegrees ? sin(x * M_PI / 180.0) : sin(x); }
static double fn_cos(double x)  { return useDegrees ? cos(x * M_PI / 180.0) : cos(x); }
static double fn_tan(double x)  { return useDegrees ? tan(x * M_PI / 180.0) : tan(x); }

static double fn_asin(double x) { return useDegrees ? asin(x) * 180.0 / M_PI : asin(x); }
static double fn_acos(double x) { return useDegrees ? acos(x) * 180.0 / M_PI : acos(x); }
static double fn_atan(double x) { return useDegrees ? atan(x) * 180.0 / M_PI : atan(x); }

static double mysqrt(double x) { return ::sqrt(x); }
static double mylog10(double x) { return ::log10(x); }
static double myln(double x) { return ::log(x); }
static double mypow(double a, double b) { return ::pow(a,b); }
static double myabs(double x) { return fabs(x); }
static double myfact(double x) {
  if (x < 0) return NAN;
  double ip;
  if (modf(x, &ip) == 0.0 && x >= 0 && x <= 170) {
    double r = 1.0;
    for (int i = 1; i <= (int)x; ++i) r *= i;
    return r;
  }
  return tgamma(x + 1.0);
}

// ---------- Tinyexpr variable table (used each evaluation) ----------
static te_variable calc_vars_template[] = {
  {"sin", (const void*)fn_sin, TE_FUNCTION1},
  {"cos", (const void*)fn_cos, TE_FUNCTION1},
  {"tan", (const void*)fn_tan, TE_FUNCTION1},
  {"asin",(const void*)fn_asin,TE_FUNCTION1},
  {"acos",(const void*)fn_acos,TE_FUNCTION1},
  {"atan",(const void*)fn_atan,TE_FUNCTION1},

  {"sqrt",(const void*)mysqrt, TE_FUNCTION1},
  {"log10",(const void*)mylog10, TE_FUNCTION1},
  {"ln",  (const void*)myln,   TE_FUNCTION1},
  {"pow", (const void*)mypow,  TE_FUNCTION2},
  {"abs", (const void*)myabs,  TE_FUNCTION1},
  {"fact",(const void*)myfact, TE_FUNCTION1},

  // constants (we'll register as variables pointing to these storage)
  {"pi", (void*)nullptr, TE_VARIABLE},
  {"e",  (void*)nullptr, TE_VARIABLE},
};

// ---------- Preprocessor: handle ^ -> pow(...) and replace unicode tokens ----------
static String preprocess_expr(const String &in_raw) {
  String s = in_raw;

  // Normalize: convert '×' '÷' to '*' '/'
  s.replace("x", "*");
  s.replace("÷", "/");

  // Replace Unicode π with "pi" token (tinyexpr has 'pi' var)
  s.replace("π", "pi");

  // Replace caret ^ with pow(a,b) using a simple parser for common cases.
  // This handles numbers/parenthesized expressions/basic tokens: it is not a full parser but covers usual inputs.
  // We'll convert occurrences like a^b into pow(a,b)
  // Implementation: scan and rebuild string; when '^' found, find left token and right token.
  String out = "";
  for (size_t i = 0; i < s.length(); ++i) {
    char c = s[i];
    if (c != '^') {
      out += c;
      continue;
    }
    // Found '^' at out.length() position - find left token in 'out'
    int L = (int)out.length() - 1;
    // left token can be digits, ., ), letters or spaces
    // if ) then find matching '(' backwards
    int leftStart = L;
    if (leftStart >= 0 && out[leftStart] == ')') {
      int depth = 0;
      while (leftStart >= 0) {
        if (out[leftStart] == ')') depth++;
        else if (out[leftStart] == '(') {
          depth--; 
          if (depth == 0) { leftStart--; break; }
        }
        leftStart--;
      }
      leftStart++;
    } else {
      while (leftStart >= 0 && ( (out[leftStart] >= '0' && out[leftStart] <= '9') || out[leftStart]=='.' || isalpha(out[leftStart]) )) leftStart--;
      leftStart++;
    }
    String leftToken = out.substring(leftStart);

    // remove left token from out
    out.remove(leftStart);

    // find right token in s (from i+1)
    int j = (int)i + 1;
    // skip spaces
    while (j < (int)s.length() && s[j] == ' ') j++;
    int rightStart = j;
    String rightToken = "";
    if (j < (int)s.length() && s[j] == '(') {
      // find matching ')'
      int depth = 0;
      int k = j;
      for (; k < (int)s.length(); ++k) {
        if (s[k] == '(') depth++;
        else if (s[k] == ')') {
          depth--;
          if (depth == 0) { rightToken = s.substring(j, k+1); i = k; break; }
        }
      }
      if (rightToken == "") { // no matching, just take until next operator
        int k2 = j;
        while (k2 < (int)s.length() && (isalnum(s[k2]) || s[k2]=='.' || s[k2]=='(' || s[k2]==')')) k2++;
        rightToken = s.substring(j, k2-1);
        i = k2-1;
      }
    } else {
      // token is number or name
      int k = j;
      while (k < (int)s.length() && ( (s[k] >= '0' && s[k] <= '9') || s[k]=='.' || isalpha(s[k]) )) k++;
      rightToken = s.substring(j, k);
      i = k-1;
    }

    // append pow(left,right)
    out += "pow(" + leftToken + "," + rightToken + ")";
  }

  s = out;

  // Handle percent: "50%" -> pct(50)
  out = "";
  for (size_t i = 0; i < s.length(); ++i) {
    char c = s[i];
    if (c == '%') {
      // find previous number
      int j = (int)out.length()-1;
      while (j >= 0 && ( (out[j]>='0' && out[j]<='9') || out[j]=='.' )) j--;
      String num = out.substring(j+1);
      out.remove(j+1);
      out += " ( " + num + " / 100.0 ) ";
    } else out += c;
  }
  s = out;

  return s;
}


// ---------- Evaluate an expression using tinyexpr ----------
static bool evaluate_with_tinyexpr(const String &rawExpr, double &outVal) {
  String expr = preprocess_expr(rawExpr);

  // copy the template and set constant pointers properly (we need storage for pi and e)
  double pi_storage = M_PI;
  double e_storage  = M_E;

  // build variable table (fresh each time)
  te_variable vars[] = {
    {"sin", (const void*)fn_sin, TE_FUNCTION1},
    {"cos", (const void*)fn_cos, TE_FUNCTION1},
    {"tan", (const void*)fn_tan, TE_FUNCTION1},
    {"asin",(const void*)fn_asin, TE_FUNCTION1},
    {"acos",(const void*)fn_acos, TE_FUNCTION1},
    {"atan",(const void*)fn_atan, TE_FUNCTION1},

    {"sqrt",(const void*)mysqrt, TE_FUNCTION1},
    {"log10",(const void*)mylog10, TE_FUNCTION1},
    {"ln",  (const void*)myln,   TE_FUNCTION1},
    {"pow", (const void*)mypow,  TE_FUNCTION2},
    {"abs", (const void*)myabs,  TE_FUNCTION1},
    {"fact",(const void*)myfact,  TE_FUNCTION1},

    {"pi", &pi_storage, TE_VARIABLE},
    {"e",  &e_storage,  TE_VARIABLE}
  };

  int err;
  te_expr *n = te_compile(expr.c_str(), vars, sizeof(vars)/sizeof(te_variable), &err);
  if (!n) {
    // fallback to te_interp if compile failed
    double r = te_interp(expr.c_str(), &err);
    if (err != 0) return false;
    outVal = r;
    return true;
  }
  double r = te_eval(n);
  te_free(n);
  outVal = r;
  return true;
}

// ---------- Fraction converter ----------
static void decimal_to_fraction(double value, long long &num, long long &den, long long maxDen=10000) {
  const double EPS = 1e-12;
  if (isnan(value) || isinf(value)) { num = 0; den = 1; return; }
  long long a0 = (long long)floor(value);
  double rem = value - a0;
  if (fabs(rem) < EPS) { num = a0; den = 1; return; }

  double x = value;
  long long p0 = 1, q0 = 0, p1 = (long long)floor(x), q1 = 1;
  while (true) {
    x = 1.0 / (x - floor(x));
    long long a = (long long)floor(x);
    long long p2 = a * p1 + p0;
    long long q2 = a * q1 + q0;
    if (q2 > maxDen) break;
    p0 = p1; q0 = q1; p1 = p2; q1 = q2;
    double approx = (double)p1 / (double)q1;
    if (fabs(approx - value) < 1e-12) break;
  }
  num = p1; den = q1;
}

// ---------- UI state ----------
//static lv_obj_t* calc_screen = nullptr;
static lv_obj_t* expr_label = nullptr;
//static lv_obj_t* result_label = nullptr;
static lv_obj_t* history_list = nullptr;
static lv_obj_t* degrad_label = nullptr;
static String history_buffer[CALC_HISTORY_MAX];
static int history_count = 0;

// ---------- UI styling helpers ----------
static void style_button(lv_obj_t* btn) {
  lv_obj_set_style_radius(btn, 12, LV_PART_MAIN);
  lv_obj_set_style_pad_all(btn, 8, LV_PART_MAIN);
  lv_obj_set_style_bg_color(btn, lv_color_hex(0x1E88E5), LV_PART_MAIN);
  lv_obj_set_style_bg_color(btn, lv_color_hex(0x1565C0), LV_STATE_PRESSED);
  lv_obj_set_style_text_color(btn, lv_color_white(), LV_PART_MAIN);
}

static void style_func_button(lv_obj_t* btn) {
  lv_obj_set_style_radius(btn, 12, LV_PART_MAIN);
  lv_obj_set_style_pad_all(btn, 8, LV_PART_MAIN);
  lv_obj_set_style_bg_color(btn, lv_color_hex(0x424242), LV_PART_MAIN);
  lv_obj_set_style_bg_color(btn, lv_color_hex(0x212121), LV_STATE_PRESSED);
  lv_obj_set_style_text_color(btn, lv_color_white(), LV_PART_MAIN);
}

static void add_history_entry(const String &expr, const String &res) {
  if (history_count >= CALC_HISTORY_MAX) {
    // rotate
    for (int i = 1; i < CALC_HISTORY_MAX; ++i) history_buffer[i-1] = history_buffer[i];
    history_count = CALC_HISTORY_MAX - 1;
  }
  history_buffer[history_count++] = expr + " = " + res;

  // update LVGL list
  lv_list_create(history_list);
  for (int i = history_count - 1; i >= 0; --i) { // newest at top
    lv_obj_t *btn = lv_list_add_btn(history_list, NULL, history_buffer[i].c_str());
    // store the expression portion as user data for easy reload (we'll parse before ' = ')
    // We set event to reload
    lv_obj_add_event_cb(btn, [](lv_event_t *e) {
      lv_obj_t *btn = lv_event_get_target(e);
      const char *txt = lv_list_get_btn_text(lv_obj_get_parent(btn), btn);
      // txt format "expr = result"
      String s(txt);
      int pos = s.indexOf(" = ");
      String expr = pos >= 0 ? s.substring(0, pos) : s;
      lv_label_set_text(expr_label, expr.c_str());
    }, LV_EVENT_CLICKED, NULL);
  }
}

struct StatsPopupData {
    bool wantStdDev;  // false = mean, true = stddev
};

static void stats_btn_event_cb(lv_event_t *e) {
    StatsPopupData *data = (StatsPopupData *) lv_event_get_user_data(e);
    if (!data) return;

    if (data->wantStdDev) {
        Serial.println("Standard Deviation calculation selected.");
        // 👉 Add your stddev calculation logic here
    } else {
        Serial.println("Mean calculation selected.");
        // 👉 Add your mean calculation logic here
    }

    // Close popup after selection
    lv_obj_del(lv_event_get_current_target(e)->parent);
}

static void show_stats_popup() {
    // Create a popup container
    lv_obj_t *popup = lv_obj_create(lv_scr_act());
    lv_obj_set_size(popup, 220, 100);
    lv_obj_center(popup);
    lv_obj_set_style_bg_color(popup, lv_color_hex(0x222222), 0);
    lv_obj_set_style_radius(popup, 12, 0);
    lv_obj_set_style_pad_all(popup, 8, 0);

    // Title
    lv_obj_t *title = lv_label_create(popup);
    lv_label_set_text(title, "Statistics");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

    // Horizontal container for buttons
    lv_obj_t *cont = lv_obj_create(popup);
    lv_obj_set_size(cont, 200, 50);
    lv_obj_align(cont, LV_ALIGN_BOTTOM_MID, 0, -5);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_gap(cont, 10, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);

    // --- Mean button ---
    lv_obj_t *btn_mean = lv_btn_create(cont);
    lv_obj_set_size(btn_mean, 90, 40);
    lv_obj_t *lbl_mean = lv_label_create(btn_mean);
    lv_label_set_text(lbl_mean, "Mean");
    lv_obj_center(lbl_mean);

    static StatsPopupData meanData;
    meanData.wantStdDev = false;
    lv_obj_add_event_cb(btn_mean, stats_btn_event_cb, LV_EVENT_CLICKED, &meanData);

    // --- StdDev button ---
    lv_obj_t *btn_std = lv_btn_create(cont);
    lv_obj_set_size(btn_std, 90, 40);
    lv_obj_t *lbl_std = lv_label_create(btn_std);
    lv_label_set_text(lbl_std, "StdDev");
    lv_obj_center(lbl_std);

    static StatsPopupData stdData;
    stdData.wantStdDev = true;
    lv_obj_add_event_cb(btn_std, stats_btn_event_cb, LV_EVENT_CLICKED, &stdData);
}


// ---------- Popup: Unit conversion (simple) ----------
static void show_unit_conversion_popup() {
  lv_obj_clean(lv_scr_act());
  lv_obj_t *popup = lv_obj_create(lv_scr_act());
  lv_obj_set_size(popup, 640, 420);
  lv_obj_center(popup);

  lv_obj_t *lbl = lv_label_create(popup);
  lv_label_set_text(lbl, "Unit conversion: length (m ↔ cm ↔ mm) / temperature (°C ↔ °F)");
  lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, 8);

  lv_obj_t *ta = lv_textarea_create(popup);
  lv_obj_set_size(ta, 560, 80);
  lv_obj_align(ta, LV_ALIGN_TOP_MID, 0, 60);
  lv_textarea_set_placeholder_text(ta, "Enter value (e.g. 10 m to cm)");

  lv_obj_t *btn = lv_btn_create(popup);
  lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -20);
  lv_obj_t *bll = lv_label_create(btn);
  lv_label_set_text(bll, "Convert");

  lv_obj_add_event_cb(btn, [](lv_event_t *e) {
    lv_obj_t *p = lv_obj_get_parent(lv_event_get_target(e));
    lv_obj_t *ta = lv_obj_get_child(p, 1);
    String q = lv_textarea_get_text(ta);

    // very simple parsing: "<value> <from> to <to>"
    q.trim();
    q.toLowerCase();
    double val = 0;
    String from, to;
    int sp = q.indexOf(' ');
    if (sp < 0) { lv_label_set_text(result_label, "Parse error"); lv_obj_del(p); return; }
    val = q.substring(0, sp).toDouble();
    int toPos = q.indexOf(" to ");
    if (toPos < 0) { lv_label_set_text(result_label, "Use 'to' syntax"); lv_obj_del(p); return; }
    from = q.substring(sp+1, toPos);
    to = q.substring(toPos+4);

    String out = "Err";
    // handle length: m, cm, mm
    if ((from == "m" || from == "meter" || from=="meters") && (to == "cm" || to=="centimeter")) {
      out = String(val * 100.0);
    } else if ((from=="cm") && (to=="m")) {
      out = String(val / 100.0);
    } else if ((from=="c") && (to=="f")) {
      out = String(val * 9.0/5.0 + 32.0);
    } else if ((from=="f") && (to=="c")) {
      out = String((val - 32.0) * 5.0/9.0);
    } else {
      out = "Unsupported";
    }

    lv_label_set_text(result_label, out.c_str());
    add_history_entry(q, out);
    lv_obj_del(p);
  }, LV_EVENT_CLICKED, NULL);
}

// ---------- Main calculator button handler ----------
static void calc_button_handler(lv_event_t *e) {
  const char *token = (const char*)lv_event_get_user_data(e);
  if (!token) return;

  String t(token);

  if (t == "C") {
    lv_label_set_text(expr_label, "");
    lv_label_set_text(result_label, "");
    return;
  } 
  if (t == "DEL") {
    const char *cur = lv_label_get_text(expr_label);
    String s(cur);
    if (!s.isEmpty()) {
      s.remove(s.length()-1);
      lv_label_set_text(expr_label, s.c_str());
    }
    return;
  }
  if (t == "=") {
    const char *cur = lv_label_get_text(expr_label);
    String expr(cur);
    double val;
    if (evaluate_with_tinyexpr(expr, val)) {
      // fraction mode handling
      if (fractionMode) {
        long long n,d; decimal_to_fraction(val, n, d, 10000);
        char buf[64]; snprintf(buf, sizeof(buf), "%lld/%lld", n, d);
        lv_label_set_text(result_label, buf);
        add_history_entry(expr, String(buf));
      } else {
        char buf[64]; snprintf(buf, sizeof(buf), "%0.12g", val);
        lv_label_set_text(result_label, buf);
        add_history_entry(expr, String(buf));
      }
    } else {
      lv_label_set_text(result_label, "Error");
    }
    return;
  }
  if (t == "DEG") { useDegrees = true; lv_label_set_text(degrad_label, "DEG"); return; }
  if (t == "RAD") { useDegrees = false; lv_label_set_text(degrad_label, "RAD"); return; }
  if (t == "FRAC") { fractionMode = !fractionMode; lv_label_set_text(result_label, fractionMode ? "FRAC ON" : "FRAC OFF"); return; }
  if (t == "STATS:MEAN") { }//show_stats_popup(); return; }
  if (t == "STATS:STD")  { }//show_stats_popup(); return; }
  if (t == "CONV") { }//show_unit_conversion_popup(); return; }
  if (t == "Home") {
    // integrate with your main menu loader
    // TODO: replace the following with your actual main menu scr pointer or function
    //extern lv_obj_t* create_main_menu(); // you must provide this in your main project
    create_main_menu();
    //return;
  }

  // default: append token text to expression label
  const char *cur = lv_label_get_text(expr_label);
  String s(cur);
  s += t;
  lv_label_set_text(expr_label, s.c_str());
}


// ----- Casio Style Button Styles -----
static lv_style_t style_digit;
static lv_style_t style_operator;
static lv_style_t style_function;

void init_calc_styles() {
  lv_style_init(&style_digit);
  lv_style_set_radius(&style_digit, 20); // rounded
  lv_style_set_bg_color(&style_digit, lv_color_hex(0x007BFF)); // blue
  lv_style_set_text_color(&style_digit, lv_color_hex(0xFFFFFF));
  lv_style_set_border_width(&style_digit, 2);
  lv_style_set_border_color(&style_digit, lv_color_hex(0x004C99));

  lv_style_init(&style_operator);
  lv_style_set_radius(&style_operator, 20);
  lv_style_set_bg_color(&style_operator, lv_color_hex(0x28A745)); // green
  lv_style_set_text_color(&style_operator, lv_color_hex(0xFFFFFF));
  lv_style_set_border_width(&style_operator, 2);
  lv_style_set_border_color(&style_operator, lv_color_hex(0x145A2E));

  lv_style_init(&style_function);
  lv_style_set_radius(&style_function, 20);
  lv_style_set_bg_color(&style_function, lv_color_hex(0xFF8C00)); // orange
  lv_style_set_text_color(&style_function, lv_color_hex(0xFFFFFF));
  lv_style_set_border_width(&style_function, 2);
  lv_style_set_border_color(&style_function, lv_color_hex(0xB35900));
}



// ---------- Create calculator screen (call lv_scr_load(...) to show) ----------
//lv_obj_t* 
void create_calculator_page() {
  // If already created, return it
  //if (calc_screen) return calc_screen;
  lv_obj_clean(lv_scr_act());
  lv_obj_t* calc_screen = lv_obj_create(lv_scr_act());
  lv_obj_set_size(calc_screen, SCREEN_WIDTH, SCREEN_HEIGHT);
  lv_obj_clear_flag(calc_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_center(calc_screen);
  lv_obj_set_scrollbar_mode(calc_screen, LV_SCROLLBAR_MODE_OFF);

  // Header: Home and Mode
  lv_obj_t *home_btn = lv_btn_create(calc_screen);
  lv_obj_set_size(home_btn, 110, 60);
  lv_obj_align(home_btn, LV_ALIGN_TOP_LEFT, 12, 8);
  lv_obj_t *hl = lv_label_create(home_btn);
  lv_label_set_text(hl, "Home");
  lv_obj_set_style_text_font(hl, &lv_font_montserrat_28, 0);
  lv_obj_center(hl);
  lv_obj_add_event_cb(home_btn, calc_button_handler, LV_EVENT_CLICKED, (void*)"Home");
  style_func_button(home_btn);

  degrad_label = lv_label_create(calc_screen);
  lv_label_set_text(degrad_label, useDegrees ? "DEG" : "RAD");
  lv_obj_align(degrad_label, LV_ALIGN_TOP_RIGHT, -12, 16);

  // Expression & Result (two-line)
  expr_label = lv_label_create(calc_screen);
  lv_obj_set_width(expr_label, 660);
  lv_obj_set_style_text_font(expr_label, &lv_font_montserrat_28, 0);
  lv_label_set_text(expr_label, "");
  lv_obj_align(expr_label, LV_ALIGN_TOP_MID, 0, 90);

  result_label = lv_label_create(calc_screen);
  lv_obj_set_width(result_label, 660);
  lv_obj_set_style_text_font(result_label, &lv_font_montserrat_36, 0);
  lv_label_set_text(result_label, "0");
  lv_obj_align(result_label, LV_ALIGN_TOP_MID, 0, 140);

  // History list left side
  history_list = lv_list_create(calc_screen);
  lv_obj_set_size(history_list, 250, 940);
  lv_obj_align(history_list, LV_ALIGN_LEFT_MID, 20, 90);

  // Buttons grid (right side). We'll arrange a 7x6 grid (compact)
  const char *keys[] = {
    "7","8","9","/",
    "4","5","6","x",
    "1","2","3","+",
    "0","("," )","-",
    ".","pi","%","^",
    "sin(","cos(","tan(","log10(",
    "sqrt(","fact(","FRAC", "ln(",
    "Ans","CONV","DEG","RAD",
    "STATS:MEAN","STATS:STD","DEL","C",
    "=", NULL, NULL, NULL
  };

  const int cols = 4;
  const int rows = 10;
  const int btn_w = 85;
  const int btn_h = 85;
  const int start_x = 320;
  const int start_y = 240;
  const int spacing_x = 10;
  const int spacing_y = 10;

    int idx = 0;
  for (int r = 0; r < rows; ++r) {
    for (int c = 0; c < cols; ++c) {
      const char *label = keys[idx++];
      if (!label) continue;

      // create button
      lv_obj_t *btn = lv_btn_create(calc_screen);
      lv_obj_set_size(btn, btn_w, btn_h);
      lv_obj_set_pos(btn, start_x + c * (btn_w + spacing_x), start_y + r * (btn_h + spacing_y));

      // create label (text on button)
      lv_obj_t *lbl = lv_label_create(btn);

      // display substitution for certain tokens
      String showLabel = String(label);
      if (showLabel == "sqrt(") showLabel = "sqrt(";
      if (showLabel == "pi" || showLabel == String("pi")) showLabel = "pi";
      if (showLabel == "log10(") showLabel = "log";
      if (showLabel == "ln(") showLabel = "ln";

      lv_label_set_text(lbl, showLabel.c_str());
      lv_obj_center(lbl);

      // ---------- Determine category and apply explicit styles ----------
      // Digit: single-character 0-9 OR "." OR "Ans"
      bool isDigit = (strlen(label) == 1 && isdigit((unsigned char)label[0])) || (strcmp(label, ".") == 0) || (strcmp(label, "Ans") == 0);

      // Operators: + - * / ^ = % ( ) 
      bool isOperator = (strcmp(label, "+") == 0) || (strcmp(label, "-") == 0) ||
                        (strcmp(label, "x") == 0) || (strcmp(label, "/") == 0) ||
                        (strcmp(label, "^") == 0) || (strcmp(label, "=") == 0) ||
                        (strcmp(label, "%") == 0) || (strcmp(label, "(") == 0) ||
                        (strcmp(label, ")") == 0);

      // Functions / modes
      bool isFunction = (strcmp(label, "sin(") == 0) || (strcmp(label, "cos(") == 0) ||
                        (strcmp(label, "tan(") == 0) || (strcmp(label, "log10(") == 0) ||
                        (strcmp(label, "ln(") == 0) || (strcmp(label, "sqrt(") == 0) ||
                        (strcmp(label, "fact(") == 0) || (strcmp(label, "FRAC") == 0) ||
                        (strcmp(label, "CONV") == 0) || (strcmp(label, "DEG") == 0) ||
                        (strcmp(label, "RAD") == 0) || (strcmp(label, "STATS:MEAN") == 0) ||
                        (strcmp(label, "STATS:STD") == 0) || (strcmp(label, "pi") == 0) ||
                        (strcmp(label, "π") == 0) || (strcmp(label, "Ans") == 0);

      // Special alert buttons
      bool isSpecial = (strcmp(label, "C") == 0) || (strcmp(label, "DEL") == 0) || (strcmp(label, "Home") == 0);

      // Apply styles explicitly with LVGL calls (overrides any global styles)
      if (isDigit) {
        // Blue digit style
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x007BFF), LV_PART_MAIN);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x0056B3), LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
        lv_obj_set_style_radius(btn, 18, LV_PART_MAIN);
        lv_obj_set_style_border_width(btn, 2, LV_PART_MAIN);
        lv_obj_set_style_border_color(btn, lv_color_hex(0x004C99), LV_PART_MAIN);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_28, LV_PART_MAIN);
      }
      else if (isOperator) {
        // Green operator style
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x28A745), LV_PART_MAIN);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x1E7A33), LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
        lv_obj_set_style_radius(btn, 18, LV_PART_MAIN);
        lv_obj_set_style_border_width(btn, 2, LV_PART_MAIN);
        lv_obj_set_style_border_color(btn, lv_color_hex(0x145A2E), LV_PART_MAIN);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_28, LV_PART_MAIN);
      }
      else if (isFunction) {
        // Orange function style
        lv_obj_set_style_bg_color(btn, lv_color_hex(0xFF8C00), LV_PART_MAIN);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0xD46F00), LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
        lv_obj_set_style_radius(btn, 16, LV_PART_MAIN);
        lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, LV_PART_MAIN);
      }
      else if (isSpecial) {
        // Use your existing special style function for C/DEL/Home
        style_func_button(btn);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, LV_PART_MAIN);
      }
      else {
        // Default neutral style (light gray)
        lv_obj_set_style_bg_color(btn, lv_color_hex(0xF5F5F5), LV_PART_MAIN);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0x000000), LV_PART_MAIN);
        lv_obj_set_style_radius(btn, 14, LV_PART_MAIN);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, LV_PART_MAIN);
      }

      // Emphasize "=" visually
      if (strcmp(label, "=") == 0) {
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x0069D9), LV_PART_MAIN);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_28, LV_PART_MAIN);
      }

      // attach handler, pass original token
      lv_obj_add_event_cb(btn, calc_button_handler, LV_EVENT_CLICKED, (void*)label);
    }
  }


  //return calc_screen;
}

void stopAlarm() {
    alarmActive = false;

    // Stop buzzer
    M5.Speaker.end();

    Serial.println("Alarm dismissed");
}

void startAlarm() {
    alarmActive = true;

    // Update UI
    //lv_obj_clean(lv_scr_act());
    lv_obj_t *label = lv_label_create(lv_scr_act());
    lv_label_set_text(label, "⏰ ALARM RINGING!");
    lv_obj_align(label, LV_ALIGN_CENTER, 0, -40);

    lv_obj_t *btn = lv_btn_create(lv_scr_act());
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, 40);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, "Dismiss Alarm");
    lv_obj_center(lbl);
    lv_obj_add_event_cb(btn, [](lv_event_t *e){
        stopAlarm();
        create_clock_page();
    }, LV_EVENT_CLICKED, NULL);
   
    M5.Speaker.begin();
    M5.Speaker.setVolume(255);
    M5.Speaker.tone(4000, 60);
    delay(1000);
    M5.Speaker.tone(2000, 60);
    delay(1000);
}


// ========================
// SPIFFS ALARM STORAGE
// ========================
void saveAlarmToSPIFFS(int hour, int minute) {
    File file = SPIFFS.open("/alarm.txt", FILE_WRITE);
    if (!file) {
        Serial.println("Failed to open alarm.txt for writing");
        return;
    }
    file.printf("%02d:%02d", hour, minute);
    file.close();
    alarmHour = hour;
    alarmMinute = minute;
    alarmTriggered = false;
    Serial.printf("Alarm saved: %02d:%02d\n", hour, minute);
}

void loadAlarmFromSPIFFS() {
    File file = SPIFFS.open("/alarm.txt", FILE_READ);
    if (!file) {
        Serial.println("No alarm saved");
        return;
    }
    String alarmStr = file.readString();
    sscanf(alarmStr.c_str(), "%d:%d", &alarmHour, &alarmMinute);
    file.close();
    Serial.printf("Alarm loaded: %02d:%02d\n", alarmHour, alarmMinute);
}

// ========================
// NTP SYNC
// ========================
void syncNTP() {
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
        Serial.println("Time synchronized with NTP");
    } else {
        Serial.println("Failed to sync NTP");
    }
}

// ========================
// RTC MANUAL SET
// ========================
void setManualTime(int hour, int minute) {
    struct tm t = {};
    t.tm_year = 2025 - 1900; // placeholder year
    t.tm_mon  = 8;           // Sep
    t.tm_mday = 29;
    t.tm_hour = hour;
    t.tm_min  = minute;
    t.tm_sec  = 0;

    time_t epoch = mktime(&t);
    struct timeval now = { .tv_sec = epoch };
    settimeofday(&now, NULL);

    Serial.printf("Manual time set: %02d:%02d\n", hour, minute);
}

// ========================
// CLOCK UPDATE
// ========================
void update_clock_task(lv_timer_t * timer) {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
        // Time
        char buf[16];
        strftime(buf, sizeof(buf), "%H:%M:%S", &timeinfo);
        lv_label_set_text(time_label, buf);

        // Date
        char datebuf[32];
        strftime(datebuf, sizeof(datebuf), "%a %d %b %Y", &timeinfo);
        lv_label_set_text(date_label, datebuf);

        // Alarm check
        // Alarm check
        if (alarmHour >= 0 && alarmMinute >= 0 && !alarmTriggered) {
          if (timeinfo.tm_hour == alarmHour && timeinfo.tm_min == alarmMinute && timeinfo.tm_sec == 0) {
               alarmTriggered = true;
                Serial.println("⏰ ALARM TRIGGERED!");
                startAlarm();
          }
        }
    }
}

// ========================
// UI PAGES
// ========================
void create_clock_page() {
   // lv_obj_t *parent = lv_scr_act();
  if (WiFi.status() == WL_CONNECTED) {
        
    lv_obj_clean(lv_scr_act());//parent);
    lv_obj_t *parent = lv_scr_act();
    time_label = lv_label_create(parent);
    lv_obj_align(time_label, LV_ALIGN_CENTER, 0, -60);
    lv_label_set_text(time_label, "--:--:--");
    lv_obj_set_style_text_font(time_label, &lv_font_montserrat_30, 0);

    date_label = lv_label_create(parent);
    lv_obj_align(date_label, LV_ALIGN_CENTER, 0, -20);
    lv_label_set_text(date_label, "Loading...");
    lv_obj_set_style_text_font(date_label, &lv_font_montserrat_30, 0);

    alarm_label = lv_label_create(parent);
    lv_obj_align(alarm_label, LV_ALIGN_CENTER, 0, 20);
    if (alarmHour >= 0){
        lv_label_set_text_fmt(alarm_label, "Alarm: %02d:%02d", alarmHour, alarmMinute);
        lv_obj_set_style_text_font(alarm_label, &lv_font_montserrat_30, 0);
    }
    else{
        lv_label_set_text(alarm_label, "No alarm set");
        lv_obj_set_style_text_font(alarm_label, &lv_font_montserrat_30, 0);
    }
    // Buttons
    lv_obj_t *btn1 = lv_btn_create(parent);
    lv_obj_align(btn1, LV_ALIGN_CENTER, -100, 80);
    lv_obj_t *label1 = lv_label_create(btn1);
    lv_label_set_text(label1, "Set Time");
    lv_obj_center(label1);
    lv_obj_add_event_cb(btn1, [](lv_event_t *e){
        create_set_time_page();
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn2 = lv_btn_create(parent);
    lv_obj_align(btn2, LV_ALIGN_CENTER, 100, 80);
    lv_obj_t *label2 = lv_label_create(btn2);
    lv_label_set_text(label2, "Set Alarm");
    lv_obj_center(label2);
    lv_obj_add_event_cb(btn2, [](lv_event_t *e){
        create_set_alarm_page();
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t *back_btn = lv_btn_create(parent);
    lv_obj_set_size(back_btn, 100, 80);
    lv_obj_align(back_btn, LV_ALIGN_TOP_MID, -305, 5);
    lv_obj_add_event_cb(back_btn, [](lv_event_t *e){
        create_main_menu();
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t *back_btn_label = lv_label_create(back_btn);//back_btn);
    lv_label_set_text(back_btn_label, "Back");
    lv_obj_center(back_btn_label);

    lv_timer_create(update_clock_task, 1000, NULL);
  }
  else{ 
  create_wifi_page();
  }

}

void create_set_time_page() {
  
    lv_obj_t *time = lv_scr_act();
    //lv_obj_clean(time);

    lv_obj_t *label = lv_label_create(time);
    lv_label_set_text(label, "Set Time (HH:MM)");
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 10);

    hourRoller = lv_roller_create(time);
    lv_roller_set_options(hourRoller, "00\n01\n02\n03\n04\n05\n06\n07\n08\n09\n10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n20\n21\n22\n23", LV_ROLLER_MODE_NORMAL);
    lv_obj_set_size(hourRoller, 60, 100);
    lv_obj_align(hourRoller, LV_ALIGN_CENTER, -50, 0);

    minRoller = lv_roller_create(time);
    lv_roller_set_options(minRoller, "00\n01\n02\n03\n04\n05\n06\n07\n08\n09\n10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n20\n21\n22\n23\n24\n25\n26\n27\n28\n29\n30\n31\n32\n33\n34\n35\n36\n37\n38\n39\n40\n41\n42\n43\n44\n45\n46\n47\n48\n49\n50\n51\n52\n53\n54\n55\n56\n57\n58\n59", LV_ROLLER_MODE_NORMAL);
    lv_obj_set_size(minRoller, 60, 100);
    lv_obj_align(minRoller, LV_ALIGN_CENTER, 50, 0);

    lv_obj_t *save_btn = lv_btn_create(time);
    lv_obj_align(save_btn, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_t *save_btn_lbl = lv_label_create(save_btn);
    lv_label_set_text(save_btn_lbl, "Save");
    lv_obj_center(save_btn_lbl);
    lv_obj_add_event_cb(save_btn, [](lv_event_t *e){
        int hour = lv_roller_get_selected(hourRoller);
        int minute = lv_roller_get_selected(minRoller);
        setManualTime(hour, minute);
        create_clock_page();
    }, LV_EVENT_CLICKED, NULL);

  
}

void create_set_alarm_page() {
    
    lv_obj_t *alarm = lv_scr_act();
    //lv_obj_clean(alarm);

    lv_obj_t *label = lv_label_create(alarm);
    lv_label_set_text(label, "Set Alarm (HH:MM)");
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 10);

    hourRoller = lv_roller_create(alarm);
    lv_roller_set_options(hourRoller, "00\n01\n02\n03\n04\n05\n06\n07\n08\n09\n10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n20\n21\n22\n23", LV_ROLLER_MODE_NORMAL);
    lv_obj_set_size(hourRoller, 60, 100);
    lv_obj_align(hourRoller, LV_ALIGN_CENTER, -50, 0);

    minRoller = lv_roller_create(alarm);
    lv_roller_set_options(minRoller, "00\n01\n02\n03\n04\n05\n06\n07\n08\n09\n10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n20\n21\n22\n23\n24\n25\n26\n27\n28\n29\n30\n31\n32\n33\n34\n35\n36\n37\n38\n39\n40\n41\n42\n43\n44\n45\n46\n47\n48\n49\n50\n51\n52\n53\n54\n55\n56\n57\n58\n59", LV_ROLLER_MODE_NORMAL);
    lv_obj_set_size(minRoller, 60, 100);
    lv_obj_align(minRoller, LV_ALIGN_CENTER, 50, 0);

    lv_obj_t *save_btn = lv_btn_create(alarm);
    lv_obj_align(save_btn, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_t *save_btn_lbl = lv_label_create(save_btn);
    lv_label_set_text(save_btn_lbl, "Save");
    lv_obj_center(save_btn_lbl);
    lv_obj_add_event_cb(save_btn, [](lv_event_t *e){
        int hour = lv_roller_get_selected(hourRoller);
        int minute = lv_roller_get_selected(minRoller);
        saveAlarmToSPIFFS(hour, minute);
        create_clock_page();
    }, LV_EVENT_CLICKED, NULL);
}






/// -------------------------------------------------
// Save/Load
// -------------------------------------------------
void save_chat_history() {
    File file = SPIFFS.open("/chat_history.json", FILE_WRITE);
    if (!file) return;

    DynamicJsonDocument doc(8192);
    JsonArray arr = doc.to<JsonArray>();

    for (auto &msg : chat_history) {
        JsonObject obj = arr.createNestedObject();
        obj["text"] = msg.text;
        obj["is_user"] = msg.is_user;
    }

    serializeJson(doc, file);
    file.close();
}

void clear_chat(lv_event_t *e) {
 
  
 
  File file = SPIFFS.open("/chat_history.json", FILE_WRITE);
 
  if (!file) {
    Serial.println("There was an error opening the file for writing");
    return;
  }
 
  if (file.print("some content")) {
    Serial.println("File was written");
  } else {
    Serial.println("File write failed");
  }
 
  file.close();
 
  Serial.println("\n\n---BEFORE REMOVING---");
 
  SPIFFS.remove("/chat_history.json");
 
  Serial.println("\n\n---AFTER REMOVING---");
 
 
}

void load_chat_history_from_file() {
    if (!SPIFFS.exists("/chat_history.json")) return;
    File file = SPIFFS.open("/chat_history.json", FILE_READ);
    if (!file) return;

    DynamicJsonDocument doc(8192);
    if (deserializeJson(doc, file)) {
        file.close();
        return;
    }

    chat_history.clear();
    for (JsonObject obj : doc.as<JsonArray>()) {
        ChatMessage msg;
        msg.text = obj["text"].as<String>();
        msg.is_user = obj["is_user"];
        chat_history.push_back(msg);
    }

    file.close();
}


 
// -------------------------------------------------
// Chat bubbles
// -------------------------------------------------
void add_message(const char *txt, bool is_user, bool save = true) {
    lv_obj_t *row = lv_obj_create(chat_cont);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_style_pad_all(row, 6, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    if (is_user)
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    else
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *avatar = lv_img_create(row);
    if (is_user) lv_img_set_src(avatar, &user_avatar);
    else lv_img_set_src(avatar, &bani_avatar);
    lv_obj_set_size(avatar, 100, 1000);

    lv_obj_t *bubble = lv_obj_create(row);
    lv_obj_set_width(bubble, lv_pct(70));
    lv_obj_set_height(bubble, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(bubble, 18, 0);
    lv_obj_set_style_pad_all(bubble, 16, 0);
    if (is_user)
        lv_obj_set_style_bg_color(bubble, lv_color_hex(0x007BFF), 0);
    else
        lv_obj_set_style_bg_color(bubble, lv_color_hex(0xE0E0E0), 0);

    lv_obj_t *lbl = lv_label_create(bubble);
    lv_label_set_text(lbl, txt);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(lbl, lv_pct(100));
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(lbl, is_user ? lv_color_white() : lv_color_black(), 0);

    lv_obj_scroll_to_view(row, LV_ANIM_ON);

    if (save) {
        chat_history.push_back({String(txt), is_user});
        save_chat_history();
    }
}

// -------------------------------------------------
// Typing indicator
// -------------------------------------------------
void show_typing_indicator() {
    if (typing_row) return;

    typing_row = lv_obj_create(chat_cont);
    lv_obj_remove_style_all(typing_row);
    lv_obj_set_width(typing_row, lv_pct(100));
    lv_obj_set_style_pad_all(typing_row, 6, 0);
    lv_obj_set_flex_flow(typing_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(typing_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(typing_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *avatar = lv_img_create(typing_row);
    lv_img_set_src(avatar, &bani_avatar);
    lv_obj_set_size(avatar, 60, 60);

    lv_obj_t *bubble = lv_obj_create(typing_row);
    lv_obj_set_width(bubble, lv_pct(30));
    lv_obj_set_height(bubble, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(bubble, 18, 0);
    lv_obj_set_style_pad_all(bubble, 16, 0);
    lv_obj_set_style_bg_color(bubble, lv_color_hex(0xE0E0E0), 0);

    lv_obj_t *lbl = lv_label_create(bubble);
    lv_label_set_text(lbl, "...");
}

// -------------------------------------------------
// Gemini call
// -------------------------------------------------
String call_gemini(const String &prompt) {
    if (WiFi.status() != WL_CONNECTED) return "WiFi not connected.";

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient https;
    // ✅ Step 3: Increase timeout (default is 5s → set to 15s or more)
    //https.setTimeout(15000);
    https.setTimeout(11000); // Set a 5-second timeout for data reception
    https.setConnectTimeout(20000); // Set a 10-second timeout for connection establishment


    // ✅ Step 4: Enable HTTP keep-alive (reduces reconnect overhead)
    https.useHTTP10(false);

    String url = String(GEMINI_ENDPOINT) + "?key=" + GEMINI_API_KEY;
    if (!https.begin(client, url)) {
        return "Failed to connect to Gemini.";
    }

    https.addHeader("Content-Type", "application/json");
    String payload = "{\"contents\":[{\"parts\":[{\"text\":\"" + prompt + "\"}]}]}";

    int httpCode = https.POST(payload);
    if (httpCode <= 0) {
        https.end();
        return "HTTP Error: " + String(https.errorToString(httpCode));
    }

    String response = https.getString();
    https.end();

    DynamicJsonDocument doc(8192);
    if (deserializeJson(doc, response)) {
        return "Parse error.";
    }

    String reply = doc["candidates"][0]["content"]["parts"][0]["text"].as<String>();
    if (reply == "") reply = "No response from Gemini.";
    return reply;
}

// -------------------------------------------------
// Streaming reply (from Gemini)
// -------------------------------------------------
void stream_text_reply(const String &reply) {
    lv_obj_t *row = lv_obj_create(chat_cont);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(row, 6, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *avatar = lv_img_create(row);
    lv_img_set_src(avatar, &bani_avatar);
    lv_obj_set_size(avatar, 60, 60);

    lv_obj_t *bubble = lv_obj_create(row);
    lv_obj_set_width(bubble, lv_pct(70));
    lv_obj_set_height(bubble, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(bubble, 18, 0);
    lv_obj_set_style_pad_all(bubble, 16, 0);
    lv_obj_set_style_bg_color(bubble, lv_color_hex(0xE0E0E0), 0);

    lv_obj_t *lbl = lv_label_create(bubble);
    lv_label_set_text(lbl, "");
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(lbl, lv_pct(100));
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(lbl, lv_color_black(), 0);

    lv_obj_scroll_to_view(row, LV_ANIM_ON);

    struct StreamData {
        lv_obj_t *label;
        String full_text;
        int index;
    };
    StreamData *sd = new StreamData{lbl, reply, 0};
    lv_timer_create([](lv_timer_t *t) {
        StreamData *sd = (StreamData *)t->user_data;
        if (sd->index < sd->full_text.length()) {
            String partial = sd->full_text.substring(0, sd->index + 1);
            lv_label_set_text(sd->label, partial.c_str());
            sd->index++;
        } else {
            lv_timer_del(t);
            delete sd;
        }
    }, 40, sd);
}

// -------------------------------------------------
// Chat events
// -------------------------------------------------
static void send_event_cb(lv_event_t *e) {
    const char *msg = lv_textarea_get_text(ta);
    if (strlen(msg) == 0) return;

    add_message(msg, true);
    lv_textarea_set_text(ta, "");

    show_typing_indicator();

    lv_timer_create([](lv_timer_t *t) {
        lv_obj_del(typing_row);
        typing_row = nullptr;

        // Call Gemini API
        String user_input = chat_history.back().text;
        String reply = call_gemini(user_input);
        stream_text_reply(reply);

        // Save Gemini reply
        chat_history.push_back({reply, false});
        save_chat_history();

        lv_timer_del(t);
    }, 2000, nullptr);
}

static void ta_event_cb(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_FOCUSED) {
        lv_keyboard_set_textarea(kb, ta);
        lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);
        lv_obj_align(ta, LV_ALIGN_BOTTOM_LEFT, 10, -350);
        lv_obj_align(send_btn, LV_ALIGN_BOTTOM_RIGHT, -10, -350);
    } else if (lv_event_get_code(e) == LV_EVENT_DEFOCUSED) {
        lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
        lv_obj_align(ta, LV_ALIGN_BOTTOM_LEFT, 10, -10);
        lv_obj_align(send_btn, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
    }
}

// -------------------------------------------------
// Wi-Fi call back to return to main page
// -------------------------------------------------
static void wifi_connect_cb(lv_event_t *e) {
    String ssid = lv_textarea_get_text(ssid_ta);
    String pass = lv_textarea_get_text(pass_ta);

    
    WiFi.begin(ssid.c_str(), pass.c_str());

    unsigned long wifiStart = millis();
    unsigned long wifiTimeout = 10000; // 10s timeout
    bool wifiConnected = false;
    int tries = 0;
    while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < wifiTimeout) {
        // Let LVGL run while waiting
        lv_timer_handler();  
        //delay(5); // small yield for background tasks, not blocking
        //delay(500);
        //tries++;
        /*Create a spinner*/
        
        
    }

    if (WiFi.status() == WL_CONNECTED) {
        
        lv_obj_clean(lv_scr_act());
        const char* title = "WIFI CONNECTED";
        lv_obj_t *label = lv_label_create(lv_scr_act());
        lv_label_set_text(label, title);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_28, 0);
        lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 40);

        lv_obj_t *btn = lv_btn_create(lv_scr_act());
        lv_obj_set_size(btn, 200, 80);
        lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -40);
        lv_obj_add_event_cb(btn, back_to_menu, LV_EVENT_CLICKED, NULL);

        lv_obj_t *btn_label = lv_label_create(btn);
        lv_label_set_text(btn_label, "Back");
        lv_obj_set_style_text_font(btn_label, &lv_font_montserrat_28, 0);
        lv_obj_center(btn_label);

        syncNTP();

        // Load saved alarm
        loadAlarmFromSPIFFS();
    }
}

void wifi_spinner(lv_event_t *e)
{
    /*Create a spinner*/
    lv_obj_t * spinner = lv_spinner_create(lv_scr_act(), 1000, 60);
    lv_obj_set_size(spinner, 100, 100);
    lv_obj_center(spinner);
}

void create_chat_page(){

  // Switch to chat UI
        lv_obj_clean(lv_scr_act());

        // Chat container
        chat_cont = lv_obj_create(lv_scr_act());
        lv_obj_set_size(chat_cont, SCREEN_WIDTH, SCREEN_HEIGHT - 200);
        lv_obj_set_style_pad_all(chat_cont, 10, 0);
        lv_obj_set_scroll_dir(chat_cont, LV_DIR_VER);
        lv_obj_set_scrollbar_mode(chat_cont, LV_SCROLLBAR_MODE_AUTO);
        lv_obj_set_flex_flow(chat_cont, LV_FLEX_FLOW_COLUMN);

        // Textarea
        ta = lv_textarea_create(lv_scr_act());
        lv_obj_set_size(ta, SCREEN_WIDTH - 120, 80);
        lv_obj_align(ta, LV_ALIGN_BOTTOM_LEFT, 10, -70);
        lv_obj_add_event_cb(ta, ta_event_cb, LV_EVENT_ALL, NULL);
        lv_textarea_set_placeholder_text(ta, "Type a message...");
        lv_obj_set_style_text_font(ta, &lv_font_montserrat_24, 0);

        // Send button
        send_btn = lv_btn_create(lv_scr_act());
        lv_obj_set_size(send_btn, 100, 80);
        lv_obj_align(send_btn, LV_ALIGN_BOTTOM_RIGHT, -10, -70);
        lv_obj_add_event_cb(send_btn, send_event_cb, LV_EVENT_CLICKED, NULL);

        lv_obj_t *icon = lv_img_create(send_btn);
        lv_img_set_src(icon, &send_icon);
        lv_obj_center(icon);

        // Chat keyboard
        kb = lv_keyboard_create(lv_scr_act());
        lv_obj_set_size(kb, SCREEN_WIDTH, 350);
        lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_keyboard_set_textarea(kb, ta);
        lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_text_font(kb, &lv_font_montserrat_36, 0);

        lv_obj_t *back_btn = lv_btn_create(lv_scr_act());
        lv_obj_set_size(back_btn, 100, 80);
        lv_obj_align(back_btn, LV_ALIGN_TOP_MID, -305, 5);
        lv_obj_add_event_cb(back_btn, back_to_menu, LV_EVENT_CLICKED, NULL);

        lv_obj_t *back_btn_label = lv_label_create(back_btn);
        lv_label_set_text(back_btn_label, "Back");
        lv_obj_set_style_text_font(back_btn_label, &lv_font_montserrat_28, 0);
        lv_obj_center(back_btn_label);

        lv_obj_t *clear_btn = lv_btn_create(lv_scr_act());
        lv_obj_set_size(clear_btn, 100, 80);
        lv_obj_align(clear_btn, LV_ALIGN_TOP_MID, -305, 100);
        lv_obj_add_event_cb(clear_btn, clear_chat, LV_EVENT_CLICKED, NULL);
        // Set background color (normal state)
        lv_obj_set_style_bg_color(clear_btn, lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN);

        // Set background color when pressed
        lv_obj_set_style_bg_color(clear_btn, lv_palette_main(LV_PALETTE_BLUE), LV_STATE_PRESSED);


        lv_obj_t *clear_btn_label = lv_label_create(clear_btn);
        lv_label_set_text(clear_btn_label, "clear");
        lv_obj_set_style_text_font(clear_btn_label, &lv_font_montserrat_28, 0);
        lv_obj_center(clear_btn_label);

        // Load history
        load_chat_history_from_file();
        if (chat_history.empty()) {
            add_message("Hello! I'm Bani AI (powered by Gemini).", false);
        } else {
            for (auto &msg : chat_history)
                add_message(msg.text.c_str(), msg.is_user, false);
        }
}

void create_wifi_page() {

    lv_obj_clean(lv_scr_act());

    wifi_page = lv_obj_create(lv_scr_act());
    lv_obj_set_size(wifi_page, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_clear_flag(wifi_page, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *ssid_label = lv_label_create(wifi_page);
    lv_label_set_text(ssid_label, "Wi-Fi SSID:");
    lv_obj_align(ssid_label, LV_ALIGN_TOP_LEFT, 20, 20);
    lv_obj_set_style_text_font(ssid_label, &lv_font_montserrat_24, 0);

    ssid_ta = lv_textarea_create(wifi_page);
    lv_obj_set_size(ssid_ta, SCREEN_WIDTH - 40, 80);
    lv_obj_align(ssid_ta, LV_ALIGN_TOP_LEFT, 20, 60);
    lv_textarea_set_placeholder_text(ssid_ta, "Enter SSID");
    

    lv_obj_t *pass_label = lv_label_create(wifi_page);
    lv_label_set_text(pass_label, "Password:");
    lv_obj_align(pass_label, LV_ALIGN_TOP_LEFT, 20, 160);
    lv_obj_set_style_text_font(pass_label, &lv_font_montserrat_24, 0);

    pass_ta = lv_textarea_create(wifi_page);
    lv_obj_set_size(pass_ta, SCREEN_WIDTH - 40, 80);
    lv_obj_align(pass_ta, LV_ALIGN_TOP_LEFT, 20, 200);
    lv_textarea_set_placeholder_text(pass_ta, "Enter Password");
    lv_textarea_set_password_mode(pass_ta, true);

    lv_obj_t *back_btn = lv_btn_create(wifi_page);
    lv_obj_set_size(back_btn, 200, 80);
    lv_obj_align(back_btn, LV_ALIGN_CENTER, 150, 0);
    lv_obj_add_event_cb(back_btn, back_to_menu, LV_EVENT_CLICKED, NULL);

    lv_obj_t *back_btn_label = lv_label_create(back_btn);
    lv_label_set_text(back_btn_label, "Back");
    lv_obj_set_style_text_font(back_btn_label, &lv_font_montserrat_28, 0);
    lv_obj_center(back_btn_label);

    lv_obj_t *connect_btn = lv_btn_create(wifi_page);
    lv_obj_set_size(connect_btn, 200, 80);
    lv_obj_align(connect_btn, LV_ALIGN_CENTER, -150, 0);
    //lv_obj_add_event_cb(connect_btn, wifi_spinner, LV_EVENT_CLICKED, NULL);
    /*lv_obj_add_event_cb(connect_btn, [](lv_event_t *e) {
        lv_obj_t * spinner = lv_spinner_create(wifi_page, 1000, 60);
        lv_obj_set_size(spinner, 100, 100);
        lv_obj_center(spinner);
        lv_obj_clear_flag(wifi_kb, LV_OBJ_FLAG_HIDDEN);
    }, LV_EVENT_CLICKED, NULL);*/
    lv_obj_add_event_cb(connect_btn, wifi_connect_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *connect_btn_label = lv_label_create(connect_btn);
    lv_label_set_text(connect_btn_label, "Connect");
    lv_obj_set_style_text_font(connect_btn_label, &lv_font_montserrat_28, 0);
    lv_obj_center(connect_btn_label);

    wifi_kb = lv_keyboard_create(wifi_page);
    lv_obj_set_size(wifi_kb, SCREEN_WIDTH, 350);
    lv_obj_align(wifi_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(wifi_kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_text_font(wifi_kb, &lv_font_montserrat_36, 0);

    lv_obj_add_event_cb(ssid_ta, [](lv_event_t *e) {
        lv_keyboard_set_textarea(wifi_kb, ssid_ta);
        lv_obj_clear_flag(wifi_kb, LV_OBJ_FLAG_HIDDEN);
    }, LV_EVENT_FOCUSED, NULL);


    lv_obj_add_event_cb(pass_ta, [](lv_event_t *e) {
        lv_keyboard_set_textarea(wifi_kb, pass_ta);
        lv_obj_clear_flag(wifi_kb, LV_OBJ_FLAG_HIDDEN);
    }, LV_EVENT_FOCUSED, NULL);

    lv_obj_add_event_cb(ssid_ta, [](lv_event_t *e) {
        lv_obj_add_flag(wifi_kb, LV_OBJ_FLAG_HIDDEN);
    }, LV_EVENT_DEFOCUSED, NULL);

    lv_obj_add_event_cb(pass_ta, [](lv_event_t *e) {
        lv_obj_add_flag(wifi_kb, LV_OBJ_FLAG_HIDDEN);
    }, LV_EVENT_DEFOCUSED, NULL);
}



// --------------------
// Generic Page Creator
// --------------------
void create_page(const char *title) {
  lv_obj_clean(lv_scr_act());

  lv_obj_t *label = lv_label_create(lv_scr_act());
  lv_label_set_text(label, title);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_40, 0);
  lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 40);

  lv_obj_t *btn = lv_btn_create(lv_scr_act());
  lv_obj_set_size(btn, 200, 80);
  lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -40);
  lv_obj_add_event_cb(btn, back_to_menu, LV_EVENT_CLICKED, NULL);

  lv_obj_t *btn_label = lv_label_create(btn);
  lv_label_set_text(btn_label, "Back");
  lv_obj_center(btn_label);
}

// --------------------
// Event Handlers for Pages
// --------------------
static void show_wifi_page(lv_event_t *e){ create_wifi_page(); }
static void show_chat_page(lv_event_t *e){ create_chat_page(); }
static void show_clock_page(lv_event_t *e){ create_clock_page(); }
static void show_calculator_page(lv_event_t *e){ create_calculator_page(); }
static void show_note_page(lv_event_t *e){ create_note_page(); }
static void show_settings_page(lv_event_t *e){ create_settings_page(); }
static void show_page7(lv_event_t *e){ create_page("Page 7"); }
static void show_page8(lv_event_t *e){ create_page("Page 8"); }
static void show_page9(lv_event_t *e){ create_page("Page 9"); }
static void show_page10(lv_event_t *e){ create_page("Page 10"); }
static void show_page11(lv_event_t *e){ create_page("Page 11"); }
static void show_page12(lv_event_t *e){ create_page("Page 12"); }

// --------------------
// Main Menu
// --------------------
void create_main_menu() {
  lv_obj_t *cont = lv_obj_create(lv_scr_act());
  lv_obj_set_size(cont, 720, 1280);
  lv_obj_center(cont);

  // 3 cols × 4 rows
  static lv_coord_t col_dsc[] = { LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST };
  static lv_coord_t row_dsc[] = { LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST };
  lv_obj_set_grid_dsc_array(cont, col_dsc, row_dsc);

  // Helper lambda for creating buttons
  auto make_btn = [&](int col, int row, const char *txt, lv_event_cb_t cb) {
    lv_obj_t *btn = lv_btn_create(cont);
    lv_obj_set_size(btn, 200, 120);
    lv_obj_set_grid_cell(btn, LV_GRID_ALIGN_CENTER, col, 1,
                               LV_GRID_ALIGN_CENTER, row, 1);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *label = lv_label_create(btn);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_26, 0);
    lv_label_set_text(label, txt);
    lv_obj_center(label);
  };

  // 12 buttons
  make_btn(0, 0, "WIFI", show_wifi_page);
  make_btn(1, 0, "AI CHAT", show_chat_page);
  make_btn(2, 0, "CLOCK", show_clock_page);

  make_btn(0, 1, "CALCULATOR", show_calculator_page);
  make_btn(1, 1, "NOTES", show_note_page);
  make_btn(2, 1, "SETTINGS", show_settings_page);

  make_btn(0, 2, "Btn 7", show_page7);
  make_btn(1, 2, "Btn 8", show_page8);
  make_btn(2, 2, "Btn 9", show_page9);

  make_btn(0, 3, "Btn 10", show_page10);
  make_btn(1, 3, "Btn 11", show_page11);
  make_btn(2, 3, "Btn 12", show_page12);
}

// --------------------
// Main Menu
// --------------------



static void lv_indev_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data)
{  
     lgfx::touch_point_t tp[3];
     uint8_t touchpad = display.getTouchRaw(tp,3);
       if (touchpad > 0)
       {
          data->state = LV_INDEV_STATE_PR;
          data->point.x = tp[0].x;
          data->point.y = tp[0].y;
          //Serial.printf("X: %d   Y: %d\n", tp[0].x, tp[0].y); //for testing
       }
       else
       {
        data->state = LV_INDEV_STATE_REL;
       }
}

void lv_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p)
{
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);
    display.pushImageDMA(area->x1, area->y1, w, h, (uint16_t *)&color_p->full); 
    lv_disp_flush_ready(disp);
}

// --------------------
// Arduino Setup
// --------------------
void setup() {

  Serial.begin(115200);
  // --- Configure M5 ---
  auto cfg = M5.config();
  
  //cfg.clear_display = false;   // don’t wipe screen
  cfg.output_power  = true;    // keep 5V on for peripherals
  cfg.internal_spk  = true;    // allow speaker
  cfg.external_spk  = 1;       // use ES8388 (codec)
  // note: no cfg.external_display — that union is managed differently now

  

  // If you want to play sound from ATOMIC Speaker, write this
  cfg.external_speaker.atomic_spk     = true;

  // If you want to play sound from ModuleDisplay, write this
  cfg.external_speaker.module_display = true;

  // If you want to play sound from ModuleRCA, write this
    //  cfg.external_speaker.module_rca     = true;

  // If you want to play sound from HAT Speaker, write this
    //  cfg.external_speaker.hat_spk        = true;

  // If you want to play sound from HAT Speaker2, write this
    //  cfg.external_speaker.hat_spk2       = true;

 // M5.begin(cfg);

  { /// I2S Custom configurations are available if you desire.
    auto spk_cfg = M5.Speaker.config();

    if (spk_cfg.use_dac || spk_cfg.buzzer)
    {
    /// Increasing the sample_rate will improve the sound quality instead of increasing the CPU load.
      spk_cfg.sample_rate = 192000; // default:64000 (64kHz)  e.g. 48000 , 50000 , 80000 , 96000 , 100000 , 128000 , 144000 , 192000 , 200000
    }
/*
    spk_cfg.pin_data_out=8;
    spk_cfg.pin_bck=7;
    spk_cfg.pin_ws=10;     // LRCK

    /// use single gpio buzzer, ( need only pin_data_out )
    spk_cfg.buzzer = false;

    /// use DAC speaker, ( need only pin_data_out ) ( only GPIO_NUM_25 or GPIO_NUM_26 )
    spk_cfg.use_dac = false;
    // spk_cfg.dac_zero_level = 64; // for Unit buzzer with DAC.

    /// Volume Multiplier
    spk_cfg.magnification = 16;
//*/
    M5.Speaker.config(spk_cfg);
  }
  M5.Speaker.begin();
  
  display.begin();
  display.setRotation(0);   // portrait: 720 × 1280
 // M5.begin();


  lv_init();
  WiFi.setPins(SDIO2_CLK, SDIO2_CMD, SDIO2_D0, SDIO2_D1, SDIO2_D2, SDIO2_D3, SDIO2_RST);

    if (!SPIFFS.begin(true)) {
        Serial.println("SPIFFS Mount Failed");
        init_settings();   // Load saved settings and apply them
    }
    // Ensure /notes directory exists
    if (!SPIFFS.exists("/notes")) {
        SPIFFS.mkdir("/notes");
    }

  lv_disp_draw_buf_init(&draw_buf, buf, NULL, 720 * 60);

  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = 720;
  disp_drv.ver_res = 1280;
  disp_drv.flush_cb = [](lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
    display.pushImage(area->x1, area->y1,
                      area->x2 - area->x1 + 1,
                      area->y2 - area->y1 + 1,
                      (uint16_t *)&color_p->full);
    lv_disp_flush_ready(disp);
  };
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);
  
  /*Initialize touch*/
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = lv_indev_read;
    lv_indev_drv_register(&indev_drv);  

  // Tick handler
  //lv_tick_set_cb(lv_tick_handler);

  create_main_menu();
}

void loop() {
  lv_timer_handler();
  delay(5);



}
