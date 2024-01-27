#include <linux/fb.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <math.h>
#include <X11/Xlib.h>

typedef struct coordinate {
	int32_t x;
	int32_t y;
} coord_t;

typedef struct colour {
	uint8_t r;
	uint8_t g;
	uint8_t b;
	uint8_t a;
} colour_t;

typedef struct window_info {
	Display * display;
	Window window;
	Window root_window;
	GC graphics_context;
	XImage * image;
	union {
		uint32_t * framebuffer_ptr;
		char * image_data;
	};
	int32_t x_pos;
	int32_t y_pos;
	uint32_t x_res;
	uint32_t y_res;
} window_t;

struct psf_header {
	uint32_t magic; //bytes that identify a psf file
	uint32_t version; //file format version
	uint32_t offset; //offset of the data from the start of the file
	uint32_t flags; //indicate whether a unicode table is present
	uint32_t num_glyphs; //number of character glyphs
	uint32_t bytes_per_glyph; //number of bytes used for each glyph
	uint32_t height; //height of the glyphs in pixels
	uint32_t width; //width of the glyphs in pixels
};

typedef struct PC_screen_font {
	union {
		struct psf_header header;
		uint8_t read_in_buffer[32];
	};
	uint8_t * font_glyph_ptr;
} psf_t;

window_t create_window(coord_t position, coord_t size, uint32_t border_width) {
	window_t win;
	win.x_res = size.x;
	win.y_res = size.y;
	win.display = XOpenDisplay(NULL);
	int32_t screen_num = DefaultScreen(win.display);
	win.root_window = RootWindow(win.display, screen_num);
	win.graphics_context = DefaultGC(win.display, screen_num);
	win.window = XCreateSimpleWindow(win.display, win.root_window, position.x, position.y, size.x, size.y, border_width, 0, 0);
	Visual * visual = DefaultVisual(win.display, screen_num);
	int32_t depth = DefaultDepth(win.display, screen_num);
	win.framebuffer_ptr = malloc(win.x_res * win.y_res * 4);
	win.image = XCreateImage(win.display, visual, depth, ZPixmap, 0, win.image_data, size.x, size.y, 32, 0);
	win.image->data = win.image_data;
	XMapWindow(win.display, win.window);
	return win;
}

void refresh_window(window_t *win) {
	uint32_t old_x_res = win->x_res;
	uint32_t old_y_res = win->y_res;
	uint32_t null = 0;
	uint32_t null2 = 0;
	XGetGeometry(win->display, win->window, &(win->root_window), &(win->x_pos), &(win->y_pos), &(win->x_res), &(win->y_res), &null, &null2);
	if ((win->x_res != old_x_res) || (win->y_res != old_y_res)) {
		win->image_data = realloc(win->image_data, win->x_res * win->y_res * 4);
		win->image->data = win->image_data;
		win->image->width = win->x_res;
		win->image->height = win->y_res;
		win->image->bytes_per_line = win->x_res * 4;
		printf("%d, %d\n", win->image->width, win->image->height);
		//printf("%lu\n", sizeof(*win->framebuffer_ptr));
	}
	//int32_t screen_num = DefaultScreen(win->display);
	//Visual * visual = DefaultVisual(win->display, screen_num);
	//int32_t depth = DefaultDepth(win->display, screen_num);
	//win->image = XCreateImage(win->display, visual, depth, ZPixmap, 0, win->image_data, win->x_res, win->y_res, 32, 0);
	//XInitImage(win->image);
	XPutImage(win->display, win->window, win->graphics_context, win->image, 0, 0, 0, 0, win->x_res, win->y_res);
	XFlush(win->display);
}

void set_pixel(window_t *win, coord_t coordinate, colour_t colour) {
	uint32_t pixel_value = 0;
	pixel_value = (colour.r << 16);
	pixel_value |= (colour.g << 8);
	pixel_value |= (colour.b << 0);
	pixel_value |= (colour.a << 24);
	int64_t location_offset = (coordinate.y * win->x_res) + coordinate.x;
	*(win->framebuffer_ptr + location_offset) = pixel_value;
}

colour_t number_to_colour(int32_t value, uint8_t alpha) {
	colour_t colour;

	value %= 1280;

	switch ((uint8_t)(value / 255)) {
		case 0:
			colour.r = (value - (255 * 0));
			colour.g = 0;
			colour.b = 0;
			break;
		case 1:
			colour.r = 255;
			colour.g = (value - (255 * 1));
			colour.b = 0;
			break;
		case 2:
			colour.r = (255 - (value - (255 * 2)));
			colour.g = 255;
			colour.b = 0;
			break;
		case 3:
			colour.r = 0;
			colour.g = 255;
			colour.b = (value - (255 * 3));
			break;
		case 4:
			colour.r = (value - (255 * 4));
			colour.g = (255 - (value - (255 * 4)));
			colour.b = 255;
			break;
		default:
			colour.r = 0;
			colour.g = 0;
			colour.b = 0;
			break;
	}

	colour.a = alpha;
	return colour;
}

void init_font(psf_t *font_info_ptr, char *font_file_path) {
	int32_t font_file_descriptor = open(font_file_path, O_RDONLY);
	pread(font_file_descriptor, font_info_ptr->read_in_buffer, 32, 0);
	uint32_t bytes = font_info_ptr->header.bytes_per_glyph * font_info_ptr->header.num_glyphs;
	uint8_t * glyphs = calloc(bytes, 1);
	pread(font_file_descriptor, glyphs, bytes, font_info_ptr->header.offset);
	font_info_ptr->font_glyph_ptr = glyphs;
}

void display_char(char ch, psf_t *font_info_ptr, window_t *win, coord_t pos, colour_t colour) {
	uint8_t row_bits;
	uint32_t glyph_offset = ch * font_info_ptr->header.height;
	uint32_t row_offset = 0;
	uint32_t x_start = pos.x;
	uint32_t i;
	for (row_offset = 0; row_offset < font_info_ptr->header.height; row_offset++) {
		row_bits = *(font_info_ptr->font_glyph_ptr + (glyph_offset + row_offset));
		for (i = 0; i < 8; i++) {
			if (row_bits & 0x80) {
				set_pixel(win, pos, colour);
			}
			row_bits <<= 1;
			pos.x ++;
		}
		pos.x = x_start;
		pos.y ++;
	}
}

void display_string(char string[], psf_t *font_info_ptr, window_t *win, coord_t pos, colour_t colour) {
	uint32_t i = 0;
	while (string[i]) {
		display_char(string[i], font_info_ptr, win, pos, colour);
		pos.x += font_info_ptr->header.width;
		i++;
	}
}

int main(void) {

	coord_t win_pos, win_size;
	win_pos.x = 100;
	win_pos.y = 100;
	win_size.x = 768;
	win_size.y = 768;
	window_t main_window = create_window(win_pos, win_size, 0);

	psf_t font;
	init_font(&font, "./ter-powerline-v16b.psf");

	int32_t x = 0;
 	int32_t y = 0;
	uint32_t i = 0;
	colour_t pixel_colour;
	//colour_t text_colour;
	coord_t screen_coord;
	//coord_t text_pos;

	//text_pos.x = 512;
	//text_pos.y = 512;
	//text_colour.r = 0x00;
	//text_colour.g = 0xFF;
	//text_colour.b = 0x00;
	//text_colour.a = 0xFF;

	uint32_t index = 0;
	char message[] = "Hello World! ";

	while (1) {
		for (x = 0; x < main_window.x_res; x ++) {
			for (y = 0; y < main_window.y_res; y ++) {
				screen_coord.x = x;
				screen_coord.y = y;
				pixel_colour = number_to_colour((x + i) + (y + i), 0xFF);
				set_pixel(&main_window, screen_coord, pixel_colour);
				//display_char(message[index % (sizeof(message) - 1)], &font, &main_window, screen_coord, pixel_colour);
			}
			index ++;
		}
		index = 0;
		//display_string("Hello World!", &font, &main_window, text_pos, text_colour);
		i++;
		refresh_window(&main_window);
	}
	return 0;
}
