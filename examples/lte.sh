#!/bin/bash
INTF_AT="/dev/ttyUSB0"
INTF_ETH="wwan0"

openFD() {
    if [ ! -c "${INTF_AT}" ]
    then
        echo "${INTF_AT}: Character device not found."
        exit 1
    fi
    if [ ! -r "${INTF_AT}" ] || [ ! -w "${INTF_AT}" ]
    then
        echo "${INTF_AT}: Permission denied."
        exit 1
    fi

    # open device for r/w
    exec 99<>"${INTF_AT}"

    # verify we are talking to an at modem
    if ! execAT AT
    then
        echo "Error: Modem did not react properly to wakeup command."
        exit 1
    fi

    # and check if the eth device exists
    if ! ip link show dev "${INTF_ETH}" &> /dev/null
    then
        echo "${INTF_ETH}: Ethernet device not found."
        exit 1
    fi

    # initialize modem, see https://www.computerhope.com/atcom.htm
    execAT ATZ # factory defaults should be fine
}

execAT () {
    # clear buffer
    timeout 0.1 cat <&99 >/dev/null

    # execute command
    echo "${*}" >&99

    # read answer(s)
    for i in {1..20} # 20 rows / 2 seconds max
    do
        read -t 0.1 -r answer <&99
        if [ -n "${answer}" ]
        then
            if  [ "${answer}" = "OK" ] || \
                [ "${answer}" = "ERROR" ] || \
                [ "${answer}" = "COMMAND NOT SUPPORT" ]
            then
                break
            else
                echo "${answer}"
            fi
        fi
    done
    [ "${answer}" = "OK" ]
}

closeFD() {
    # close fd
    exec 99<&-
}

statusInfo() {
    echo -n "SIM Status: "
    execAT AT+CPIN? | awk '{print $2}'
    echo -n "Network Status: "
    case $(execAT AT+CGREG? | awk '{print $2}') in
        0,0) echo "Not Registered" ;;
        0,1) echo "Registered" ;;
        0,2) echo "Searching" ;;
        0,3) echo "Registration Denied" ;;
        0,5) echo "Registered (Roaming)" ;;
        *) echo "Unkown" ;;
    esac
    echo -n "Network Name: "
    execAT AT+COPS? | awk '{print $2}' | cut -d '"' -f 2 | tr -d '"'
    echo -n "Signal Quality: "
    echo "$((-113 + $(execAT AT+CSQ | awk '{print $2}' | cut -d , -f 1)*2)) dBm"
    echo -n "PDP Status: "
    if execAT AT+CGACT? | grep -q "^+CGACT: 1,1"
    then
        echo "Active"
        echo "WAN IP: $(convertToIP $(getActivePDPParameters 3))"
        echo "DNS1 IP: $(convertToIP $(getActivePDPParameters 5))"
        echo "DNS2 IP: $(convertToIP $(getActivePDPParameters 6))"
    else
        echo "Inactive"
    fi
}

configureAPN() {
    #execAT AT+CGDCONT=?
    execAT "AT+CGDCONT=1,\"IPV6\",\"hybrid.telekom\""
}

connect() {
    local timeout=15
    while [ "$(execAT AT+CGREG? | awk '{print $2}')" != "0,1" ]
    do
        echo "Warning: Modem is not registered with network. Trying to register..."
        execAT AT+CGATT=1
        sleep 5
        timeout=$((${timeout}-5))
        if [ "${timeout}" -le 0 ]
        then
            echo "Error: Unable to register with network."
            exit 1
        fi
    done
    #execAT AT+CGACT=1,1
    execAT "AT^NDISDUP=1,1,\"hybrid.telekom\""
    execAT AT+CGACT? | grep -q "^+CGACT: 1,1"
    
}

disconnect() {
    #execAT AT+CGACT=0,1
    execAT AT^NDISDUP=1,0
}

getActivePDPParameters() {
    local index="${1}"
    local pdpinfo
    IFS="," read -r -a pdpinfo <<< $(execAT AT+CGCONTRDP | awk '{print $2}')
    echo "${pdpinfo[${index}]}" | tr -d '"'
}

convertToIP() {
    local decvalues
    IFS="." read -r -a decvalues <<< "${1}"
    for i in {0..15}
    do
        if [ "${i}" -gt 0 ] && [ "$((${i}%2))" -eq 0 ]
        then
            echo -n ":"
        fi
        printf %02x "${decvalues[${i}]}"
    done
    echo ""
}

configureInterface() {
    ip link set "${INTF_ETH}" up
    ip -6 address replace $(convertToIP $(getActivePDPParameters 3)) dev "${INTF_ETH}"
}

waitForRouterAdvertisement() {
    local timeout=30
    while true
    do
        [ -n "$(ip -6 neighbour show dev "${INTF_ETH}" | awk '{print $1}')" ] && break
        sleep 1
        timeout=$((${timeout}-1))
        if [ "${timeout}" -le 0 ]
        then
            echo "Error: Did not retrieve a router advertisement within a reasonable time frame."
            exit 1
        fi
    done
}

configureDNS() {
    local gateway=$(ip -6 neighbor show dev "${INTF_ETH}" | awk '{print $1}')
    ip -6 route replace $(convertToIP $(getActivePDPParameters 5)) via "${gateway}" dev "${INTF_ETH}"
    ip -6 route replace $(convertToIP $(getActivePDPParameters 6)) via "${gateway}" dev "${INTF_ETH}"
}

deconfigureInterface() {
    ip link set "${INTF_ETH}" down
}

# main()
openFD
case "${1}" in
    connect)
        configureAPN
        if connect
        then
            configureInterface
            #waitForRouterAdvertisement
            #configureDNS
        else
            echo "Error: Activation of PDP context failed."
        fi
        ;;
    disconnect) 
        deconfigureInterface
        disconnect
        ;;
    status)
        statusInfo
        ;;
    *)
        echo "Usage: ${0} connect|disconnect|status"
        exit 1
        ;;
    esac
closeFD
