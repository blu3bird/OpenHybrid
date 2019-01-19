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

void read_config(char *path) {
    /* Defaults part 1 */
    runtime.log_level = LOG_INFO;
    memcpy(&runtime.lte.interface_name, "wwan0", 5);
    memcpy(&runtime.dsl.interface_name, "ppp0", 4);
    memcpy(&runtime.lte.gre_interface_name, "gre1", 4);
    memcpy(&runtime.dsl.gre_interface_name, "gre2", 4);

    FILE *fp = fopen(path, "r");
    if (!fp) {
        logger(LOG_FATAL, "Reading config file failed: %s\n", strerror(errno));
    }

    int read;
    char *line = NULL;
    size_t line_size = 0;
    while ((read = getline(&line, &line_size, fp)) != -1) {
        if (line[read-1] == '\n') {
            line[read-1] = 0;
        }
        if (line[0] == '#' ) {
            continue;
        }
        char *value = strstr(line, "=");
        if ((value != NULL) && (strlen(value) > 2)) {
            value += 2;
            if (strncmp(line, "haap anycast ip =", 17) == 0) {
                inet_pton(AF_INET6, value, &runtime.haap.anycast_ip);
            } else if (strncmp(line, "lte interface =", 15) == 0) {
                if (strlen(value) >= sizeof(runtime.lte.interface_name)) {
                    logger(LOG_FATAL, "Maximum length for 'lte interface' config is %i.\n", sizeof(runtime.lte.interface_name) - 1);
                }
                memset(&runtime.lte.interface_name, 0, sizeof(runtime.lte.interface_name));
                memcpy(&runtime.lte.interface_name, value, strlen(value));
            } else if (strncmp(line, "dsl interface =", 15) == 0) {
                if (strlen(value) >= sizeof(runtime.lte.interface_name)) {
                    logger(LOG_FATAL, "Maximum length for 'dsl interface' config is %i.\n", sizeof(runtime.dsl.interface_name) - 1);
                }
                memset(&runtime.dsl.interface_name, 0, sizeof(runtime.dsl.interface_name));
                memcpy(&runtime.dsl.interface_name, value, strlen(value));
            } else if (strncmp(line, "lte gre interface =", 19) == 0) {
                if (strlen(value) >= sizeof(runtime.lte.interface_name)) {
                    logger(LOG_FATAL, "Maximum length for 'lte gre interface' config is %i.\n", sizeof(runtime.lte.gre_interface_name) - 1);
                }
                memset(&runtime.lte.gre_interface_name, 0, sizeof(runtime.lte.gre_interface_name));
                memcpy(&runtime.lte.gre_interface_name, value, strlen(value));
            } else if (strncmp(line, "dsl gre interface =", 19) == 0) {
                if (strlen(value) >= sizeof(runtime.dsl.interface_name)) {
                    logger(LOG_FATAL, "Maximum length for 'dsl gre interface' config is %i.\n", sizeof(runtime.dsl.gre_interface_name) - 1);
                }
                memset(&runtime.dsl.gre_interface_name, 0, sizeof(runtime.dsl.gre_interface_name));
                memcpy(&runtime.dsl.gre_interface_name, value, strlen(value));
            } else if (strncmp(line, "bonding =", 9) == 0) {
                if (strcmp(value, "true") == 0) {
                    runtime.bonding = true;
                } else if (strcmp(value, "false") != 0) {
                    logger(LOG_WARNING, "Invalid bonding config '%s', falling back to 'false'.\n", value);
                }
            } else if (strncmp(line, "log level =", 11) == 0) {
                if (strcmp(value, "none") == 0) {
                    runtime.log_level = LOG_NONE;
                } if (strcmp(value, "error") == 0) {
                    runtime.log_level = LOG_ERROR;
                } if (strcmp(value, "critical") == 0) {
                    runtime.log_level = LOG_FATAL;
                } else if (strcmp(value, "warning") == 0) {
                    runtime.log_level = LOG_WARNING;
                } else if (strcmp(value, "info") == 0) {
                    runtime.log_level = LOG_INFO;
                } else if (strcmp(value, "debug") == 0) {
                    runtime.log_level = LOG_DEBUG;
                } else if (strcmp(value, "crazydebug") == 0) {
                    runtime.log_level = LOG_CRAZYDEBUG;
                } else {
                    logger(LOG_WARNING, "Invalid log level '%s', falling back to 'warning'.\n", value);
                }
            } else if (strncmp(line, "gre interface mtu =", 19) == 0) {
                if (atoi(value) < 1280) { /* IPV6_MIN_MTU */
                    logger(LOG_FATAL, "Minimum size for 'gre interface mtu' config is 1280.\n");
                } else if (atoi(value) > 1448) {
                    logger(LOG_FATAL, "Maximum size for 'gre interface mtu' config is 1448.\n");
                }
                runtime.gre_interface_mtu = atoi(value);
            } else if (strncmp(line, "active hello interval =", 23) == 0) {
                runtime.haap.active_hello_interval = atoi(value);
            } else if (strncmp(line, "hello retry times =", 19) == 0) {
                runtime.haap.hello_retry_times = atoi(value);
            } else if (strncmp(line, "event script path =", 19) == 0) {
                memset(&runtime.event_script_path, 0, sizeof(runtime.event_script_path));
                memcpy(&runtime.event_script_path, value, strlen(value));
            } else {
                logger(LOG_WARNING, "Ignoring invalid line in config file: %s\n", line);
            }
        } else if (strlen(line) > 1) {
                logger(LOG_WARNING, "Ignoring invalid line in config file: %s\n", line);
        }
    }
    free(line);
    fclose(fp);

    /* Defaults part 2 */
    if (memcmp(&runtime.haap.anycast_ip, "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", 16) == 0) {
        inet_pton(AF_INET6, "2003:6::1", &runtime.haap.anycast_ip);
    }
    runtime.haap.ip = runtime.haap.anycast_ip;
    if (!runtime.gre_interface_mtu) {
        runtime.gre_interface_mtu = 1448; /* 1500 - ipv6 header(40) - gre header(12) */
        if (runtime.bonding) {
            runtime.gre_interface_mtu -= 8; /* pppoe header */
        }
    }
}