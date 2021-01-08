# Notes on building OpenHybrid for OpenWRT

## Clone

```
$> git clone https://github.com/blu3bird/OpenHybrid.git
$> cd OpenHybrid
$> OPENHYBRID_PATH=$(pwd)
```

## Compile

_The following commands assume you are in the OpenWRT directory and your toolchain is already build._

- You need to copy the busybox patches to your OpenWRT repository.
	```
	$> cp $OPENHYBRID_PATH/openwrt/busybox-patches/* package/utils/busybox/patches/
	```
	_The linux kernel patch is only required if you are buidling for the [bugged](https://bugzilla.kernel.org/show_bug.cgi?id=202147#c1) kernel version 4.20 or higher._

- Copy openhybrid directory
	```
	$> cp -r $OPENHYBRID_PATH/openwrt/openhybrid package/network/service/
	```

- OpenHybrid needs to be added to the `.config` file.

	Select openhybrid package by running:
	```
	$> make menuconfig
	```
	and select the package in _Network_ by pressing `m`.
	Build it.
	```
	$> make package/openhybrid/download
	$> make package/openhybrid/prepare
	$> make package/openhybrid/compile
	```

## Install

Copy the busybox and openhybrid .ipk file to your router and install both.
Set your configuration in /etc/openhybrid/openhybrid.conf and run it with
```
#> openhybrid /etc/openhybrid/openhybrid.conf
```

## TODO
- Add patch file for redefined `ethhdr`
	Hotfix: Wrap ethhdr definition into: `#ifndef _LINUX_IF_ETHER_H`

- Add service init.d script
