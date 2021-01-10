#include <stdio.h>
#include <string.h>

#include "../startup.h"
#include "../rpi-mailbox-interface.h"

#include "screen_modes.h"

unsigned char* fb = NULL;


// Palette for 8bpp modes
#define NUM_COLOURS 256
static uint32_t colour_table[NUM_COLOURS];
static int sync_palette;

static void update_palette(int offset, int num_colours) {
   RPI_PropertyInit();
   RPI_PropertyAddTag(TAG_SET_PALETTE, offset, num_colours, colour_table);
#ifdef USE_DOORBELL
   // Call the Check version as doorbell and mailboxes are seperate
   //LOG_INFO("Calling TAG_SET_PALETTE\r\n");
   RPI_PropertyProcess();
   //rpi_mailbox_property_t *buf = RPI_PropertyGet(TAG_SET_PALETTE);
   //if (buf) {
   //   LOG_INFO("TAG_SET_PALETTE returned %08x\r\n", buf->data.buffer_32[0]);
   //} else {
   //   LOG_INFO("TAG_SET_PALETTE returned ?\r\n");
   //}
#else
   // Call the NoCheck version as our mailbox FIQ handler swallows the response
   RPI_PropertyProcessNoCheck();
#endif
}

static void init_colour_table(screen_mode_t *screen) {
   // Colour  0 = Black
   // Colour  1 = Dark Red
   // Colour  2 = Dark Green
   // Colour  3 = Dark Yellow
   // Colour  4 = Dark Blue
   // Colour  5 = Dark Magenta
   // Colour  6 = Dark Cyan
   // Colour  7 = Dark White
   // Colour  8 = Dark Black
   // Colour  9 = Red
   // Colour 10 = Green
   // Colour 11 = Yellow
   // Colour 12 = Blue
   // Colour 13 = Magenta
   // Colour 14 = Cyan
   // Colour 15 = White
   // Colour 16-255 Black
   for (int i = 0; i < 16; i++) {
      int intensity = (i & 8) ? 255 : 127;
      int b = (i & 4) ? intensity : 0;
      int g = (i & 2) ? intensity : 0;
      int r = (i & 1) ? intensity : 0;
      if (i == 8) {
         r = g = b = 63;
      }
      screen->set_colour(screen, i, r, g, b);
   }
   for (int i = 16; i < 256; i++) {
      screen->set_colour(screen, i, 0, 0, 0);
   }
}

static void init_screen(screen_mode_t *screen) {

    rpi_mailbox_property_t *mp;

    /* Initialise a framebuffer... */
    RPI_PropertyInit();
    RPI_PropertyAddTag(TAG_ALLOCATE_BUFFER);
    RPI_PropertyAddTag(TAG_SET_PHYSICAL_SIZE, screen->width, screen->height );
    RPI_PropertyAddTag(TAG_SET_VIRTUAL_SIZE,  screen->width, screen->height * 2 ); // TODO: FIX ME (remove the * 2)
    RPI_PropertyAddTag(TAG_SET_DEPTH,         screen->bpp );
    RPI_PropertyAddTag(TAG_GET_PITCH );
    RPI_PropertyAddTag(TAG_GET_PHYSICAL_SIZE );
    RPI_PropertyAddTag(TAG_GET_DEPTH );
    RPI_PropertyProcess();

    if( ( mp = RPI_PropertyGet( TAG_GET_PHYSICAL_SIZE ) ) )
    {
        int width = mp->data.buffer_32[0];
        int height = mp->data.buffer_32[1];

        printf( "Initialised Framebuffer: %dx%d ", width, height );
    }

    if( ( mp = RPI_PropertyGet( TAG_GET_DEPTH ) ) )
    {
        int bpp = mp->data.buffer_32[0];
        printf( "%dbpp\r\n", bpp );
    }

    if( ( mp = RPI_PropertyGet( TAG_GET_PITCH ) ) )
    {
        screen->pitch = mp->data.buffer_32[0];
        printf( "Pitch: %d bytes\r\n", screen->pitch );
    }

    if( ( mp = RPI_PropertyGet( TAG_ALLOCATE_BUFFER ) ) )
    {
        fb = (unsigned char*)mp->data.buffer_32[0];
        printf( "Framebuffer address: %8.8X\r\n", (unsigned int)fb );
    }

    // On the Pi 2/3 the mailbox returns the address with bits 31..30 set, which is wrong
    fb = (unsigned char *)(((unsigned int) fb) & 0x3fffffff);


    /* Copy default colour table */
    sync_palette = 0;
    init_colour_table(screen);
    sync_palette = 1;

    /* Update the palette (only in 8-bpp modes) */
    if (screen->bpp == 8) {
       update_palette(0, NUM_COLOURS);
    }

    /* Clear the screen to the background colour */
    screen->clear(screen, 0);

};


static void clear_screen(screen_mode_t *screen, int value) {
   // TODO: This needs to take acount of the size and bit depth
   memset((void *)fb, value, screen->height * screen->pitch);
}

static void scroll_screen(screen_mode_t *screen) {
   // TODO: Fix Hard Coded Font Size
   _fast_scroll(fb, fb + 12 * screen->pitch, (screen->height - 12) * screen->pitch);
   _fast_clear(fb + (screen->height - 12) * screen->pitch, 0, 12 * screen->pitch);
}



static void set_colour_8bpp(screen_mode_t *screen, unsigned int index, int r, int g, int b) {
   colour_table[index] = 0xFF000000 | ((b & 0xFF) << 16) | ((g & 0xFF) << 8) | (r & 0xFF);
   if (sync_palette) {
      update_palette(index, 1);
   }
}

static void set_colour_16bpp(screen_mode_t *screen, unsigned int index, int r, int g, int b) {
   // 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0
   // R4 R3 R2 R1 R0 G5 G4 G3 G2 G1 G0 B4 B3 B2 B1 B0
   colour_table[index] = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | ((b & 0xF8) >> 3);
}

static void set_colour_32bpp(screen_mode_t *screen, unsigned int index, int r, int g, int b) {
   colour_table[index] = 0xFF000000 | ((r & 0xFF) << 16) | ((g & 0xFF) << 8) | (b & 0xFF);
}

static uint32_t get_colour_8bpp(screen_mode_t *screen, unsigned int index) {
   return index;
}

static uint32_t get_colour_16bpp(screen_mode_t *screen, unsigned int index) {
   return colour_table[index];
}

static uint32_t get_colour_32bpp(screen_mode_t *screen, unsigned int index) {
   return colour_table[index];
}

static void set_pixel_8bpp(screen_mode_t *screen, int x, int y, pixel_t value) {
   uint8_t *fbptr = (uint8_t *)(fb + (screen->height - y - 1) * screen->pitch + x);
   *fbptr = value;

}

static void set_pixel_16bpp(screen_mode_t *screen, int x, int y, pixel_t value) {
   uint16_t *fbptr = (uint16_t *)(fb + (screen->height - y - 1) * screen->pitch + x * 2);
   *fbptr = value;

}

static void set_pixel_32bpp(screen_mode_t *screen, int x, int y, pixel_t value) {
   uint32_t *fbptr = (uint32_t *)(fb + (screen->height - y - 1) * screen->pitch + x * 4);
   *fbptr = value;
}

static pixel_t get_pixel_8bpp(screen_mode_t *screen, int x, int y) {
   uint8_t *fbptr = (uint8_t *)(fb + (screen->height - y - 1) * screen->pitch + x);
   return *fbptr;
}

static pixel_t get_pixel_16bpp(screen_mode_t *screen, int x, int y) {
   uint16_t *fbptr = (uint16_t *)(fb + (screen->height - y - 1) * screen->pitch + x * 2);
   return *fbptr;
}

static pixel_t get_pixel_32bpp(screen_mode_t *screen, int x, int y) {
   uint32_t *fbptr = (uint32_t *)(fb + (screen->height - y - 1) * screen->pitch + x * 4);
   return *fbptr;
}

static screen_mode_t screen_modes[] = {
   {
      .mode_num      = 0,
      .width         = 640,
      .height        = 480,
      .bpp           = 8,
      .num_colours   = 0x100,
      .text_width    = 80,
      .text_height   = 40,
      .init          = init_screen,
      .clear         = clear_screen,
      .scroll        = scroll_screen,
      .set_colour    = set_colour_8bpp,
      .get_colour    = get_colour_8bpp,
      .set_pixel     = set_pixel_8bpp,
      .get_pixel     = get_pixel_8bpp
   },
   {
      .mode_num      = 1,
      .width         = 640,
      .height        = 480,
      .bpp           = 16,
      .num_colours   = 0x10000,
      .text_width    = 80,
      .text_height   = 40,
      .init          = init_screen,
      .clear         = clear_screen,
      .scroll        = scroll_screen,
      .set_colour    = set_colour_16bpp,
      .get_colour    = get_colour_16bpp,
      .set_pixel     = set_pixel_16bpp,
      .get_pixel     = get_pixel_16bpp
   },
   {
      .mode_num      = 2,
      .width         = 640,
      .height        = 480,
      .bpp           = 32,
      .num_colours   = 0x100000,
      .text_width    = 80,
      .text_height   = 40,
      .init          = init_screen,
      .clear         = clear_screen,
      .scroll        = scroll_screen,
      .set_colour    = set_colour_32bpp,
      .get_colour    = get_colour_32bpp,
      .set_pixel     = set_pixel_32bpp,
      .get_pixel     = get_pixel_32bpp
   },
   {
      .mode_num     = -1,
   }
};

screen_mode_t *get_screen_mode(int mode_num) {
   screen_mode_t *sm = screen_modes;
   while (sm->mode_num >= 0) {
      if (sm->mode_num == mode_num) {
         return sm;
      }
      sm++;
   }
   return NULL;
}
