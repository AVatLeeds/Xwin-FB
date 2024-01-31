#ifndef XCB_FRAMEBUFFER_WINDOW_H
#define XCB_FRAMEBUFFER_WINDOW_H

#include <cstdint>

#include <xcb/xcb.h>
#include <xcb/xcb_image.h>
#include <xcb/xcb_icccm.h>
#include <xcb/xproto.h>

struct window_props
{
    int error_status;
    unsigned int bit_depth;
    unsigned int bits_per_pixel;
    unsigned int stride;
};

class Framebuffer_window
{
    public:
    Framebuffer_window(unsigned int width, unsigned int height, char * name, unsigned int name_len, struct window_props * window_properties);
    ~Framebuffer_window();

    void re_draw();
    int handle_events();
    void hide();
    void show();

    uint8_t * framebuffer_ptr;

    private:
    xcb_void_cookie_t shared_cookie;
    xcb_generic_error_t * shared_error_ptr;

    static unsigned int instances;
    static xcb_connection_t * connection;
    static xcb_screen_t * screen;

    xcb_image_t * framebuffer_image;
    int shm_id;
    xcb_shm_seg_t xcb_shm_segment;

    xcb_window_t window;
    const unsigned int window_mask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;
    unsigned int window_value_list[2];

    xcb_size_hints_t window_manager_size_hints;

    xcb_intern_atom_reply_t * protocol_reply_ptr;
    xcb_intern_atom_reply_t * close_reply_ptr;

    xcb_gcontext_t graphics_context;

    xcb_generic_event_t * event_ptr;

};

#endif