#include <cstddef>
#include <iostream>

#include <sys/ipc.h>
#include <sys/shm.h>
#include <xcb/xcb.h>
#include <xcb/xproto.h>
#include <xcb/xcb_image.h>
#include <xcb/shm.h>
#include <xcb/xcb_icccm.h>

#include "XCB_framebuffer_window.h"

unsigned int Framebuffer_window::instances;
xcb_connection_t * Framebuffer_window::connection;
xcb_screen_t * Framebuffer_window::screen;

Framebuffer_window::Framebuffer_window(unsigned int width, unsigned int height, char * name, unsigned int name_len, struct window_props * window_properties)
{
    if (instances == 0)
    {
        connection = xcb_connect(NULL, NULL);
        if (xcb_connection_has_error(connection))
        {
            std::cerr << "Error opening X connection.\n";
            window_properties->error_status = -1;
            goto FAIL;
        }

        const xcb_setup_t * const setup = xcb_get_setup(connection);
        xcb_screen_iterator_t screen_iter = xcb_setup_roots_iterator(setup);
        // For now simply assign the first screen from the screen iterator.
        screen = screen_iter.data;

        // Check that the shared memory segment is available
        // xcb_shm_id is a struct provided by the xcb/shm.h header which contains the name string and the global id for the shm extension.
        // The function xcb_get_extension_data returns a query reply struct with info about the presence or absence of the extension.
        const struct xcb_query_extension_reply_t * shm_extension_data = xcb_get_extension_data(connection, &xcb_shm_id);
        if ((shm_extension_data == NULL) || (shm_extension_data->present == 0))
        {
            std::cerr <<"Error: XCB SHM extension does not seem to be present.\n";
            window_properties->error_status = -1;
            goto FAIL;
        }
    }

    ++ instances;

    // xcb_image_create_native requires fewer parameters than xcb_image_create.
    // The bit depth from the selected screen is used (screen->root_depth),
    // Data pointer and data size (bytes) cannot be provided yet as we don't know bits per pixel in advance in this case.
    framebuffer_image = xcb_image_create_native(connection, width, height, XCB_IMAGE_FORMAT_Z_PIXMAP, screen->root_depth, NULL, 0, NULL);

    window_properties->bit_depth = framebuffer_image->depth;
    window_properties->bits_per_pixel = framebuffer_image->bpp;
    window_properties->stride = framebuffer_image->stride;

    // IPC_CREAT ensures a new segment is created. IPC_EXCL ensures failure if the segment already exists.
    // last four digits specify user, group and global permissions.
    shm_id = shmget(IPC_PRIVATE, framebuffer_image->stride * framebuffer_image->height, IPC_CREAT | IPC_EXCL | 0600);
    if (shm_id < 0)
    {
        std::cerr << "Error: Failed to acquire shared memory segment.\n";
        window_properties->error_status = -1;
        goto FAIL;
    }
    // attach the shared memory to the process address space and assign image data to point at it.
    // a shmaddr of NULL yields attachment at the first available address.
    framebuffer_image->data = (uint8_t *)shmat(shm_id, NULL, 0);
    framebuffer_ptr = framebuffer_image->data;

    // request that XCB also attach the shared memory segment.
    xcb_shm_segment = xcb_generate_id(connection);
    shared_cookie = xcb_shm_attach_checked(connection, xcb_shm_segment, shm_id, 0);
    shared_error_ptr = xcb_request_check(connection , shared_cookie);
    // TODO implement error handling here.

    // Creating and showing a window.
    window = xcb_generate_id(connection);
    window_value_list[0] = screen->black_pixel;
    window_value_list[1] = XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_STRUCTURE_NOTIFY;
    xcb_create_window(
        connection,
        XCB_COPY_FROM_PARENT,
        window,
        screen->root,
        0,
        0,
        width,
        height,
        0,
        XCB_WINDOW_CLASS_INPUT_OUTPUT,
        screen->root_visual,
        window_mask,
        window_value_list);

    // Set values for the suggested dimension and placement limits for the window.
    xcb_icccm_size_hints_set_position(&window_manager_size_hints, 0, 0, 0);
    xcb_icccm_size_hints_set_max_size(&window_manager_size_hints, width, height);
    xcb_icccm_size_hints_set_min_size(&window_manager_size_hints, width, height);
    // Change the normal hints window manager property to the newly created size hints.
    xcb_icccm_set_wm_size_hints(connection, window, XCB_ATOM_WM_NORMAL_HINTS, &window_manager_size_hints);

    // Set window name property
    xcb_change_property(
        connection,
        XCB_PROP_MODE_REPLACE,
        window,
        XCB_ATOM_WM_NAME,
        XCB_ATOM_STRING,
        8,
        name_len,
        name);

    // I have no idea how or why the following works.
    // It changes some sort or property such that an XCB_CLIENT_MESSAGE event is triggered when the window is closed.
    // Some aspect of the content of the message can be checked against a "WM_DELETE_WINDOW" atom to see if the window
    // manager has closed the window. If so we can gracefully quit out of the program.
    close_reply_ptr = xcb_intern_atom_reply(
        connection,
        xcb_intern_atom(connection, 0, 16, "WM_DELETE_WINDOW"),
        NULL);
    protocol_reply_ptr = xcb_intern_atom_reply(
        connection, 
        xcb_intern_atom(connection, 1, 12, "WM_PROTOCOLS"), 
        NULL);
    xcb_change_property(
        connection,
        XCB_PROP_MODE_REPLACE, 
        window, 
        protocol_reply_ptr->atom, 
        XCB_ATOM_ATOM, 
        32, 
        1, 
        &(close_reply_ptr->atom));

    // Map and display window
    xcb_map_window(connection, window);
    xcb_flush(connection);

    // Drawing graphics in a window requires a graphics context 
    graphics_context = xcb_generate_id(connection);
    xcb_create_gc(
        connection,
        graphics_context,
        window,
        XCB_GC_FOREGROUND,
        &(screen->black_pixel));

    FAIL:{}
}

void Framebuffer_window::re_draw()
{
    xcb_shm_put_image(
        connection,
        window,
        graphics_context,
        framebuffer_image->width,
        framebuffer_image->height,
        0,
        0,
        framebuffer_image->width,
        framebuffer_image->height,
        0,
        0,
        framebuffer_image->depth,
        framebuffer_image->format,
        0,
        xcb_shm_segment,
        0);
    xcb_flush(connection);
}

int Framebuffer_window::handle_events()
{
    event_ptr = xcb_poll_for_event(connection);
    if (event_ptr == NULL) return 0;
    switch (event_ptr->response_type & 0x7F)
    {
        case XCB_EXPOSE:
        re_draw();
        break;

        case XCB_CLIENT_MESSAGE:
        if (((xcb_client_message_event_t *)event_ptr)->data.data32[0] == close_reply_ptr->atom) return -1;

        default:
        break;
    }

    return 0;
}

void Framebuffer_window::hide()
{
    xcb_unmap_window(connection, window);
}

void Framebuffer_window::show()
{
    xcb_map_window(connection, window);
}

Framebuffer_window::~Framebuffer_window()
{
    -- instances;

    // Detach shared memory and destroy x image no longer needed.
    xcb_shm_detach(connection, xcb_shm_segment);
    shmdt(framebuffer_image->data);
    shmctl(shm_id, IPC_RMID, 0);
    xcb_image_destroy(framebuffer_image);
    free(framebuffer_image);

    free(protocol_reply_ptr);
    free(close_reply_ptr);

    xcb_free_gc(connection, graphics_context);
    xcb_destroy_window(connection, window);
    free(event_ptr);

    if (instances == 0)
    {
        xcb_disconnect(connection);
        free(connection);
        free(screen);
    }
}


