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

bool send_grecprequest(uint8_t tuntype) {
    unsigned char buffer[MAX_PKT_SIZE];
    int size = 0;

    if (tuntype == GRECP_TUNTYPE_LTE) {
        /* Speedports user their model name + firmware version */
        /* example: Speedport_Hybrid_050124_02_00_012 */
        size += append_grecpattribute(buffer + size, GRECP_MSGATTR_CLIENT_IDENTIFICATION_NAME, 40, "OpenHybrid\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0");
    }
    if (runtime.haap.session_id) {
        uint32_t sessionid = htonl(runtime.haap.session_id);
        size += append_grecpattribute(buffer + size, GRECP_MSGATTR_SESSION_ID, sizeof(sessionid), &sessionid);
    }
    size += append_grecpattribute(buffer + size, GRECP_MSGATTR_PADDING, 0, NULL);

    bool res;
    if (send_grecpmessage(GRECP_MSGTYPE_REQUEST, tuntype, buffer, size)) {
        if (tuntype == GRECP_TUNTYPE_LTE) {
            logger(LOG_DEBUG, "Sent request message for LTE tunnel.\n");
        }  else {
            logger(LOG_DEBUG, "Sent request message for DSL tunnel.\n");
        }
        logger_hexdump(LOG_CRAZYDEBUG, buffer, size, "Contents of request message:\n");
        res = true;
    } else {
        logger(LOG_ERROR, "Sending request message failed.\n");
        res = false;
    }

    return res;
}