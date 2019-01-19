#!/bin/bash
LTE_INTF="wwan0"
DSL_INTF="ppp0"
# edit /etc/iproute2/rt_tables if you prefer names over numbers
LTE_TABLE="101"
DSL_TABLE="102"

if ! ip -6 rule show &> /dev/null
then
    # System doesn't support rule-based routing? Regular routing should do for an LTE only setup...
    router=$(ip -o -6 neigh show dev $LTE_INTF | grep " router " | awk '{print $1}')
    ip -6 route add 2003:6:5000::0/36 via $router dev $LTE_INTF
else
    # LTE
    ip -6 route flush table $LTE_TABLE
    router=$(ip -o -6 neigh show dev $LTE_INTF | grep " router " | awk '{print $1}')
    ip -6 route add default via $router dev $LTE_INTF table $LTE_TABLE
    ip=$(ip -o -6 addr show dev $LTE_INTF scope global | head -n 1 | awk '{print $4}' | sed -e 's#/.*$##')
    ip -6 rule del table $LTE_TABLE 2> /dev/null
    ip -6 rule add from $ip table $LTE_TABLE

    # DSL
    ip -6 route flush table $DSL_TABLE
    ip -6 route add default dev $DSL_INTF table $DSL_TABLE
    ip=$(ip -o -6 addr show dev $DSL_INTF scope global | head -n 1 | awk '{print $4}' | sed -e 's#/.*$##')
    ip -6 rule del table $DSL_TABLE 2> /dev/null
    ip -6 rule add from $ip table $DSL_TABLE
fi
