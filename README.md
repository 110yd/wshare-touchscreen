# wshare-touchscreen
Waveshare touchscreen DKMS driver
Reworked from old userspace driver.

### Compile:

1. Extract source folder into /usr/src/wsh7inch-0.1/

2. Build through DKMS system:

	sudo dkms build wsh7inch/0.1

### Install

	sudo dkms install wsh7inch/0.1

### Restrictions

* Sometimes touchscreen is really buggy. This driver does not perform any data correction
