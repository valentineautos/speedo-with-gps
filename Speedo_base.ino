#include <Arduino.h>
#include <ESP_Panel_Library.h>
#include <lvgl.h>
#include "lvgl_port_v8.h"
#include "fonts/roboto_semibold_28.h"
#include "fonts/roboto_semibold_80.h"
#include "fonts/roboto_bold_120.h"
#include "fonts/roboto_regular_40.h"
#include <WiFi.h>
#include <esp_now.h>

// ESPNow structures
typedef struct struct_speed {
  int id;
  int satellites;
  int speed_mph;
}struct_speed;

struct_speed speedData;


// Global components
lv_obj_t *scr; // Screen background
lv_obj_t *rpm_arc; // Moving arc
lv_obj_t *rpm_label; // Digital RPM
lv_obj_t *mph_label; // Digital MPH
lv_obj_t *gear_label; // Digital gear
lv_obj_t *satellite_icon; // Icon showing satellites

// Global styles
static lv_style_t style_unit_text;
static lv_style_t icon_style;

// Color palette
lv_color_t palette_amber = LV_COLOR_MAKE(250, 140, 0);
lv_color_t palette_black = LV_COLOR_MAKE(0, 0, 0);
lv_color_t palette_red = LV_COLOR_MAKE(255, 0, 0);
lv_color_t palette_white = LV_COLOR_MAKE(255, 255, 255);

// Attributes
volatile bool data_ready = false;
hw_timer_t* timer = nullptr;

int rpm = 0;
int current_speed = 0;
int target_speed = 0;
unsigned long previous_ticks = 0;
const int stepper_time = 30; 

const int diameter = 480;
const int radius = diameter / 2;
const int rpm_max = 8000;
const int rpm_redline = 6600;
const int rpm_arc_width = 24;
const int rpm_line_width = 4;
const int rpm_arc_size = diameter - (rpm_line_width * 12);
const int arc_start = 120;
const int arc_end = 344;
const int rpm_freq = 500;
const float marker_gap = ((float)arc_end - arc_start) / (rpm_max / 1000);

uint32_t last_update_time = 0;
const uint32_t timeout_ms = 5000;

// Styles
void make_styles(void) {
    lv_style_init(&style_unit_text);
    lv_style_set_text_font(&style_unit_text, &roboto_semibold_28);
    lv_style_set_text_color(&style_unit_text, palette_amber);

    lv_style_init(&icon_style);
    lv_style_set_text_font(&icon_style, &lv_font_montserrat_40);
}

// ESPNow received
void OnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
  memcpy(&speedData, incomingData, sizeof(speedData));

  Serial.print("Recieved speed: ");
  Serial.println(speedData.speed_mph);
  Serial.print("Recieved satelittes: ");
  Serial.println(speedData.satellites);

  //target_speed = speedData.speed_mph;

  last_update_time = lv_tick_get();
  data_ready = true;
}

void esp_now_timeout() {
    uint32_t current_time = lv_tick_get();
    if (current_time - last_update_time > timeout_ms) {
        Serial.print("GPS Offline (Timeout)");
        lv_obj_clear_flag(satellite_icon, LV_OBJ_FLAG_HIDDEN);
        speedData.speed_mph = 0;
        speedData.satellites = 0;
        last_update_time = current_time;
    }
}

// Update values
void arc_anim_cb(void *arc, int32_t value) {
  lv_arc_set_value((lv_obj_t *)arc, value);
}

void update_speed(int new_speed) {
  char mph_text[4];
  itoa(new_speed, mph_text, 10);
  lv_label_set_text(mph_label, mph_text);

  Serial.print("Speed updated to :");
  Serial.println(new_speed);

  current_speed = new_speed;
}

void update_gear(char new_gear[2]) {
    lv_label_set_text(gear_label, new_gear); 
}

void update_rpm(int new_rpm) {
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, rpm_arc);
    lv_anim_set_values(&a, lv_arc_get_value(rpm_arc), new_rpm);
    lv_anim_set_time(&a, rpm_freq);
    lv_anim_set_exec_cb(&a, arc_anim_cb);
    lv_anim_set_path_cb(&a, lv_anim_path_linear); 
    lv_anim_start(&a);

    if (new_rpm >= rpm_redline) {
      lv_obj_set_style_arc_color(rpm_arc, palette_red, LV_PART_INDICATOR);
    } else {
      lv_obj_set_style_arc_color(rpm_arc, palette_amber, LV_PART_INDICATOR);
    }

    static char rpm_text[8];
    snprintf(rpm_text, sizeof(rpm_text), "%d", new_rpm);
    lv_label_set_text(rpm_label, rpm_text);

    rpm = new_rpm;
}


// Maker UI Elements

void make_rpm_arc(void) {
    rpm_arc = lv_arc_create(scr);

    lv_obj_set_size(rpm_arc, rpm_arc_size, rpm_arc_size);
    lv_obj_align(rpm_arc, LV_ALIGN_CENTER, 0, 0);
    lv_arc_set_bg_angles(rpm_arc, arc_start + 2, arc_end - 1);

    lv_obj_set_style_arc_color(rpm_arc, palette_black,  LV_PART_MAIN);
    lv_obj_set_style_arc_color(rpm_arc, palette_amber,  LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(rpm_arc, false, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(rpm_arc, rpm_arc_width, LV_PART_INDICATOR);

    lv_obj_remove_style(rpm_arc, NULL, LV_PART_KNOB); 

    lv_arc_set_range(rpm_arc, 0, rpm_max);
    lv_arc_set_value(rpm_arc, rpm_max);
}

void position_markers(lv_obj_t *marker, int position) {
    lv_obj_set_size(marker, rpm_arc_size + (rpm_line_width * 4), rpm_arc_size + (rpm_line_width * 4));
    lv_obj_align(marker, LV_ALIGN_CENTER, 0, 0);
    
    lv_obj_set_style_arc_width(marker, rpm_arc_width + (rpm_line_width * 4), LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(marker, false, LV_PART_MAIN);
    lv_obj_set_style_arc_color(marker, palette_white, LV_PART_MAIN);

    lv_arc_set_bg_angles(marker, arc_start + (position * marker_gap), arc_start + (position * marker_gap) + 1);

    lv_obj_remove_style(marker, NULL, LV_PART_KNOB); 
    lv_obj_remove_style(marker, NULL, LV_PART_INDICATOR);    
}

void make_rpm_border(void) {
    lv_obj_t *rpm_arc_border = lv_arc_create(scr);

    lv_obj_set_size(rpm_arc_border, rpm_arc_size - (rpm_arc_width * 2) - (rpm_line_width * 2), rpm_arc_size - (rpm_arc_width * 2) - (rpm_line_width * 2));
    lv_obj_align(rpm_arc_border, LV_ALIGN_CENTER, 0, 0);
    lv_arc_set_bg_angles(rpm_arc_border, arc_start, arc_end);

    lv_obj_set_style_arc_width(rpm_arc_border, rpm_line_width, LV_PART_MAIN); 
    lv_obj_set_style_arc_rounded(rpm_arc_border, false, LV_PART_MAIN);
    lv_obj_set_style_arc_color(rpm_arc_border, palette_white, LV_PART_MAIN);

    lv_obj_remove_style(rpm_arc_border, NULL, LV_PART_KNOB); 
    lv_obj_remove_style(rpm_arc_border, NULL, LV_PART_INDICATOR);

    int rpm_marker_count = (rpm_max / 1000) + 1;
    lv_obj_t *rpm_markers[rpm_marker_count];

    for (int i = 0; i < rpm_marker_count; i++) {
        rpm_markers[i] = lv_arc_create(scr);
        position_markers(rpm_markers[i], i);
    }
}

void make_rpm_redline(void) {
    lv_obj_t *rpm_redline_marker = lv_arc_create(scr);

    lv_obj_set_size(rpm_redline_marker, diameter, diameter);
    lv_obj_align(rpm_redline_marker, LV_ALIGN_CENTER, 0, 0);
    lv_arc_set_bg_angles(rpm_redline_marker, arc_start + ((arc_end - arc_start) * ((float)rpm_redline / rpm_max)), arc_end);

    lv_obj_set_style_arc_color(rpm_redline_marker, palette_red, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(rpm_redline_marker, false, LV_PART_MAIN);
    lv_obj_set_style_arc_width(rpm_redline_marker, rpm_line_width * 2, LV_PART_MAIN);
    
    lv_obj_remove_style(rpm_redline_marker, NULL, LV_PART_KNOB);
    lv_obj_remove_style(rpm_redline_marker, NULL, LV_PART_INDICATOR); 
}

void make_rpm_digital(void) {
    static lv_style_t style_rpm_text;
    lv_style_init(&style_rpm_text);
    lv_style_set_text_font(&style_rpm_text, &roboto_regular_40);
    lv_style_set_text_color(&style_rpm_text, palette_white);

    rpm_label = lv_label_create(scr);
    lv_label_set_text(rpm_label, "2500");
    lv_obj_add_style(rpm_label, &style_rpm_text, 0);
    lv_obj_align(rpm_label, LV_ALIGN_LEFT_MID, diameter - 120, -10);

    lv_obj_t *rpm_unit_label = lv_label_create(scr);
    lv_label_set_text(rpm_unit_label, "rpm");
    lv_obj_add_style(rpm_unit_label, &style_unit_text, 0);
    lv_obj_align(rpm_unit_label, LV_ALIGN_LEFT_MID, diameter - 120, 20);

}

void make_speed_digital(void) {
    static lv_style_t style_speed_text;
    lv_style_init(&style_speed_text);
    lv_style_set_text_font(&style_speed_text, &roboto_bold_120);
    lv_style_set_text_color(&style_speed_text, palette_white);

    mph_label = lv_label_create(scr);
    lv_label_set_text(mph_label, "0");
    lv_obj_add_style(mph_label, &style_speed_text, 0);
    lv_obj_align(mph_label, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *mph_unit_label = lv_label_create(scr);
    lv_label_set_text(mph_unit_label, "mph");
    lv_obj_add_style(mph_unit_label, &style_unit_text, 0);
    lv_obj_align(mph_unit_label, LV_ALIGN_CENTER, 40, 60);
}

void make_gear(void) { 
    static lv_color_t canvas_buf[LV_CANVAS_BUF_SIZE_TRUE_COLOR(120, 120)];
    lv_obj_t *canvas = lv_canvas_create(scr);
    lv_canvas_set_buffer(canvas, canvas_buf, 120, 120, LV_IMG_CF_TRUE_COLOR);
    lv_obj_align(canvas, LV_ALIGN_CENTER, 0, radius - 60);
    lv_obj_set_style_border_width(canvas, 0, 0);
    lv_obj_set_style_bg_opa(canvas, LV_OPA_0, 0);

    lv_point_t points[] = {
        {15, 15},  
        {105, 15},
        {120, 120},
        {0, 120},
    };

    lv_draw_rect_dsc_t gear_bg_style;
    lv_draw_rect_dsc_init(&gear_bg_style);
    gear_bg_style.bg_color = lv_color_make(45, 45, 45);
    gear_bg_style.bg_opa = LV_OPA_COVER; 
    gear_bg_style.border_width = 0; 

   lv_canvas_draw_polygon(canvas, points, 4, &gear_bg_style);

    gear_label = lv_label_create(scr);

    static lv_style_t style_gear_text;
    lv_style_init(&style_gear_text);
    lv_style_set_text_font(&style_gear_text, &roboto_semibold_80);
    lv_style_set_text_color(&style_gear_text, palette_amber);

    lv_label_set_text(gear_label, "N");
    lv_obj_add_style(gear_label, &style_gear_text, 0);
    lv_obj_align(gear_label, LV_ALIGN_CENTER, 0, radius - 50);
}

void make_notifications(void) {
    satellite_icon = lv_label_create(scr);
    lv_label_set_text(satellite_icon, LV_SYMBOL_WIFI);
    lv_obj_add_style(satellite_icon, &icon_style, 0);
    lv_obj_set_style_text_color(satellite_icon, palette_red, 0);
    lv_obj_align(satellite_icon, LV_ALIGN_LEFT_MID, diameter - 95, 75);
}

void scr_init() {
    ESP_Panel *panel = new ESP_Panel();
    panel->init();

    #if LVGL_PORT_AVOID_TEAR
        // When avoid tearing function is enabled, configure the RGB bus according to the LVGL configuration
        ESP_PanelBus_RGB *rgb_bus = static_cast<ESP_PanelBus_RGB *>(panel->getLcd()->getBus());
        rgb_bus->configRgbFrameBufferNumber(LVGL_PORT_DISP_BUFFER_NUM);
        rgb_bus->configRgbBounceBufferSize(LVGL_PORT_RGB_BOUNCE_BUFFER_SIZE);
    #endif
    panel->begin();

    lvgl_port_init(panel->getLcd(), panel->getTouch());
}

void wifi_init() {
  WiFi.mode(WIFI_STA);

  // Init ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }
  
  // Once ESPNow is successfully Init, we will register for recv CB to
  // get recv packer info
  esp_now_register_recv_cb(esp_now_recv_cb_t(OnDataRecv));
}

void IRAM_ATTR onTimer() {
    lv_tick_inc(1); // Increment LVGL's tick by 1ms
}

void setup()
{
  Serial.begin(115200);

  scr_init();
  wifi_init();

  scr = lv_scr_act();    
  lv_obj_set_style_bg_color(scr, palette_black, 0);

  // Common styles
  make_styles();

  // RPM stuff
  make_rpm_arc();
  make_rpm_border();
  make_rpm_redline();
  make_rpm_digital();

  // Fast boi stuff
  make_speed_digital();
  make_gear();

  // Notifications
  make_notifications();

  // Setting values with update functions
  update_rpm(3500);
  update_speed(current_speed);
  update_gear("2");

   // Setup the timer
  const uint32_t lv_tick_frequency = 1000; // 1 kHz = 1ms period

  timer = timerBegin(lv_tick_frequency); // Configure the timer with 1kHz frequency
  if (!timer) {
      Serial.println("Failed to configure timer!");
      while (1); // Halt execution on failure
  }

  timerAttachInterrupt(timer, &onTimer); // Attach the ISR to the timer
  Serial.println("Timer initialized for LVGL tick!");

  last_update_time = lv_tick_get();
}

int counter = 0;

void loop(){
    lv_timer_handler();
    esp_now_timeout();

  //   unsigned long current_ticks = lv_tick_get();
  //   int speed_difference = abs(target_speed - current_speed);

  //   if (speed_difference > 0) {
  //     if (current_ticks - previous_ticks >= stepper_time ) {
  //         previous_ticks = current_ticks;

  //         if (current_speed < target_speed) {
  //             current_speed++;
  //         } else if (current_speed > target_speed) {
  //             current_speed--;
  //         }

  //         update_speed(current_speed);
  //     }
  //  } 

    if (data_ready) {
      if (speedData.satellites > 0) {
        Serial.print("GPS Online");
        lv_obj_add_flag(satellite_icon, LV_OBJ_FLAG_HIDDEN);
      } else {
        Serial.print("GPS Offline");
        lv_obj_clear_flag(satellite_icon, LV_OBJ_FLAG_HIDDEN);
      }
      update_speed(speedData.speed_mph);
      data_ready = false;
    }
}