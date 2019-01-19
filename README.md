# OpenHybrid
An open client implementation of [RFC8157](https://tools.ietf.org/html/rfc8157).

In other words: A way to use [MagentaZuhause Hybrid](https://www.telekom.de/zuhause/tarife-und-optionen/internet/magenta-zuhause-m-hybrid) without a [Speedport Hybrid](https://www.telekom.de/zuhause/geraete-und-zubehoer/wlan-und-router/speedport-hybrid) router.

## State of the implementation

OpenHybrid is still expirmental and requires quite a bit of tinkering to get it to work. You are however encouraged to try it and provide feedback. But you may want refrain from throwing away your Speedport Hybrid just yet.

## Operating modes

OpenHybrid has two operating modes, controlled by the `bonding` configuration option.

### LTE only

In this mode OpenHybrid will establish a GRE tunnel to the HAAP (Hybrid Access Aggregation Point) server hosted by Deutsche Telekom using the (restricted) LTE connection. It will then obtain a public IPv4 address and a public IPv6 prefix using DHCP with which you have full access to the internet.

Use this mode if you wish to use the DSL and LTE connection independently, possibly even on different routers.

### Bonding (LTE + DSL)

In addition to the GRE tunnel describe in LTE only mode, OpenHybrid will establish a second GRE tunnel using the DSL connection. These two tunnels are bonded together by the HAAP: Both tunnel interfaces have the same IP addresses, allowing you to sent/receive an IP packet through either of them.

For downloads the HAAP will distribute the IP packets among those two tunnels, allowing you to utilize the bandwidth of both connections (LTE + DSL) with a single TCP/IP connection.

For uploads you can implement a similar logic, or route your outgoing traffic stacially through either tunnel.

## Requirements

### Hardware

* LTE modem (or usb stick) with IPv6 only support
* VDSL2 modem (or router in bridge mode) supporting profile 17a
* A system capable of running Linux

### Software

* Linux (kernel 3.7 or newer)
* busybox (sh, udhcpc and udhcpc6 applets)
* libmnl

And for compilation:
* GNU make
* A reasonable C compiler

#### udhcpc6 prefix delegation bug

The current (1.29.3) version of busybox' udhcpc6 does not support prefix delegation. You need to patch it using `busybox/udhcpc6_iapd.patch`. Without this patch IPv6 will not work and you are stuck with IPv4 only.


## Usage

### Step 1: Establish a DSL connection (optional for LTE only mode)

Set up a regular PPPoE connection. Nothing special here. You can use pppd or a even a hardware router if it support forwarding of GRE traffic.

### Step 2: Set up the LTE connection

You need to establish an IPv6 only connection using your LTE modem or usb stick.

The catch is that you need an IPv6 only connection without DHCP. The IP address is obtained via dynamic PDP parameters. So forget all those fancy tools like ModemManager, you are pretty much stuck with raw AT commands...

Start up your favorite serial communication program, for instance minicom

You may very well end up using raw AT commands yourself, probably consolidated in a shell script. In a nutshell the AT commands required are:
```
AT+CGDCONT=1,"IPV6","hybrid.telekom"
AT^NDISDUP=1,1,"hybrid.telekom"
AT+CGCONTRDP
AT+CGPADDR
```

The 1st line will create a new profile (bearer), the 2nd line will initiate the connection and the 3rd line will show you the dynamic PDP parameters, which contain the public IPv6 address assigned to you in decimal.

You can also automate this with scripts, see `examples/lte.sh` for example.


Once the LTE connection is established you will not have access to the internet. Your access will be restricted to:
- The HAAP servers
- The DNS servers provided via PDP

### Step 3: Set up policy based routing (optional for LTE only mode)

Regular routing uses the destination address of a packet to determine which interface to use for sending. If you have multiple interfaces (LTE + DSL) and you whish to send packets to a single server using both interfaces you need to set up source based routing.

See `examples/policy-based-routing.sh` for an example.

### Step 4: Create OpenHybrid config

Create a config file for OpenHybrid. You can use `examples/openhybrid.conf` as a template, it contains comments to describe what each config option does.

### Step 5: Create OpenHybrid event script

OpenHybrid will not assign IP addresses or install routes, that's what the event script is for.

See `examples/openhybrid_event.sh` for an example.

Make sure you've set `event script path` to the absolute or relative path of this script in your OpenHybrid config.

### Step 6: Compile and start OpenHybrid

Compilation is fairly simple, just type make:
 ```
 cd src
 make
```

And then start OpenHybrid as root, providing the path to the configuration file as argument:
```
 sudo ./openhybrid /path/to/openhybrid.conf
 ```

## How to report bugs

Please report bugs via GitHub issues. Remember to include as much details as possible.