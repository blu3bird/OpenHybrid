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
#include "openhybrid.h"

bool send_grecpnotify_filterlistpackageack(uint8_t ackcode) {
    void *buffer = calloc(1, MAX_PKT_SIZE);
    int size = 0;

    uint32_t commitcount = htonl(runtime.haap.filter_list.commit_count);
    void *payload = calloc(1, sizeof(commitcount) + 1);
    memcpy(payload, &commitcount, sizeof(commitcount));
    memset(payload + sizeof(commitcount), ackcode, sizeof(ackcode));
    size += append_grecpattribute(buffer + size, GRECP_MSGATTR_FILTER_LIST_PACKAGE_ACK, sizeof(commitcount) + sizeof(ackcode), payload);
    free(payload);
    size += append_grecpattribute(buffer + size, GRECP_MSGATTR_PADDING, 0, NULL);

    bool res;
    if (send_grecpmessage(GRECP_MSGTYPE_NOTIFY, GRECP_TUNTYPE_LTE, buffer, size)) {
        logger(LOG_DEBUG, "Sent 'Filter List Package ACK' notify message for LTE tunnel.\n");
        logger_hexdump(LOG_CRAZYDEBUG, buffer, size, "Contents of notify message:\n");
        res = true;
    } else {
        logger(LOG_ERROR, "Sending 'Filter List Package ACK' notify message failed.\n");
        res = false;
    }

    free(buffer);
    return res;
}

bool send_grecpnotify_tunnelverify() {
    void *buffer = calloc(1, MAX_PKT_SIZE);
    int size = 0;

    size += append_grecpattribute(buffer + size, GRECP_MSGATTR_TUNNEL_VERIFICATION, 0, NULL);
    size += append_grecpattribute(buffer + size, GRECP_MSGATTR_PADDING, 0, NULL);

    bool res;
    if (send_grecpmessage(GRECP_MSGTYPE_NOTIFY, GRECP_TUNTYPE_LTE, buffer, size)) {
        logger(LOG_DEBUG, "Sent 'Tunnel Verification' notify message for LTE tunnel.\n");
        logger_hexdump(LOG_CRAZYDEBUG, buffer, size, "Contents of notify message:\n");
        res = true;
    } else {
        logger(LOG_ERROR, "Sending 'Tunnel Verification' notify message failed.\n");
        res = false;
    }

    free(buffer);
    return res;
}

bool send_grecpnotify_linkfailure(uint8_t tuntype) {
    void *buffer = calloc(1, MAX_PKT_SIZE);
    int size = 0;

    /* we can only signal the failure of the other tunnel, not our own */
    if (tuntype == GRECP_TUNTYPE_LTE) {
        size += append_grecpattribute(buffer + size, GRECP_MSGATTR_DSL_LINK_FAILURE, 0, NULL);
    } else {
        size += append_grecpattribute(buffer + size, GRECP_MSGATTR_LTE_LINK_FAILURE, 0, NULL);
    }
    size += append_grecpattribute(buffer + size, GRECP_MSGATTR_PADDING, 0, NULL);

    bool res;
    if (send_grecpmessage(GRECP_MSGTYPE_NOTIFY, tuntype, buffer, size)) {
        if (tuntype == GRECP_TUNTYPE_LTE) {
            logger(LOG_DEBUG, "Sent 'DSL Link Failure' notify message for LTE tunnel.\n");
        } else {
            logger(LOG_DEBUG, "Sent 'LTE Link Failure' notify message for DSL tunnel.\n");
        }
        logger_hexdump(LOG_CRAZYDEBUG, buffer, size, "Contents of notify message:\n");
        res = true;
    } else {
        if (tuntype == GRECP_TUNTYPE_LTE) {
            logger(LOG_ERROR, "Sending 'DSL Link Failure' notify message for LTE tunnel failed.\n");
        } else {
            logger(LOG_ERROR, "Sending 'LTE Link Failure' notify message for DSL tunnel failed.\n");
        }
        res = false;
    }

    free(buffer);
    return res;
}

bool send_grecpnotify_bypasstraffic(uint32_t kbit) {
    void *buffer = calloc(1, MAX_PKT_SIZE);
    int size = 0;

    uint32_t bypasstraffic = htonl(kbit);
    size += append_grecpattribute(buffer + size, GRECP_MSGATTR_BYPASS_TRAFFIC_RATE, sizeof(bypasstraffic), &bypasstraffic);
    size += append_grecpattribute(buffer + size, GRECP_MSGATTR_PADDING, 0, NULL);

    bool res;
    if (send_grecpmessage(GRECP_MSGTYPE_NOTIFY, GRECP_TUNTYPE_DSL, buffer, size)) {
        runtime.dsl.last_bypass_traffic_sent = get_uptime().tv_sec;
        logger(LOG_DEBUG, "Sent 'Bypass Traffic' notify message for DSL tunnel.\n");
        logger_hexdump(LOG_CRAZYDEBUG, buffer, size, "Contents of notify message:\n");
        res = true;
    } else {
        logger(LOG_ERROR, "Sending 'Bypass Traffic' notify message for DSL tunnel failed.\n");
        res = false;
    }

    free(buffer);
    return res;
}

void handle_grecpnotify(uint8_t tuntype, void *buffer, int size) {
    if (tuntype == GRECP_TUNTYPE_LTE) {
        logger(LOG_DEBUG, "Received notify message for LTE tunnel.\n");
    }  else {
        logger(LOG_DEBUG, "Received notify message for DSL tunnel.\n");
    }
    logger_hexdump(LOG_CRAZYDEBUG, buffer, size, "Contents of notify message:\n");

    struct grecpattr attr = {};
    int bytes_read = 0;
    while ((bytes_read = read_grecpattribute(buffer += bytes_read, size -= bytes_read, &attr)) > 0) {
        switch (attr.id) {
            case GRECP_MSGATTR_FILTER_LIST_PACKAGE:
                memcpy(&runtime.haap.filter_list.commit_count, attr.value, sizeof(runtime.haap.filter_list.commit_count));
                runtime.haap.filter_list.commit_count = ntohl(runtime.haap.filter_list.commit_count);
                runtime.filter_list_acked = false;
                break;
            case GRECP_MSGATTR_TUNNEL_VERIFICATION:
                runtime.lte.tunnel_verification_required = true;
                break;
            case GRECP_MSGATTR_BYPASS_TRAFFIC_RATE:
            case GRECP_MSGATTR_PADDING:
                break;
            default:
                logger(LOG_DEBUG, "Unimplemented attribute in notify message received: %u\n", attr.id);
                break;
        }
    }
}