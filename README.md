# wshare-touchscreen
Waveshare touchscreen user space driver

I brought a Waveshare 7-inch HDMI LCD and it provides touchscreen through USB. Unfortunately, dvd with raspbian images for raspberry was completely unreadable, so I decided to google problem and have [found solution](https://github.com/derekhe/wavesahre-7inch-touchscreen-driver).

But I thought, that writing driver in Python is completely bad idea, and that driver imitated just mouse, not touchscreen, as I needed for my project. So, I decided to implement user space uinput driver, that implements [simple (type A) protocol from linux documetation](https://www.kernel.org/doc/Documentation/input/multi-touch-protocol.txt).

### Dependencies:

- [libsuinput](https://github.com/tuomasjjrasanen/libsuinput), version >= 0.6.1
- pthread
- C99 compiler
- libudev 

### Compile:

	gcc -std=c99 -Wall ./waveshare.c -pthread -lsuinput -ludev -o waveshare-touch-driver

### Install

1. Copy file to the path
	
	`sudo cp waveshare-touch-driver /usr/local/bin/waveshare-touch-driver`

2. Edit `/etc/rc.local` file and append:

	`/usr/local/bin/waveshare-touch-driver`	

3. Edit /etc/modules and append neccessary `uinput` kernel module

4. Reboot

### Restrictions

* Sometimes touchscreen is really buggy. This driver does not perform any data correction
* For now "clean" `/dev/input/eventX` source is almost useless for X applications, and you should use additional driver for implementing touchscreen support in X. However it is useful to launch Qt applications in EGLFS mode. Just add `QT_QPA_EVDEV_TOUCHSCREEN_PARAMETERS=/dev/input/eventX` to environment variables (where X is device number)
