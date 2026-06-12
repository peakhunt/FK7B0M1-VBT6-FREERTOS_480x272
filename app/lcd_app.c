#include <stdint.h>
#include <stdbool.h>
#include "app_stm32.h"
#include "FreeRTOS.h"
#include "cmsis_os2.h"
#include "task.h"
#include "main.h"
#include "lcd_app.h"

extern LTDC_HandleTypeDef hltdc;

static uint8_t* const frame_buffer0 = (uint8_t*)0x24000000; 
static uint8_t* const frame_buffer1 = (uint8_t*)0x2403FC00; 

// Resolution constants
#define LCD_WIDTH   480
#define LCD_HEIGHT  272

#define SWITCH_INTERVAL   1000

static uint8_t visible_layer = 0;
static osSemaphoreId_t ltdc_vsync_sem;

#include <stdint.h>

// Ensure your global macros are set to your new panel metrics:
// #define LCD_WIDTH  480
// #define LCD_HEIGHT 272

static inline
void write_pixel_rgb565(uint8_t* buffer, uint32_t x, uint32_t y, uint8_t r, uint8_t g, uint8_t b)
{
  // RGB565 means 2 bytes per pixel. Use LCD_WIDTH dynamically instead of hardcoded 800.
  uint32_t index = ((y * LCD_WIDTH) + x) * 2; 
  
  // Pack 8-bit R, G, B components into 16-bit word (Red: 5-bits, Green: 6-bits, Blue: 5-bits)
  uint16_t color_565 = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);

  // Cast buffer to uint16_t pointer for high performance direct native memory tracking
  uint16_t* pixel_ptr = (uint16_t*)(&buffer[index]);
  *pixel_ptr = color_565;
}

static void
generate_tv_bars_pattern(uint8_t* buffer)
{
  uint32_t c;
  uint8_t r, g, b;

  for (uint32_t y = 0; y < LCD_HEIGHT; y++)
  {
    for (uint32_t x = 0; x < LCD_WIDTH; x++)
    {
      // 1. Draw the 20-pixel Outer Calibration Corners & White/Blue Border Lines
      if (x < 20 && y < 20)
        write_pixel_rgb565(buffer, x, y, 255, 255, 255); // White Top-Left Corner
      else if (x < 20 && (y > 20 && y < LCD_HEIGHT - 20))
        write_pixel_rgb565(buffer, x, y, 0, 0, 255);     // Blue Left Margin
      else if (y < 20 && (x > 20 && x < LCD_WIDTH - 20))
        write_pixel_rgb565(buffer, x, y, 0, 255, 0);     // Green Top Margin
      else if (x > LCD_WIDTH - 20 && (y > 20 && y < LCD_HEIGHT - 20))
        write_pixel_rgb565(buffer, x, y, 255, 0, 0);     // Red Right Margin
      else if (y > LCD_HEIGHT - 20 && (x > 20 && x < LCD_WIDTH - 20))
        write_pixel_rgb565(buffer, x, y, 255, 255, 0);   // Yellow Bottom Margin
      else if (x == 20 || x == LCD_WIDTH - 20 || y == 20 || y == LCD_HEIGHT - 20)
        write_pixel_rgb565(buffer, x, y, 255, 255, 255); // Inner White Border Framing

      // 2. Draw the Corner-to-Corner Diagonal Geometry Crosshairs
      else if (x == y || (LCD_WIDTH - x) == (LCD_HEIGHT - y))
        write_pixel_rgb565(buffer, x, y, 255, 0, 255);   // Magenta Diagonal (\)
      else if ((LCD_WIDTH - x) == y || x == (LCD_HEIGHT - y))
        write_pixel_rgb565(buffer, x, y, 0, 255, 255);   // Cyan Diagonal (/)

      // 3. Render the Core Multi-Channel RGB Grayscale Ramp Sweeps
      else if (x > 20 && y > 20 && x < LCD_WIDTH - 20 && y < LCD_HEIGHT - 20)
      {
        int slice = (x * 3) / LCD_WIDTH;
        r = 0; g = 0; b = 0;

        if (slice == 0)      b = (y % 256); // Left 33%: Cascading Blue Ramp gradient
        else if (slice == 1) g = (y % 256); // Middle 33%: Cascading Green Ramp gradient
        else                 r = (y % 256); // Right 33%: Cascading Red Ramp gradient

        c = (r << 16) | (g << 8) | (b << 0);
        write_pixel_rgb565(buffer, x, y, (c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
      }

      // 4. Fill dead zones
      else
      {
        write_pixel_rgb565(buffer, x, y, 0, 0, 0);
      }
    }
  }
}

static void
generate_tv_grid_pattern(uint8_t* buffer)
{
  // 1. Color declarations matching the strict 75% intensity SMPTE color space standard
  const uint8_t bars_75[7][3] = {
    {192, 192, 192}, // 0: 75% Gray / White
    {192, 192,   0}, // 1: Yellow
    {  0, 192, 192}, // 2: Cyan
    {  0, 192,   0}, // 3: Green
    {192,   0, 192}, // 4: Magenta
    {192,   0,   0}, // 5: Red
    {  0,   0, 192}  // 6: Blue
  };

  const uint8_t bars_100[7][3] = {
    {  0,   0, 192}, // 0: Blue
    {  0,   0,   0}, // 1: Pure Black
    {192,   0, 192}, // 2: Magenta
    {  0,   0,   0}, // 3: Pure Black
    {  0, 192, 192}, // 4: Cyan
    {  0,   0,   0}, // 5: Pure Black
    {192, 192, 192}  // 6: White
  };

  // Dynamically calculate structural widths using your current LCD_WIDTH configuration
  uint32_t bar_width = LCD_WIDTH / 7; 

  // Dynamically divide standard SMPTE vertical layout slices based on LCD_HEIGHT proportional bounds
  uint32_t tier1_height = (LCD_HEIGHT * 67) / 100; // Top 67%
  uint32_t tier2_height = (LCD_HEIGHT * 75) / 100; // Next 8% (accumulative 75%)

  for (uint32_t y = 0; y < LCD_HEIGHT; y++)
  {
    for (uint32_t x = 0; x < LCD_WIDTH; x++)
    {
      uint32_t bar_index = x / bar_width;
      if (bar_index > 6) bar_index = 6; // Boundary protection clip

      // =======================================================================
      // TIER 1: Top 67% -> Standard 75% Chroma Bars
      // =======================================================================
      if (y < tier1_height)
      {
        write_pixel_rgb565(buffer, x, y, bars_75[bar_index][0], bars_75[bar_index][1], bars_75[bar_index][2]);
      }
      // =======================================================================
      // TIER 2: Middle 8% -> Reverse Phase Alignment Bars
      // =======================================================================
      else if (y >= tier1_height && y < tier2_height)
      {
        write_pixel_rgb565(buffer, x, y, bars_100[bar_index][0], bars_100[bar_index][1], bars_100[bar_index][2]);
      }
      // =======================================================================
      // TIER 3: Bottom 25% -> Engineering Reference & PLUGE
      // =======================================================================
      else
      {
        // Block A (Sub-bar 0): I-Signal (Dark Cyan/Navy block)
        if (x < (uint32_t)(bar_width * 1.25)) 
        {
          write_pixel_rgb565(buffer, x, y, 0, 33, 76); 
        }
        // Block B (Sub-bar 1): Super-White 100% Canvas Peak Reference
        else if (x >= (uint32_t)(bar_width * 1.25) && x < (uint32_t)(bar_width * 2.5)) 
        {
          write_pixel_rgb565(buffer, x, y, 255, 255, 255); 
        }
        // Block C (Sub-bar 2): Q-Signal (Deep Purple/Violet block)
        else if (x >= (uint32_t)(bar_width * 2.5) && x < (uint32_t)(bar_width * 3.75)) 
        {
          write_pixel_rgb565(buffer, x, y, 56, 0, 110); 
        }
        // Block D (Sub-bar 3 & 4): Pure Baseline Broadcast Black (0 IRE)
        else if (x >= (uint32_t)(bar_width * 3.75) && x < (uint32_t)(bar_width * 5.0)) 
        {
          write_pixel_rgb565(buffer, x, y, 19, 19, 19); 
        }
        // Block E (Sub-bar 5): PLUGE Test Pulses (-2 IRE, 0 IRE, +2 IRE calibration stripes)
        else if (x >= (uint32_t)(bar_width * 5.0) && x < (uint32_t)(bar_width * 6.0)) 
        {
          uint32_t pluge_offset = x - (uint32_t)(bar_width * 5);
          
          // Scaled width allocations for PLUGE stripes matching the smaller 480 res matrix
          uint32_t stripe_width = bar_width / 3;
          
          if (pluge_offset < stripe_width)                 write_pixel_rgb565(buffer, x, y, 11, 11, 11); // Super-Black (-2 IRE)
          else if (pluge_offset < (stripe_width * 2))       write_pixel_rgb565(buffer, x, y, 19, 19, 19); // Reference Black (0 IRE)
          else                                              write_pixel_rgb565(buffer, x, y, 27, 27, 27); // Light-Black (+2 IRE)
        }
        // Block F (Sub-bar 6): Remaining trailing Edge Base Black Block
        else 
        {
          write_pixel_rgb565(buffer, x, y, 19, 19, 19);
        }
      }
    }
  }
}

// This is an official HAL override function
void HAL_LTDC_ReloadEventCallback(LTDC_HandleTypeDef *hltdc)
{
  // Verify the callback came from the correct LTDC instance
  if (hltdc->Instance == LTDC)
  {
    // Release the semaphore to wake up the blocked set_visible_layer thread
    osSemaphoreRelease(ltdc_vsync_sem);
  }
}

static void
set_visible_layer(LTDC_HandleTypeDef *phltdc, uint8_t layer)
{
  uint32_t target_address = (layer == 0) ? (uint32_t)frame_buffer0 : (uint32_t)frame_buffer1;

  // 1. Configure the new address in the shadow registers
  HAL_LTDC_SetAddress_NoReload(phltdc, target_address, LTDC_LAYER_1);

  // 2. Request a reload on the next Vertical Blanking period (VSYNC) to prevent tearing
  HAL_LTDC_Reload(phltdc, LTDC_RELOAD_VERTICAL_BLANKING);

  osSemaphoreAcquire(ltdc_vsync_sem, pdMS_TO_TICKS(30));
}

static void
lcd_task(void* argument)
{
  generate_tv_grid_pattern(frame_buffer1);
  generate_tv_bars_pattern(frame_buffer0);

  visible_layer = 0;
  set_visible_layer(&hltdc, visible_layer);

  while(true)
  {
    osDelay(SWITCH_INTERVAL);
    visible_layer = visible_layer == 0 ? 1 : 0;
    set_visible_layer(&hltdc, visible_layer);
  }
}

void
lcd_app_init(void)
{
  const osThreadAttr_t task_attr = {
    .name = "lcd_task",
    .stack_size = 1024,
    .priority = (osPriority_t) osPriorityHigh,
  };

  ltdc_vsync_sem = osSemaphoreNew(1, 0, NULL);
  osThreadNew(lcd_task, NULL, &task_attr);
}
