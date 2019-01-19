#!/bin/sh
# OpenHybrid event script example. This script is executed every time something important happens in OpenHybrid.
#
# The name of the event will be passed as 1st parameter: $1
# List of all events:
# -  startup
#    Triggered when OpenHybrid is started.
# -  shutdown
#    Triggered when OpenHybrid is stopped.
# -  tunneldown_lte
#    Triggered when the tunnel device for the lte tunnel is destroyed.
# -  tunneldown_dsl
#    Triggered when the tunnel device for the dsl tunnel is destroyed.
# -  tunnelup_lte
#    Triggered when the tunnel device for the lte tunnel is created.
# -  tunnelup_dsl
#    Triggered when the tunnel device for the dsl tunnel is created.
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
# - lte_gre_interface_name
#   Name of the LTE tunnel interface as defined in openhybrid.conf.
# - dsl_interface_name
#   Name of the DSL interface as defined in openhybrid.conf. Only available in bonding mode.
# - dsl_gre_interface_name
#   Name of the DSL tunnel interface as defined in openhybrid.conf. Only available in bonding mode.
# - gre_interface_mtu
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
    # when a tunnel device is created or an ip/prefix is obtained
    tunnelup_*|dhcpup_*)
        # execute the commands below for all configured interfaces
        for dev in ${lte_gre_interface_name} ${dsl_gre_interface_name}
        do
	        # skip if interface does not exist
            ip link show $dev &> /dev/null || continue

            # use different metrics for lte and dsl, this way we have an automatic failover an we don't have to deal with rp_filter (ipv4)
            [ "${dev}" = "${dsl_gre_interface_name}" ] && metric=1 || metric=2

            # if we have an ipv4 addres
            if [ -n "${dhcp_ip}" ]
            then
	            # assign it to the device
                ip -4 address replace ${dhcp_ip}/32 dev $dev

                # and add routes for heise.de and speed.hetzner.de, use ip's since we may not have a working DNS
                for dst in 193.99.144.80 88.198.248.254
                do
                    ip -4 route replace $dst dev $dev metric $metric
                done
            fi

            # if we have an ipv6 prefix
            if [ "${dhcp6_prefix_address}" != "::" ]
            then
	            # assign the first ip of the prefix to the device
                ip -6 address replace ${dhcp6_prefix_address}1/128 dev $dev

                # and add routes for heise.de and speed.hetzner.de, use ip's since we may not have a working DNS
                for dst in 2a02:2e0:3fe:1001:302:: 2a01:4f8:0:59ed::2
                do
                    ip -6 route replace $dst dev $dev metric $metric
                done
            fi
        done
        ;;
    # when a dhcp lease (ipv4) expires
    dhcpdown_ip)
        # execute the commands below for all configured interfaces
        for dev in $lte_gre_interface_name $dsl_gre_interface_name
        do
	        # skip if interface does not exist
            ip link show $dev &> /dev/null || continue

            # delete routes and ips
            ip -4 route flush dev $dev
            ip -4 address flush dev $dev
        done
        ;;
    # when a dhcp lease (ipv6) expires
    dhcpdown_ip6)
        # execute the commands below for all configured interfaces
        for dev in $lte_gre_interface_name $dsl_gre_interface_name
        do
	        # skip if interface does not exist
            ip link show $dev &> /dev/null || continue

            # delete routes and (global) ips
            ip -6 route flush dev $dev
            ip -6 address flush dev $dev scope global
        done
        ;;
    # when a tunnel device is destroyed
    tunneldown_*)
        # Addresses and routes linked to a device will automatically be removed if the device is removed
        ;;
esac