/* OpenHybrid - an open GRE tunnel bonding implemantion
 * Copyright (C) 2019  Friedrich Oslage <friedrich@oslage.de>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <linux/types.h>

/* Proto and flags are static */
#define GRECP_PROTO 0x0101 /* RFC says 0xbfea */
#define GRECP_FLAGSANDVERSION 0x2000 /* Key bit set, all other bits unset */
#define GRECP_FLAGSANDVERSION_WITH_SEQ 0x3000 /* Key bit set, sequence bit set, all other bits unset */

/* Message types */
#define GRECP_MSGTYPE_REQUEST 1
#define GRECP_MSGTYPE_ACCEPT 2
#define GRECP_MSGTYPE_DENY 3
#define GRECP_MSGTYPE_HELLO 4
#define GRECP_MSGTYPE_TEARDOWN 5
#define GRECP_MSGTYPE_NOTIFY 6

/* Message attributes */
#define GRECP_MSGATTR_H_IPV4_ADDRESS 1
#define GRECP_MSGATTR_H_IPV6_ADDRESS 2
#define GRECP_MSGATTR_CLIENT_IDENTIFICATION_NAME 3
#define GRECP_MSGATTR_SESSION_ID 4 /* RFC says this is a string...doesn't look like a string to me */
#define GRECP_MSGATTR_TIMESTAMP 5
#define GRECP_MSGATTR_BYPASS_TRAFFIC_RATE 6
#define GRECP_MSGATTR_DSL_SYNCHRONIZATION_RATE 7
#define GRECP_MSGATTR_FILTER_LIST_PACKAGE 8
#define GRECP_MSGATTR_RTT_DIFFERENCE_THRESHOLD 9
#define GRECP_MSGATTR_BYPASS_BANDWIDTH_CHECK_INTERVAL 10
#define GRECP_MSGATTR_SWITCHING_TO_DSL_TUNNEL 11
#define GRECP_MSGATTR_OVERFLOWING_TO_LTE_TUNNEL 12
#define GRECP_MSGATTR_IPV6_PREFIX_ASSIGNED_BY_HAAP 13
#define GRECP_MSGATTR_ACTIVE_HELLO_INTERVAL 14
#define GRECP_MSGATTR_HELLO_RETRY_TIMES 15
#define GRECP_MSGATTR_IDLE_TIMEOUT 16
#define GRECP_MSGATTR_ERROR_CODE 17
#define GRECP_MSGATTR_DSL_LINK_FAILURE 18
#define GRECP_MSGATTR_LTE_LINK_FAILURE 19
#define GRECP_MSGATTR_BONDING_KEY_VALUE 20
#define GRECP_MSGATTR_IPV6_PREFIX_ASSIGNED_TO_HOST 21
#define GRECP_MSGATTR_CONFIGURED_DSL_UPSTREAM_BANDWIDTH 22
#define GRECP_MSGATTR_CONFIGURED_DSL_DOWNSTREAM_BANDWIDTH 23
#define GRECP_MSGATTR_RTT_DIFFERENCE_THRESHOLD_VIOLATION 24
#define GRECP_MSGATTR_RTT_DIFFERENCE_THRESHOLD_COMPLIANCE 25
#define GRECP_MSGATTR_DIAGNOSTIC_START_BONDING_TUNNEL 26
#define GRECP_MSGATTR_DIAGNOSTIC_START_DSL_TUNNEL 27
#define GRECP_MSGATTR_DIAGNOSTIC_START_LTE_TUNNEL 28
#define GRECP_MSGATTR_DIAGNOSTIC_END 29
#define GRECP_MSGATTR_FILTER_LIST_PACKAGE_ACK 30
#define GRECP_MSGATTR_IDLE_HELLO_INTERVAL 31
#define GRECP_MSGATTR_NO_TRAFFIC_MONITORED_INTERVAL 32
#define GRECP_MSGATTR_SWITCHING_TO_ACTIVE_HELLO_STATE 33
#define GRECP_MSGATTR_SWITCHING_TO_IDLE_HELLO_STATE 34
#define GRECP_MSGATTR_TUNNEL_VERIFICATION 35
#define GRECP_MSGATTR_PADDING 255 /* 'Magic' padding, not part of RFC */

/* Tunnel types */
#define GRECP_TUNTYPE_LTE 0 /* RFC says 1 */
#define GRECP_TUNTYPE_DSL 8 /* RFC says 2 */

struct grehdr {
    uint16_t flags_and_version;
    uint16_t proto;
    uint32_t key;
};

struct grecphdr {
    uint8_t msgtype_and_tuntype;
};

struct grecpattr {
    uint8_t id;
    uint16_t length;
    void *value;
};

int read_grecpattribute(void *buffer, int size, struct grecpattr *attr);
int append_grecpattribute(void *buffer, uint8_t id, uint16_t length, void *value);
void process_grecpmessage(void *buffer, int size);
bool send_grecpmessage(uint8_t msgtype, uint8_t tuntype, void *attributes, int attributes_size);