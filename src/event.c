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
#include <sys/wait.h>

#define MAX_ENV_VARS 64
#define MAX_ENV_VAR_LENGTH 128

void trigger_event(char *name) {
    if (strlen(runtime.event_script_path) == 0)
        return;

    pid_t pid = fork();
    if (pid == -1) {
        logger(LOG_ERROR, "Triggering event '%s' failed: %s\n", name, strerror(errno));
    } else if (pid == 0) {
        pid = fork();
        if (pid == -1) {
            logger(LOG_ERROR, "Triggering event '%s' failed: %s\n", name, strerror(errno));
            exit(1);
        } else if (pid == 0) {
            char *env[MAX_ENV_VAR_LENGTH];
            char straddr[INET_ADDRSTRLEN] = {};
            char straddr6[INET6_ADDRSTRLEN] = {};

            /* this is a waste of memory, but I'm lazy */
            int i;
            for (i=0; i < MAX_ENV_VARS; i++) {
                env[i] = calloc(1, MAX_ENV_VAR_LENGTH);
            }
            i = 0;

            /* Interfaces */
            sprintf(env[i++], "lte_interface_name=%s", runtime.lte.interface_name);
            if (runtime.bonding) {
                sprintf(env[i++], "dsl_interface_name=%s", runtime.dsl.interface_name);
            }
            sprintf(env[i++], "tunnel_interface_name=%s", runtime.tunnel_interface_name);

            /* MTU */
            sprintf(env[i++], "tunnel_interface_mtu=%u", runtime.tunnel_interface_mtu);

            /* DHCP */
            inet_ntop(AF_INET, &runtime.dhcp.ip, straddr, INET_ADDRSTRLEN);
            sprintf(env[i++], "dhcp_ip=%s", straddr);
            sprintf(env[i++], "dhcp_lease_time=%u", runtime.dhcp.lease_time);

            /* DHCP6 */
            inet_ntop(AF_INET6, &runtime.dhcp6.prefix_address, straddr6, INET6_ADDRSTRLEN);
            sprintf(env[i++], "dhcp6_prefix_address=%s", straddr6);
            sprintf(env[i++], "dhcp6_prefix_length=%u", runtime.dhcp6.prefix_length);
            sprintf(env[i++], "dhcp6_lease_time=%u", runtime.dhcp6.lease_time);

            env[i++] = NULL;
            execle(runtime.event_script_path, runtime.event_script_path, name, NULL, env);
            exit(EXIT_FAILURE);
        } else {
            logger(LOG_DEBUG, "Triggered event '%s'.\n", name);
            exit(0);
        }
    } else
        waitpid(pid, NULL, 0);
}