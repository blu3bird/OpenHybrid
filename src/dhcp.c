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
#include <signal.h>
#include <sys/stat.h>

int udhcpc_pipe[2];
int udhcpc6_pipe[2];

char dhcp_script_path[23];
#define DHCP_SCRIPT_CONTENT "#!/bin/busybox sh\n"\
                    "if [ \"${1}\" = \"bound\" ]\n"\
                    "then\n"\
                    "    if [ -n \"${ip}\" ]\n"\
                    "    then\n"\
                    "        echo \"ip=${ip}\"\n"\
                    "        echo \"lease=${lease}\"\n"\
                    "    elif [ -n \"${ipv6prefix}\" ]\n"\
                    "    then\n"\
                    "        echo \"prefix_address=${ipv6prefix//\\/*/}\"\n"\
                    "        echo \"prefix_length=${ipv6prefix//*\\//}\"\n"\
                    "        echo \"lease=${ipv6prefix_lease}\"\n"\
                    "    fi\n"\
                    "fi"\

bool create_dhcp_script() {
    memcpy(&dhcp_script_path, "/tmp/openhybrid.XXXXXX", 22);
    int fd;
    if ((fd = mkstemp(dhcp_script_path)) > 0) {
        int s = write(fd, DHCP_SCRIPT_CONTENT, sizeof(DHCP_SCRIPT_CONTENT) - 1);
        close(fd);
        chmod(dhcp_script_path, S_IRUSR | S_IWUSR | S_IXUSR);
        if (s == sizeof(DHCP_SCRIPT_CONTENT) - 1)
            return true;
        else
            return false;
    } else
        return false;
}

bool delete_dhcp_script() {
    if (unlink(dhcp_script_path) == 0)
        return true;
    else
        return false;
}

pid_t start_udhcpc() {
    if (pipe(udhcpc_pipe) == -1) {
        logger(LOG_ERROR, "Failed to set up udhcpc pipe: %s\n", strerror(errno));
        return 0;
    }

    pid_t pid = fork();
    if (pid == -1) {
        logger(LOG_ERROR, "Start of udhcpc failed: %s\n", strerror(errno));
        return 0;
    } else if (pid == 0) {
        close(udhcpc_pipe[0]);
        dup2(udhcpc_pipe[1], STDOUT_FILENO);
        dup2(udhcpc_pipe[1], STDERR_FILENO);
        execl("/bin/busybox",
            "udhcpc",
            "-i", runtime.tunnel_interface_name,
            "-s", dhcp_script_path,
            "-n",
            "-q",
            "-f",
            "-C",
            (runtime.log_level >= LOG_CRAZYDEBUG) ? "-v" : NULL,
            NULL);
        exit(EXIT_FAILURE);
    } else {
        close(udhcpc_pipe[1]);
        logger(LOG_INFO, "Started udhcpc with pid %i.\n", pid);
        return pid;
    }
}

pid_t start_udhcpc6() {
    if (pipe(udhcpc6_pipe) == -1) {
        logger(LOG_ERROR, "Failed to set up udhcpc6 pipe: %s\n", strerror(errno));
        return 0;
    }

    pid_t pid = fork();
    if (pid == -1) {
        logger(LOG_ERROR, "Start of udhcpc failed: %s\n", strerror(errno));
        return 0;
    } else if (pid == 0) {
        close(udhcpc6_pipe[0]);
        dup2(udhcpc6_pipe[1], STDOUT_FILENO);
        dup2(udhcpc6_pipe[1], STDERR_FILENO);
        execl("/bin/busybox",
            "udhcpc6",
            "-i", runtime.tunnel_interface_name,
            "-s", dhcp_script_path,
            "-f",
            "-n",
            "-q",
            "-r", "no",
            "-d",
            (runtime.log_level >= LOG_CRAZYDEBUG) ? "-v" : NULL,
            NULL);
        exit(EXIT_FAILURE);
    } else {
        close(udhcpc6_pipe[1]);
        logger(LOG_INFO, "Started udhcpc6 with pid %i.\n", pid);
        return pid;
    }
}

void process_udhcpc_output() {
    void *buffer = calloc(1, MAX_UDHCPC_OUTPUT);
    if (read(udhcpc_pipe[0], buffer, MAX_UDHCPC_OUTPUT))  {
        char *line = strtok(buffer, "\n");
        while (line != NULL) {
            if (strncmp(line, "ip=", 3) == 0)
                inet_pton(AF_INET, line + 3, &runtime.dhcp.ip);
            else if (strncmp(line, "lease=", 6) == 0)
                runtime.dhcp.lease_time = atoi(line + 6);
            else
                logger(LOG_DEBUG, "%s\n", line);

            line = strtok(NULL, "\n");
        }

        if (runtime.dhcp.lease_time) {
            runtime.dhcp.lease_obtained = get_uptime().tv_sec;
            char straddr[INET_ADDRSTRLEN] = {};
            inet_ntop(AF_INET, &runtime.dhcp.ip, straddr, INET_ADDRSTRLEN);
            logger(LOG_INFO, "Obtained %s via udhcpc, valid for %u seconds.\n", straddr, runtime.dhcp.lease_time);
            trigger_event("dhcpup_ip");
        } else {
            logger(LOG_ERROR, "Obtaining an ip via udhcpc failed.\n");
        }
    } else {
        logger(LOG_ERROR, "Failed to read udhcpc output: %s\n", strerror(errno));
    }
    close(udhcpc_pipe[0]);
    free(buffer);
}

void process_udhcpc6_output() {
    void *buffer = calloc(1, MAX_UDHCPC_OUTPUT);
    if (read(udhcpc6_pipe[0], buffer, MAX_UDHCPC_OUTPUT))  {
        char *line = strtok(buffer, "\n");
        while (line != NULL) {
            if (strncmp(line, "prefix_address=", 15) == 0)
                inet_pton(AF_INET6, line + 15, &runtime.dhcp6.prefix_address);
            else if (strncmp(line, "prefix_length=", 14) == 0)
                runtime.dhcp6.prefix_length = atoi(line + 14);
            else if (strncmp(line, "lease=", 6) == 0)
                runtime.dhcp6.lease_time = atoi(line + 6);
            else
                logger(LOG_DEBUG, "%s\n", line);

            line = strtok(NULL, "\n");
        }

        if (runtime.dhcp6.lease_time) {
            runtime.dhcp6.lease_obtained = get_uptime().tv_sec;
            char straddr[INET6_ADDRSTRLEN] = {};
            inet_ntop(AF_INET6, &runtime.dhcp6.prefix_address, straddr, INET6_ADDRSTRLEN);
            logger(LOG_INFO, "Obtained %s/%u via udhcpc6, valid for %u seconds.\n", straddr, runtime.dhcp6.prefix_length, runtime.dhcp6.lease_time);
            trigger_event("dhcpup_ip6");
        } else {
            logger(LOG_ERROR, "Obtaining a prefix via udhcpc6 failed.\n");
        }
    } else {
        logger(LOG_ERROR, "Failed to read udhcpc6 output: %s\n", strerror(errno));
    }
    close(udhcpc6_pipe[0]);
    free(buffer);
}

bool kill_udhcpc() {
    if (runtime.dhcp.udhcpc_pid) {
        if (kill(runtime.dhcp.udhcpc_pid, SIGKILL) == 0)
            logger(LOG_INFO, "Killed udhcpc with pid %i.\n", runtime.dhcp.udhcpc_pid);
        else {
            logger(LOG_ERROR, "Failed to kill udhcpc with pid %i: %s\n", runtime.dhcp.udhcpc_pid, strerror(errno));
            return false;
        }
    }
    return true;
}

bool kill_udhcpc6() {
    if (runtime.dhcp.udhcpc_pid) {
        if (kill(runtime.dhcp.udhcpc_pid, SIGKILL) == 0)
            logger(LOG_INFO, "Killed udhcpc6 with pid %i.\n", runtime.dhcp6.udhcpc6_pid);
        else {
            logger(LOG_ERROR, "Failed to kill udhcpc6 with pid %i: %s\n", runtime.dhcp6.udhcpc6_pid, strerror(errno));
            return false;
        }
    }
    return true;
}