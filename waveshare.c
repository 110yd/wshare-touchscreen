#include <stdio.h>
#include <stdlib.h>
#include <err.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <suinput.h>

#include <libudev.h>
#include <locale.h>

#include <pthread.h>

#define MAX_DEVICES_SIMULTANEONUSLY 4

#define UDEV_SELECT_TIMEOUT_uS 100

#define DEVICE_ID_VENDOR_HEX  0x0eef
#define DEVICE_ID_VENDOR      "0eef"
#define DEVICE_ID_PRODUCT_HEX 0x005
#define DEVICE_ID_PRODUCT     "0005"

#define DEVICE_WIDTH           800
#define DEVICE_HEIGHT          480

// Length of message read from hidraw*
#define MESSAGE_LENGTH 25

// Message layout
#define MULTITOUCH_IS_PRESSED_OFFSET    1
#define MULTITOUCH_FIRST_POINT_OFFSET   2
#define MULTITOUCH_SECOND_POINT_OFFSET  8
#define MULTITOUCH_THIRD_POINT_OFFSET   12
#define MULTITOUCH_FOURTH_POINT_OFFSET  16
#define MULTITOUCH_FIFTH_POINT_OFFSET   20
#define MULTITOUCH_PRESSED_COUNT_OFFSET 7

/**
 * @brief [UNSAFE] Read unsigned short (2 bytes) from buffer
 * @param Read buffer
 * @param Int-typed cursor pointer
 * @returns Read result
 */
unsigned short read_point(unsigned char *from_buffer, int offset)
{
    return from_buffer[offset] << 8 | from_buffer[offset + 1];
}

/**
 * @brief [UNSAFE] Read (x,y) pair from buffer and emit uinput signals
 * @param uinput file descriptor
 * @param Read buffer
 * @param Int-typed cursor pointer
 */
void emit_point(const int uinput_fd, unsigned char *from_buffer, int offset ) {
   suinput_emit(uinput_fd, EV_ABS, ABS_MT_POSITION_X, read_point(from_buffer, offset));
   suinput_emit(uinput_fd, EV_ABS, ABS_MT_POSITION_Y, read_point(from_buffer, offset + 2));
   suinput_emit(uinput_fd, EV_SYN, SYN_MT_REPORT, 0);
}

/**
 * @brief [UNSAFE] Read (x,y) pair from buffer and emit uinput signals
 * @param uinput file descriptor
 * @param Read buffer
 * @param Int-typed cursor pointer
 */
void emit_point_reversed(const int uinput_fd, unsigned char *from_buffer, int offset ) {
   suinput_emit(uinput_fd, EV_ABS, ABS_MT_POSITION_X, read_point(from_buffer, offset + 2));
   suinput_emit(uinput_fd, EV_ABS, ABS_MT_POSITION_Y, read_point(from_buffer, offset));
   suinput_emit(uinput_fd, EV_SYN, SYN_MT_REPORT, 0);
}

/** @brief Active device entry */
typedef struct {
   // device path
   char *path;
   // thread id or 0
   pthread_t  thread;
} device_entry;

device_entry DEVICES[MAX_DEVICES_SIMULTANEONUSLY];

/**
 * @brief Device driver thread function
 * @param Pointer to device path in filesystem
 */
void *device_loop(void *arg) {
   device_entry entry = DEVICES[(int)arg];
   // read hidraw device file
   FILE * pFile;
   pFile = fopen (entry.path, "r");
   if (pFile == NULL) {
      entry.thread = 0;
      pthread_exit(NULL);
      return 0;
   }

   struct uinput_user_dev user_dev;
   memset(&user_dev, 0, sizeof(struct uinput_user_dev));
   
   user_dev.absmin[ABS_MT_POSITION_X] = 0;
   user_dev.absmax[ABS_MT_POSITION_X] = DEVICE_WIDTH;
   user_dev.absmin[ABS_MT_POSITION_Y] = 0;
   user_dev.absmax[ABS_MT_POSITION_Y] = DEVICE_HEIGHT;
   user_dev.id.bustype = BUS_USB;
   user_dev.id.vendor  = DEVICE_ID_VENDOR_HEX;
   user_dev.id.product = DEVICE_ID_PRODUCT_HEX;
   user_dev.id.version = 1;

   strcpy(user_dev.name, "Waveshare multitouch screen");
   int uinput_fd;

   uinput_fd = suinput_open();

   if (uinput_fd == -1) {
      entry.thread = 0;
      pthread_exit(NULL); 
      return 0;
   }

   int rel_axes[] = { 
        ABS_MT_POSITION_X, 
        ABS_MT_POSITION_Y
   };
   int i;

   for (i = 0; i < 2; ++i) {
      if (suinput_enable_event(uinput_fd, EV_ABS, rel_axes[i]) == -1) {
        close(uinput_fd);
        entry.thread = 0;
        pthread_exit(NULL);
        return 0;
      } 
   }
   
   suinput_create(uinput_fd, &user_dev);

   unsigned char buffer[MESSAGE_LENGTH], 
                 pressed_count,
                 waspressed = 0;

   while (1) {

      if (fread(&buffer, 1, MESSAGE_LENGTH, pFile) != MESSAGE_LENGTH) {
         suinput_destroy(uinput_fd);
         fclose(pFile);
         break;
      }           

      if (buffer[MULTITOUCH_IS_PRESSED_OFFSET] == 0x01) {
         waspressed = 1;
         pressed_count = buffer[MULTITOUCH_PRESSED_COUNT_OFFSET];

         if (pressed_count & 0x01) 
            emit_point(uinput_fd, buffer, MULTITOUCH_FIRST_POINT_OFFSET);
         if (pressed_count & 0x02)
            emit_point_reversed(uinput_fd, buffer, MULTITOUCH_SECOND_POINT_OFFSET);
         if (pressed_count & 0x04)
            emit_point_reversed(uinput_fd, buffer, MULTITOUCH_THIRD_POINT_OFFSET);
         if (pressed_count & 0x08)
            emit_point_reversed(uinput_fd, buffer, MULTITOUCH_FOURTH_POINT_OFFSET);
         if (pressed_count & 0x10)
            emit_point_reversed(uinput_fd, buffer, MULTITOUCH_FIFTH_POINT_OFFSET);
         suinput_syn (uinput_fd);
      } else {
         if (waspressed) {
            waspressed = 0;
            suinput_emit(uinput_fd, EV_SYN, SYN_MT_REPORT, 0);
            suinput_syn (uinput_fd);
         }
      }
   }
   entry.thread = 0;
   pthread_exit(NULL);
   return 0;
}

/**
 * @brief Tries to start new thread for device
 * @param Device path
 * @returns 0 if success
 */
int try_start_device_loop(const char *device_path) { 

   int size = (1 + strlen(device_path)) * sizeof(char);

   for (int i = 0; i < MAX_DEVICES_SIMULTANEONUSLY; i++) {
      if (DEVICES[MAX_DEVICES_SIMULTANEONUSLY].thread != 0)
         continue;

      if (DEVICES[i].path)
         DEVICES[i].path = (char*) realloc(DEVICES[i].path, size);
      else
         DEVICES[i].path = (char*) malloc(size);
      
      strcpy(DEVICES[i].path, device_path);
      int rc = pthread_create(&DEVICES[i].thread, NULL, device_loop, (void *)i);
      if (rc) 
        return rc;
      return 0;
   }
   return -1;
}

/**
 * @brief Check if the devices match VID:PID and tries to initialize it
 * @param Pointer to udev_device
 * @returns -2 if this not our device, 0 if success, another value if error
 **/
int try_init_device(struct udev_device *dev) {
   struct udev_device *devusb = udev_device_get_parent_with_subsystem_devtype(dev, "usb", "usb_device");
   if (devusb) {
      if (strcmp(udev_device_get_sysattr_value(devusb, "idVendor"),  DEVICE_ID_VENDOR) == 0 &&
          strcmp(udev_device_get_sysattr_value(devusb, "idProduct"), DEVICE_ID_PRODUCT) == 0) 
      {
         return try_start_device_loop(udev_device_get_devnode(dev));
      }
   }
   return -2;
}

/**
 * @brief Loop for detecting new devices
 * @param udev monitor
 */
void monitor_loop(struct udev_monitor *mon) {
   /* Get the file descriptor (fd) for the monitor.
      This fd will get passed to select() */
   struct udev_device *dev;
   int fd = udev_monitor_get_fd(mon);
   while (1) {
      fd_set fds;
      struct timeval tv;
      int ret;

      FD_ZERO(&fds);
      FD_SET(fd, &fds);
      tv.tv_sec = 0;
      tv.tv_usec = UDEV_SELECT_TIMEOUT_uS;

      ret = select(fd + 1, &fds, NULL, NULL, &tv);

      /* Check if our file descriptor has received data. */
      if (ret > 0 && FD_ISSET(fd, &fds)) {
         /* Make the call to receive the device.
            select() ensured that this will not block. */
         dev = udev_monitor_receive_device(mon);

         if (!dev) continue;

         if (strcmp(udev_device_get_action(dev), "add") == 0)
            try_init_device(dev);
         udev_device_unref(dev);
      }
   }
}

int main(int argc, char** argv)
{
   struct udev *udev;

   udev = udev_new();
   if (!udev) 
      err(10, "Can't access to udev");

   for (int i = 0; i < MAX_DEVICES_SIMULTANEONUSLY; i++) {
      DEVICES[i].thread = 0;
      DEVICES[i].path = 0;
   }

   /* Set up a monitor to monitor hidraw devices */
   struct udev_monitor *mon = udev_monitor_new_from_netlink(udev, "udev");
   udev_monitor_filter_add_match_subsystem_devtype(mon, "hidraw", NULL);
   udev_monitor_enable_receiving(mon);

   /* Create a list of the devices in the 'hidraw' subsystem. */   
   struct udev_device *dev;
   struct udev_enumerate *enumerate;
   struct udev_list_entry *devices, *dev_list_entry;

   enumerate = udev_enumerate_new(udev);
   
   udev_enumerate_add_match_subsystem(enumerate, "hidraw");
   udev_enumerate_scan_devices(enumerate);
   devices = udev_enumerate_get_list_entry(enumerate);

   udev_list_entry_foreach(dev_list_entry, devices) {
      const char *path = udev_list_entry_get_name(dev_list_entry);
      dev = udev_device_new_from_syspath(udev, path);
      try_init_device(dev);
      udev_device_unref(dev);
   }

   monitor_loop(mon);

   pthread_exit(NULL);
}