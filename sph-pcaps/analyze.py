#!/usr/bin/env python3
from scapy.all import *
import os, sys, datetime

grecp_message_types = {
    1: "GRE Tunnel Setup Request",
    2: "GRE Tunnel Setup Accept",
    3: "GRE Tunnel Setup Deny",
    4: "GRE Tunnel Hello",
    5: "GRE Tunnel Tear Down",
    6: "GRE Tunnel Notify",
}

grecp_attribute_types = {
    1: "H IPv4 Address",
    2: "H IPv6 Address",
    3: "Client Identification Name",
    4: "Session ID", # RFC says this is a string...doesn't look like a string to me
    5: "Timestamp",
    6: "Bypass Traffic Rate",
    7: "DSL Synchronization Rate",
    8: "Filter List Package",
    9: "RTT Difference Threshold",
    10: "Bypass Bandwidth Check Interval",
    11: "Switching to DSL Tunnel",
    12: "Overflowing to LTE Tunnel",
    13: "IPv6 Prefix Assigned by HAAP",
    14: "Active Hello Interval",
    15: "Hello Retry Times",
    16: "Idle Timeout",
    17: "Error Code",
    18: "DSL Link Failure",
    19: "LTE Link Failure",
    20: "Bonding Key Value",
    21: "IPv6 Prefix Assigned to Host",
    22: "Configured DSL Upstream Bandwidth",
    23: "Configured DSL Downstream Bandwidth",
    24: "RTT Difference Threshold Violation",
    25: "RTT Difference Threshold Compliance",
    26: "Diagnostic Start: Bonding Tunnel",
    27: "Diagnostic Start: DSL Tunnel",
    28: "Diagnostic Start: LTE Tunnel",
    29: "Diagnostic End",
    30: "Filter List Package ACK",
    31: "Idle Hello Interval",
    32: "No Traffic Monitored Interval",
    33: "Switching to Active Hello State",
    34: "Switching to Idle Hello State",
    35: "Tunnel Verification",
    54: "DSL Access Concentrator Name", # not part of RFC
    255: "Magic Padding", # not part of RFC
}

grecp_filterlistitem_types = {
    1: "FQDN",
    2: "DSCP",
    3: "Destination Port",
    4: "Destination IP",
    5: "Destination IP & Port",
    6: "Source Port",
    7: "Source IP",
    8: "Source IP & Port",
    9: "Source MAC",
    10: "Protocol",
    11: "Source IP Range",
    12: "Destination IP Range",
    13: "Source IP Range & Port",
    14: "Destination IP Range & Port",
}

class GRECPFilterListItem(Packet):
    fields_desc = [
        ShortEnumField("type", None, grecp_filterlistitem_types),
        ShortField("length", None),
        ShortField("enable", None),
        ShortField("descriptionLength", None),
        StrFixedLenField("descriptionValue", None, length_from = lambda pkt:pkt.descriptionLength),
        StrFixedLenField("value", None, length_from = lambda pkt:pkt.length - pkt.descriptionLength - 4),
    ]
    def guess_payload_class(self, p):
        if len(p) >= 8:
            return conf.padding_layer

class GRECPAttribute(Packet):
    fields_desc = [
        ByteEnumField("id", None, grecp_attribute_types),
        ShortField("length", None),
        ConditionalField(IPField("valueIP", None), lambda pkt:pkt.id == 1),
        ConditionalField(IP6Field("valueIP6", None), lambda pkt:pkt.id in [2, 13, 21]),
        ConditionalField(IntField("valueInt", None), lambda pkt:pkt.id in [4, 6, 7, 9, 10, 14, 15, 16, 17, 20, 22, 23, 24, 25, 31, 32]),
        ConditionalField(ByteField("valueIPMask", None), lambda pkt:pkt.id in [13, 21]),
        ConditionalField(IntField("valueIntHigh", None), lambda pkt:pkt.id == 5),
        ConditionalField(IntField("valueIntLow", None), lambda pkt:pkt.id == 5),
        # Filter List Package (+ACK) is special
        ConditionalField(IntField("filterListCommitCount", None), lambda pkt:pkt.id in [8, 30]),
        ConditionalField(ShortField("filterListPacketSum", None), lambda pkt:pkt.id == 8),
        ConditionalField(ShortField("filterListPacketId", None), lambda pkt:pkt.id == 8),
        ConditionalField(PacketListField("filterListItems", None, GRECPFilterListItem, length_from = lambda pkt:pkt.length - 8), lambda pkt:pkt.id == 8),
        ConditionalField(ByteField("filterListAckCode", None), lambda pkt:pkt.id == 30),
        # default to string with dynamic length
        ConditionalField(StrFixedLenField("valueStr", None, length_from = lambda pkt:pkt.length), lambda pkt:pkt.length > 0 and pkt.id not in [1, 2, 4, 5, 6, 7, 8, 9, 10, 13, 14, 15, 16, 17, 20, 21, 22, 23, 24, 25, 30, 31, 32]),
    ]
    def guess_payload_class(self, p):
        if len(p) >= 3:
            return conf.padding_layer

class GRECP(Packet):
    fields_desc = [
        BitEnumField("msgType", None, 4, grecp_message_types),
        BitField("tType", None, 4),
        PacketListField("attributes", None, GRECPAttribute),
    ]

def callback(pkt):
    print("###[ Timestamp ]###")
    print(datetime.datetime.fromtimestamp(pkt.time).strftime('%Y-%d-%m %H:%M:%S.%f'))
    pkt.show();

def callback_replay(pkt):
    if pkt[IPv6].src.startswith("2003:6:5"):
        r = pkt[IPv6]
        r.dst = sys.argv[2]
        r.show()
        send(r)

def main():
    if len(sys.argv) < 2:
        print("Usage: " + sys.argv[0] + " pcapfile|interface [replay-target]")
        exit(1)

    bind_layers(GRE, GRECP, proto=0x0101) # RFC says it's 0xb7ea

    if (os.path.isfile(sys.argv[1])):
        if (len(sys.argv)) == 2:
            sniff(offline=sys.argv[1], prn=callback, lfilter=lambda p: p.haslayer(IPv6) and p.haslayer(GRE) and p.haslayer(GRECP))
        else:
            sniff(offline=sys.argv[1], prn=callback_replay, lfilter=lambda p: p.haslayer(IPv6) and p.haslayer(GRE) and p.haslayer(GRECP))
    else:
        # Not a file? Must be an interface then! Surely it's not a typo or permission issue...
        sniff(prn=callback, lfilter=lambda p: p.haslayer(IPv6) and p.haslayer(GRE) and p.haslayer(GRECP), iface=sys.argv[1])

main()
