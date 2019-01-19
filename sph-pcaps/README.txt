Packet captures of a real Speedport Hybrid for reverse engineering, because the RFC is missing some vital parts.

Files in this directory:
- analyze.py: scapy based python script to parse pcap files and live dumps and print the control plane in human readable form
- Firmware_Speedport_Hybrid_v050124.02.00.012.bin: latest v2 firmware
- tcpdump: statically linked tcpdump binary (tcpdump: 4.3.0, libpcap: 1.3.0), compatible with v2 firmware

Files in subdirectories:
- rmnet0.pcap: dump of lte interface
- ppp256.pcap: dump of dsl interface
- ip_down.txt: outputs of various ip(8) commands with both connections disabled
- ip_half.txt: outputs of various ip(8) commands with only the 1st connection up
- ip_full.txt: outputs of various ip(8) commands with both connections up

How to (re)create these?
- Downgrade your Speedport Hybrid to firmware 050124.02.00.012 or older (telnet access was removed in v3)
- Disable DSL+LTE (via web interface)
- Wait for a few minutes (HAAP timeouts and such, we want a clean capture)
- Enable telnet (download config, decrypt, modify, encrypt, restore)
- Connect via telnet and open a root shell (sh, su)
- Disable hardware acceleration: echo 0 > /proc/sys/net/core/hisi_sw_accel_flag
- Capture on ppp256 (dsl) and rmnet0 (lte) (devices do yet exist, start tcpdump in a loop)
- Enable DSL/LTE (via web interface)
- Enable DSL/LTE (via web interface)

Tcpdump filter to only include control plane messages:
- 'ip6 proto gre and ip6[42:2]=0x0101'