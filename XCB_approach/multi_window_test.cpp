// Compile with g++ -Wall multi_window_test.cpp XCB_framebuffer_window.cpp -o multi_window_test.exec -lxcb -lxcb-image -lxcb-shm -lxcb-icccm
#include <iostream>
#include <new>

#include "XCB_framebuffer_window.h"

int main(int argc, char * argv[])
{
    struct window_props window_1_properties;
    struct window_props window_2_properties;

    class Framebuffer_window window_1(640, 480, "Window 1", 8, &window_1_properties);
    if (window_1_properties.error_status < 0)
    {
        std::cout << "Failed to create window 1.\n";
        return -1;
    }

    class Framebuffer_window window_2(1024, 768, "Window 2", 8, &window_2_properties);
    if (window_2_properties.error_status < 0)
    {
        std::cout << "Failed to create window 2.\n";
        return -1;
    }

    bool loop = true;
    while (loop)
    {
        if (window_1.handle_events() < 0) loop = false;
        if (window_2.handle_events() < 0) loop = false;
    }

    return 0;
}