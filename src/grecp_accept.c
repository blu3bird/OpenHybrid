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

void handle_grecpaccept(uint8_t tuntype, void *buffer, int size) {
    if (tuntype == GRECP_TUNTYPE_LTE) {
        logger(LOG_DEBUG, "Received accept message for LTE tunnel.\n");
    }  else {
        logger(LOG_DEBUG, "Received accept message for DSL tunnel.\n");
    }
    logger_hexdump(LOG_CRAZYDEBUG, buffer, size, "Contents of accept message:\n");

    struct grecpattr attr = {};
    int bytes_read = 0;
    while ((bytes_read = read_grecpattribute(buffer += bytes_read, size -= bytes_read, &attr)) > 0) {

        switch (attr.id) {
            case GRECP_MSGATTR_H_IPV6_ADDRESS:
                memcpy(&runtime.haap.ip, attr.value, attr.length);
                break;
            case GRECP_MSGATTR_SESSION_ID:
                memcpy(&runtime.haap.session_id, attr.value, attr.length);
                runtime.haap.session_id = ntohl(runtime.haap.session_id);
                break;
            case GRECP_MSGATTR_ACTIVE_HELLO_INTERVAL:
                if (!runtime.haap.active_hello_interval) {
                    memcpy(&runtime.haap.active_hello_interval, attr.value, attr.length);
                    runtime.haap.active_hello_interval = ntohl(runtime.haap.active_hello_interval);
                } else
                    logger(LOG_DEBUG, "Custom value for 'active hello interval' set, ignoring value pushed by server.\n");
                break;
            case GRECP_MSGATTR_HELLO_RETRY_TIMES:
                if (!runtime.haap.hello_retry_times) {
                    memcpy(&runtime.haap.hello_retry_times, attr.value, attr.length);
                    runtime.haap.hello_retry_times = ntohl(runtime.haap.hello_retry_times);
                } else
                    logger(LOG_DEBUG, "Custom value for 'hello retry times' set, ignoring value pushed by server.\n");
                break;
            case GRECP_MSGATTR_BONDING_KEY_VALUE:
                memcpy(&runtime.haap.bonding_key, attr.value, attr.length);
                runtime.haap.bonding_key = ntohl(runtime.haap.bonding_key);
                break;
            case GRECP_MSGATTR_BYPASS_BANDWIDTH_CHECK_INTERVAL:
                memcpy(&runtime.haap.bypass_bandwidth_check_interval, attr.value, attr.length);
                runtime.haap.bypass_bandwidth_check_interval = ntohl(runtime.haap.bypass_bandwidth_check_interval);
                break;
            case GRECP_MSGATTR_PADDING:
                break;
            default:
                logger(LOG_DEBUG, "Unimplemented attribute in accept message received: %u\n", attr.id);
                break;
        }
    }

    /* TODO: verify if we really are good to go before setting these */
    if (tuntype == GRECP_TUNTYPE_LTE) {
        runtime.lte.tunnel_established = true;
        runtime.lte.last_hello_sent = get_uptime().tv_sec;
        runtime.lte.last_hello_received = runtime.lte.last_hello_sent;
        logger(LOG_INFO, "LTE tunnel established.\n");
    } else {
        runtime.dsl.tunnel_established = true;
        runtime.dsl.last_hello_sent = get_uptime().tv_sec;
        runtime.dsl.last_hello_received = runtime.dsl.last_hello_sent;
        logger(LOG_INFO, "DSL tunnel established.\n");
    }
}