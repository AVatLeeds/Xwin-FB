#include <stdio.h>
#include <X11/Xlib.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <X11/extensions/XShm.h>

void main()
{
    int x = 640;
    int y = 480;
    Display * display = XOpenDisplay(NULL);
    XShmSegmentInfo shm_info;

    int screen_num = DefaultScreen(display);
    Window root_window = RootWindow(display, screen_num);
    Window app_window = XCreateSimpleWindow(display, root_window, 0, 0, x, y, 0, 0, 0);

    if (!XShmQueryExtension(display))
    {
        printf("Error: X11 Shm extension not supported. Are you running over a remote connection?");
        return;
    }

    int pixmap_format = XShmPixmapFormat(display);
    printf("%d\n", pixmap_format);

    shm_info.shmid = shmget(IPC_PRIVATE, (x * 4) * y, IPC_CREAT|0777);
    printf("%d\n", shm_info.shmid);
    shm_info.readOnly = 0;
    shm_info.shmaddr = shmat(shm_info.shmid, 0, 0);
    
    if (!XShmAttach(display, &shm_info))
    {
        printf("Error: Failed to attach shared memory segment.");
        return;
    }

    XShmCreatePixmap(display, app_window, shm_info.shmaddr, &shm_info, x, y, 0);

    XMapWindow(display, app_window);
    XFlush(display);

    while (1)
    {
        static int i = 0;
        for (i = 0; i < ((x * 4) * y); i += 4)
        {
            shm_info.shmaddr[i] = 0xFF;
            XFlush(display);
        }
    }
    
    return;
}
