#include <stddef.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/shm.h>
#include <xcb/xcb.h>
#include <xcb/xproto.h>
#include <xcb/xcb_image.h>
#include <xcb/shm.h>
#include <xcb/xcb_icccm.h>

#define WINDOW_WIDTH 1024
#define WINDOW_HEIGHT 768

int main(int argc, char * argv[])
{
    xcb_connection_t * connection = xcb_connect(NULL, NULL);
    if (xcb_connection_has_error(connection))
    {
        printf("Error opening X connection.\n");
        return -1;
    }

    const xcb_setup_t * const setup = xcb_get_setup(connection);

    xcb_screen_iterator_t screen_iter = xcb_setup_roots_iterator(setup);
    xcb_screen_t * screen = screen_iter.data; // for now simply select the first screen. There seems to be only one anyway
    printf("Screens available:\n");
    for(; screen_iter.rem; xcb_screen_next(&screen_iter))
    {
        printf("%d: Height = %d pixels. Width = %d pixels. Root depth = %d bits. Root = %d.\n", screen_iter.index, screen_iter.data->height_in_pixels, screen_iter.data->width_in_pixels, screen_iter.data->root_depth, screen_iter.data->root);
    }
    putchar('\n');

    // Check that the shared memory segment is available
    // xcb_shm_id is a struct provided by the xcb/shm.h header which contains the name string and the global id for the shm extension.
    // The function xcb_get_extension_data returns a query reply struct with info about the presence or absence of the extension.
    const struct xcb_query_extension_reply_t * shm_extension_data = xcb_get_extension_data(connection, &xcb_shm_id);
    if ((shm_extension_data == NULL) || (shm_extension_data->present == 0))
    {
        printf("Error: XCB SHM extension does not seem to be present.\n");
        return -1;
    }

    // Shared memory image must now be created. This is an XCB image with memory back by an shm segment

    // xcb_image_create_native requires fewer parameters than xcb_image_create.
    // The bit depth from the selected screen is used (screen->root_depth),
    // Data pointer and data size (bytes) cannot be provided yet as we don't know bits per pixel in advance in this case.
    xcb_image_t * image = xcb_image_create_native(connection, WINDOW_WIDTH, WINDOW_HEIGHT, XCB_IMAGE_FORMAT_Z_PIXMAP, screen->root_depth, NULL, 0, NULL);

    // Determine the amount of memory required for the created image.
    const size_t image_segment_size = image->stride * image->height;
    // IPC_CREAT ensures a new segment is created. IPC_EXCL ensures faliure if the segment already exists.
    // last four digits specify user, group and global permissions.
    int shm_id = shmget(IPC_PRIVATE, image_segment_size, IPC_CREAT | IPC_EXCL | 0600);
    if (shm_id < 0)
    {
        printf("Error: Failed to acquire shared memory segment.\n");
        return -1;
    }
    // attach the shared memory to the process address space and assign image data to point at it.
    // a shmaddr of NULL yields attachment at the first available address.
    image->data = shmat(shm_id, NULL, 0);

    // request that XCB also attach the shared memory segment.
    xcb_shm_seg_t xcb_shm_segment = xcb_generate_id(connection);
    xcb_void_cookie_t cookie = xcb_shm_attach_checked(connection, xcb_shm_segment, shm_id, 0);
    xcb_generic_error_t * error = xcb_request_check(connection , cookie);

    // Creating and showing a window.
    xcb_window_t window = xcb_generate_id(connection);
    const unsigned int window_mask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;
    const unsigned int window_values[] = {screen->white_pixel, XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_STRUCTURE_NOTIFY};
    xcb_create_window(connection, XCB_COPY_FROM_PARENT, window, screen->root, 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual, window_mask, window_values);

    // Create a standard ICCCM struct for hints that suggest to the window manager the correct dimensions and placement.
    xcb_size_hints_t window_manager_size_hints;
    // Set values for the suggested dimension and placement limits for the window.
    xcb_icccm_size_hints_set_position(&window_manager_size_hints, 0, 0, 0);
    xcb_icccm_size_hints_set_max_size(&window_manager_size_hints, WINDOW_WIDTH, WINDOW_HEIGHT);
    xcb_icccm_size_hints_set_min_size(&window_manager_size_hints, WINDOW_WIDTH, WINDOW_HEIGHT);
    // Change the normal hints window manager property to the newly created size hints.
    xcb_icccm_set_wm_size_hints(connection, window, XCB_ATOM_WM_NORMAL_HINTS, &window_manager_size_hints);

    //xcb_change_property(connection, XCB_PROP_MODE_REPLACE, window, XCB_ATOM_WM_CLASS, XCB_ATOM_WM_CLASS, )

    xcb_map_window(connection, window);

    xcb_flush(connection);

    // Drawing graphics in a window requires a graphics context 
    xcb_gcontext_t graphics_context = xcb_generate_id(connection);
    const unsigned int graphics_context_mask = XCB_GC_FOREGROUND;
    const unsigned int graphics_context_values[] = {screen->black_pixel};
    xcb_create_gc(connection, graphics_context, window, graphics_context_mask, graphics_context_values);

    // Add some image data and write the image to the screen.
    for (unsigned int j = 50; j < image->height - 50; j ++)
    {
        for (unsigned int i = 50; i < image->width - 50; i ++)
        {
            *(image->data + (image->stride * j) + (i * 4) + (1 * ((i > 200) && (j > 200))) + (1 * ((i < image->width - 200) && (j < image->height - 200)))) = 0xFF;
        }
    }
    
    // I have no idea how or why the following works.
    // It changes some sort or property such that an XCB_CLIENT_MESSAGE event is triggered when the window is closed.
    // Some aspect of the content of the message can be checked against a "WM_DELETE_WINDOW" atom to see if the window
    // manager has closed the window. If so we can gracefully quit out of the program.
    xcb_intern_atom_cookie_t protocol_cookie = xcb_intern_atom(connection, 1, 12, "WM_PROTOCOLS");
    xcb_intern_atom_reply_t * protocol_reply = xcb_intern_atom_reply(connection, protocol_cookie, NULL);
    xcb_intern_atom_cookie_t close_cookie = xcb_intern_atom(connection, 0, 16, "WM_DELETE_WINDOW");
    xcb_intern_atom_reply_t * close_reply = xcb_intern_atom_reply(connection, close_cookie, NULL);
    xcb_change_property(connection, XCB_PROP_MODE_REPLACE, window, protocol_reply->atom, XCB_ATOM_ATOM, 32, 1, &(close_reply->atom));

    int loop = 1;
    xcb_generic_event_t * event;
    while (loop)
    {
        event = xcb_wait_for_event(connection);
        switch (event->response_type & 0x7F)
        {
            case XCB_EXPOSE:
            xcb_shm_put_image(connection, window, graphics_context, image->width, image->height, 0, 0, image->width, image->height, 0, 0, image->depth, image->format, 0, xcb_shm_segment, 0);
            xcb_flush(connection);
            break;

            case XCB_CLIENT_MESSAGE:
            if (((xcb_client_message_event_t *)event)->data.data32[0] == close_reply->atom)
            {
                loop = 0;
            }
            break;

            default:
            break;
        }
    }

    xcb_disconnect(connection);
    return 0;
}