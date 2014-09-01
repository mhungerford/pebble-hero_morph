#include <pebble.h>

// PNG support
// Includes Grayscale support for 1 bit (B&W)
#include "upng.h"

#define MAX(A,B) ((A>B) ? A : B)
#define MIN(A,B) ((A<B) ? A : B)

Window* my_window = NULL;

//Time Display
char time_string[] = "00:00";  // Make this longer to show AM/PM
TextLayer* time_text_layer = NULL;

//Date Display
char date_string[16];
TextLayer* date_text_layer = NULL;

//Image Display
BitmapLayer* bitmap_layer = NULL;
GBitmap* gbitmap_ptr = NULL;

//Work around using window buffer with GColorClear to leave contents,
//must occasionally paint it black, use a layer to do it (TextLayer)
TextLayer* upper_box_layer = NULL;
TextLayer* lower_box_layer = NULL;

bool animating = false;

static const char *const dname[7] =
{"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};


//Image indexes and count
int max_images = 0; //automatically detected
#define RESOURCE_ID_IMAGE_OFFSET 2
int image_index = RESOURCE_ID_IMAGE_OFFSET + 1; //Images start after IMAGE_OFFSET


void flip_byte(uint8_t* byteval) {
  uint8_t v = *byteval;
  uint8_t r = v; // r will be reversed bits of v; first get LSB of v
  int s = 7; // extra shift needed at end

  for (v >>= 1; v; v >>= 1) {   
    r <<= 1;
    r |= v & 1;
    s--;
  }
  r <<= s; // shift when v's highest bits are zero
  *byteval = r;
}

static bool gbitmap_from_bitmap(
    GBitmap* gbitmap, const uint8_t* bitmap_buffer, 
    int width, int height, int x_offset, int y_offset) {

  // Limit PNG to screen size
  width = MIN(width, 144);
  height = MIN(height, 168);

  // Copy width and height to GBitmap
  gbitmap->bounds.size.w = width;
  gbitmap->bounds.size.h = height;


  printf("xoffset:%d yoffset:%d", x_offset, y_offset);

  // Set the image offset for bitmap layer
  GRect frame;
  frame.origin.x = -74 + ((width + 1) / 2) + x_offset;
  frame.origin.y = -74 + ((height + 1) / 2) + y_offset;
  frame.size.w = 144;//width;
  frame.size.h = 144;//height;
  layer_set_frame(bitmap_layer_get_layer(bitmap_layer), frame);
  
  // GBitmap needs to be word aligned per line (bytes)
  gbitmap->row_size_bytes = ((width + 31) / 32 ) * 4;
  //Allocate new gbitmap array
  //add 1 byte, for non-aligned widths 
  gbitmap->addr = malloc(height * gbitmap->row_size_bytes + 1);
  if (gbitmap->addr == NULL) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "malloc gbitmap->addr %d bytes failed", 
      height * gbitmap->row_size_bytes);
    return false;
  }
  memset(gbitmap->addr, 0, height * gbitmap->row_size_bytes);


  for(int y = 0; y < height; y++) {
    memcpy(
      &(((uint8_t*)gbitmap->addr)[y * gbitmap->row_size_bytes]), 
      &(bitmap_buffer[y * ((width + 7) / 8)]), 
      (width + 7) / 8);
  }

  // GBitmap pixels are most-significant bit, so need to flip each byte.
  for(int i = 0; i < gbitmap->row_size_bytes * height; i++){
    flip_byte(&((uint8_t*)gbitmap->addr)[i]);
  }

  return true;
}

static bool load_png_resource(uint32_t resource_id) {
  ResHandle rHdl = resource_get_handle(resource_id);
  int png_raw_size = resource_size(rHdl);
  upng_t* upng = NULL;
  bool retval = true;

  //Allocate gbitmap if necessary
  if (gbitmap_ptr == NULL) {
    gbitmap_ptr = malloc(sizeof(GBitmap));
  }

  // Free the last bitmap
  if (gbitmap_ptr->addr) {
    free(gbitmap_ptr->addr);
  }
  
  uint8_t* png_raw_buffer = malloc(png_raw_size); //freed by upng impl
  if (png_raw_buffer == NULL) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "malloc png resource buffer failed");
  }
  resource_load(rHdl, png_raw_buffer, png_raw_size);
  upng = upng_new_from_bytes(png_raw_buffer, png_raw_size);
  if (upng == NULL) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "UPNG malloc error"); 
  }
  if (upng_get_error(upng) != UPNG_EOK) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "UPNG Loaded:%d line:%d", 
      upng_get_error(upng), upng_get_error_line(upng));
  }
  if (upng_decode(upng) != UPNG_EOK) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "UPNG Decode:%d line:%d", 
      upng_get_error(upng), upng_get_error_line(upng));
  }

  retval = gbitmap_from_bitmap(gbitmap_ptr, upng_get_buffer(upng),
    upng_get_width(upng), upng_get_height(upng), 
    upng_get_x_offset(upng), upng_get_y_offset(upng));
  //APP_LOG(APP_LOG_LEVEL_DEBUG, "converted to gbitmap");

  // Free the png, no longer needed
  upng_free(upng);
  upng = NULL;

  return retval;
}

static void increment_image(void){
  image_index = ( image_index >= max_images + RESOURCE_ID_IMAGE_OFFSET) ? 
    (RESOURCE_ID_IMAGE_OFFSET + 1) : (image_index + 1);
}

static void timer_handler(void* data) {
  increment_image();
  window_set_background_color(my_window, GColorClear);

  if (load_png_resource(image_index)) {
    layer_mark_dirty(bitmap_layer_get_layer(bitmap_layer));
  }
  if (image_index != RESOURCE_ID_IMAGE_OFFSET + 1) {
    animating = true;
    app_timer_register(125, timer_handler, data);
  } else {
    animating = false;
    light_enable(false);
  }
}

void tap_handler(AccelAxisType axis, int32_t direction){
  if(!animating) {
    light_enable(true);
    timer_handler(NULL);
  }
}

void tick_handler(struct tm *tick_time, TimeUnits units_changed){
  if (units_changed & DAY_UNIT) {
    time_t current_time = time(NULL);
    struct tm *current_tm = localtime(&current_time);
    //strftime(date_string, sizeof(date_string), "%a %-m/%d", localtime(&current_time));
    snprintf(date_string, sizeof(date_string), "%s %d/%d", 
      dname[current_tm->tm_wday], current_tm->tm_mon + 1, current_tm->tm_mday);
    layer_mark_dirty(text_layer_get_layer(date_text_layer));
  }

  clock_copy_time_string(time_string,sizeof(time_string));
  layer_mark_dirty(text_layer_get_layer(time_text_layer));
  layer_mark_dirty(text_layer_get_layer(upper_box_layer));
  layer_mark_dirty(text_layer_get_layer(lower_box_layer));
}


static void window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);
  
  //Add layers from back to front (background first)
  
  
  //Create bitmap layer for background image
  bitmap_layer = bitmap_layer_create(bounds);

  //Set bitmap layer alignment for png offset usage
  bitmap_layer_set_alignment(bitmap_layer, GAlignCenter);

  //Add bitmap_layer to window layer
  layer_add_child(window_layer, bitmap_layer_get_layer(bitmap_layer));


  //add box background behind text
  upper_box_layer = text_layer_create(GRect(0,0,144,12));
  text_layer_set_background_color(upper_box_layer, GColorBlack);
  layer_add_child(window_layer, text_layer_get_layer(upper_box_layer));

  //add box background behind text
  lower_box_layer = text_layer_create(GRect(0,142,144,26));
  text_layer_set_background_color(lower_box_layer, GColorBlack);
  layer_add_child(window_layer, text_layer_get_layer(lower_box_layer));

  //Load initial bitmap image
  load_png_resource(image_index);
  bitmap_layer_set_bitmap(bitmap_layer, gbitmap_ptr);


  //Setup the time display
  time_text_layer = text_layer_create(GRect(0, 138, 144, 30));
  text_layer_set_text(time_text_layer, time_string);
	text_layer_set_font(time_text_layer, fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_BOXY_TEXT_30)));
  text_layer_set_text_alignment(time_text_layer, GTextAlignmentCenter);
  text_layer_set_background_color(time_text_layer, GColorClear);
  text_layer_set_text_color(time_text_layer, GColorWhite);
  clock_copy_time_string(time_string,sizeof(time_string));
  
  //Add clock text second
  layer_add_child(window_layer, text_layer_get_layer(time_text_layer));

  //Setup the date display
  time_t current_time = time(NULL);
  struct tm *current_tm = localtime(&current_time);
  //strftime(date_string, sizeof(date_string), "%a %_m/%d", localtime(&current_time));
  snprintf(date_string, sizeof(date_string), "%s %d/%d", 
    dname[current_tm->tm_wday], current_tm->tm_mon + 1, current_tm->tm_mday);

  //date text
  date_text_layer = text_layer_create(GRect(0, -4, 144, 18));
  text_layer_set_text(date_text_layer, date_string);
	text_layer_set_font(date_text_layer, fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_BOXY_TEXT_18)));
  text_layer_set_text_alignment(date_text_layer, GTextAlignmentCenter);
  text_layer_set_background_color(date_text_layer, GColorClear);
  text_layer_set_text_color(date_text_layer, GColorWhite);

  layer_add_child(window_layer, text_layer_get_layer(date_text_layer));
  


  //Setup tick time handler
  tick_timer_service_subscribe((MINUTE_UNIT), tick_handler);
  
  //setup timer to start animation
  light_enable(true);
  app_timer_register(125, timer_handler, NULL);

  //Setup tap service
  accel_tap_service_subscribe(tap_handler);

}

static void window_unload(Window *window) {
  free(gbitmap_ptr);
}



void handle_init(void) {
  //Discover how many images from base index (offset+1)
  while (resource_get_handle(RESOURCE_ID_IMAGE_OFFSET + 1 + max_images)) {
    max_images++;
  }
  
  my_window = window_create();
  window_set_fullscreen(my_window, true);
  window_set_background_color(my_window, GColorBlack);
  window_set_window_handlers(my_window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });

  window_stack_push(my_window, false/*animated*/);
}

void handle_deinit(void) {
    bitmap_layer_destroy(bitmap_layer);
	  window_destroy(my_window);
}


int main(void) {
	  handle_init();
	  app_event_loop();
	  handle_deinit();
}
