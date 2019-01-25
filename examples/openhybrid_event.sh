#!/bin/sh
# OpenHybrid event script example. This script is executed every time something important happens in OpenHybrid.
#
# The name of the event will be passed as 1st parameter: $1
# List of all events:
# -  startup
#    Triggered when OpenHybrid is started.
# -  shutdown
#    Triggered when OpenHybrid is stopped.
# -  tunneldown
#    Triggered when the tunnel device is destroyed.
# -  tunnelup
#    Triggered when the tunnel device is created.
# -  dhcpup_ip
#    Triggered when an ipv4 address is obtained.
# -  dhcpup_ip6
#    Triggered when an ipv6 prefix is obtained.
# -  dhcpdown_ip
#    Triggered when the lease of the ipv4 address expired.
# -  dhcpdown_ip6
#    Triggered when the lease of the ipv6 prefix expired.
#
# Other information will be provided via environment variables, such as $dhcp_ip.
# List of all environment variables:
# - lte_interface_name
#   Name of the LTE interface as defined in openhybrid.conf.
# - dsl_interface_name
#   Name of the DSL interface as defined in openhybrid.conf. Only available in bonding mode.
# - tunnel_interface_name
#   Name of the tunnel interface as defined in openhybrid.conf. Only available in bonding mode.
# - tunnel_interface_mtu
#   MTU of the tunnel interfaces.
# - dhcp_ip
#   Public IPv4 address obtained via dhcp.
# - dhcp_lease_time
#   Lease time of the ip define in $dhcp_ip in seconds.
# - dhcp6_prefix_address
#   Public IPv6 prefix address obtained via dhcp6.
# - dhcp6_prefix_length
#   Length of the public IPv6 prefix defined in $dhcp6_prefix_address.
# - dhcp6_lease_time
#   Lease time of the prefix define in $dhcp6_prefix_* in seconds.

# These events may fire almost simultaneously, spawning multiple instances of this script.
# If your script doesn't support multiple instances, use lockfile-progs(1) for queuing, like so:
#lockfile-create -p -l /tmp/openhybrid-event-script.lock
#trap "lockfile-remove -l /tmp/openhybrid-event-script.lock" EXIT

# This is probably as minimalistic as it gets.
# Note: Use 'replace' instead of 'add' and 'flush' instead of 'delete', if possible.
case "${1}" in
    # when a tunnel device is created
    tunnelup)
        # Not much we can do with the device itself. The real actions begins once we get a DHCP ip...
        ;;
    # when an ipv4 address is obtained
    dhcpup_ip)
        # assign it to the device
        ip -4 address replace ${dhcp_ip}/32 dev $tunnel_interface_name

        # and add routes for heise.de and speed.hetzner.de, use ip's since we may not have a working DNS
        for dst in 193.99.144.80 88.198.248.254
        do
            ip -4 route replace $dst dev $tunnel_interface_name
        done
        ;;
    # when an ipv6 prefix is obtained
    dhcpup_ip6)
        # assign the first ip of the prefix to the device (TODO: use real math to calculcate the first usable ip)
        ip -6 address replace ${dhcp6_prefix_address}1/128 dev $tunnel_interface_name

        # and add routes for heise.de and speed.hetzner.de, use ip's since we may not have a working DNS
        for dst in 2a02:2e0:3fe:1001:302:: 2a01:4f8:0:59ed::2
        do
            ip -6 route replace $dst dev $tunnel_interface_name
        done
        ;;
    # when a dhcp lease (ipv4) expires
    dhcpdown_ip)
        # tunnel device may have already been removed if OpenHybrid is shutting down
        if ip link show $tunnel_interface_name &> /dev/null
        then
            # delete routes and ips
            ip -4 route flush dev $tunnel_interface_name
            ip -4 address flush dev $tunnel_interface_name
        fi
        ;;
    # when a dhcp lease (ipv6) expires
    dhcpdown_ip6)
        # tunnel device may have already been removed if OpenHybrid is shutting down
        if ip link show $tunnel_interface_name &> /dev/null
        then
            # delete routes and (global) ips
            ip -6 route flush dev $tunnel_interface_name
            ip -6 address flush dev $tunnel_interface_name
        fi
        ;;
    # when a tunnel device is destroyed
    tunneldown)
        # Addresses and routes linked to a device will automatically be removed if the device is removed
        ;;
esac