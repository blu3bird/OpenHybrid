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
#include <signal.h>
#include <linux/filter.h>

void open_socket() {
    sockfd = socket(AF_INET6, SOCK_RAW, IPPROTO_GRE);
    if (sockfd < 0) {
        logger(LOG_FATAL, "Creation of raw socket failed: %s\n", strerror(errno));
    }

    /* Set timeout for blocking reads */
    struct timeval read_timeout = { .tv_sec = 1, .tv_usec = 000000 };
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &read_timeout, sizeof(read_timeout)) < 0) {
        logger(LOG_FATAL, "Configuration of raw socket failed: %s\n", strerror(errno));
    }

    /* BPF filter to exclude non control-plane messages */
    struct sock_filter bpfcode[] = {
        BPF_STMT(BPF_LD | BPF_H | BPF_ABS, 2), /* load gre->proto */
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, GRECP_PROTO, 0, 1), /* skip next line if it's != GRECP_PROTO  */
        BPF_STMT(BPF_RET | BPF_K, -1), /* accept packet */
        BPF_STMT(BPF_RET | BPF_K, 0), /* discard packet */
    };
    struct sock_fprog bpfprog = {
        .len = 4,
        .filter = bpfcode,
    };
    if (setsockopt(sockfd, SOL_SOCKET, SO_ATTACH_FILTER, &bpfprog, sizeof(bpfprog)) < 0) {
        logger(LOG_ERROR, "Attaching BPF failed: %s\n", strerror(errno));
    }
}

int close_socket() {
    return close(sockfd);
}

void execute_timers() {
    /* connect, if not connected */
    if (!runtime.lte.tunnel_established) {
        send_grecprequest(GRECP_TUNTYPE_LTE);
    } else if ((runtime.bonding) && (!runtime.dsl.tunnel_established)) {
        send_grecprequest(GRECP_TUNTYPE_DSL);
    }

    /* ack filter list, if unacked */
    if ((runtime.lte.tunnel_established) && (runtime.haap.filter_list.commit_count > 0) && (!runtime.filter_list_acked)) {
        /* TODO: implement list */
        runtime.filter_list_acked = send_grecpnotify_filterlistpackageack(2);
    }

    /* tunnel verification (aka hellos requested by server) */
    if ((runtime.lte.tunnel_established) && (runtime.lte.tunnel_verification_required)) {
        runtime.lte.tunnel_verification_required = !send_grecpnotify_tunnelverify();
    }

    /* send hello message, count missed hellos */
    if ((runtime.lte.tunnel_established) && (runtime.lte.last_hello_sent < get_uptime().tv_sec - runtime.haap.active_hello_interval)) {
        if (runtime.lte.last_hello_received != runtime.lte.last_hello_sent) {
            runtime.lte.missed_hellos++;
            logger(LOG_WARNING, "Missed %u consecutive hello message(s) for LTE tunnel.\n", runtime.lte.missed_hellos);
        }
        send_grecphello(GRECP_TUNTYPE_LTE);
    }
    if ((runtime.dsl.tunnel_established) && (runtime.dsl.last_hello_sent < get_uptime().tv_sec - runtime.haap.active_hello_interval)) {
        if (runtime.dsl.last_hello_received != runtime.dsl.last_hello_sent) {
            runtime.dsl.missed_hellos++;
            logger(LOG_WARNING, "Missed %u consecutive hello message(s) for DSL tunnel.\n", runtime.dsl.missed_hellos);
        }
        send_grecphello(GRECP_TUNTYPE_DSL);
    }

    /* hello message verification */
    if ((runtime.lte.tunnel_established) && (runtime.lte.missed_hellos >= runtime.haap.hello_retry_times)) {
        logger(LOG_ERROR, "Maximum allowed number of missed hello messages reached. Considering LTE tunnel dead.\n");
        runtime.lte.tunnel_established = false;
    }
    if ((runtime.dsl.tunnel_established) && (runtime.dsl.missed_hellos >= runtime.haap.hello_retry_times)) {
        logger(LOG_ERROR, "Maximum allowed number of missed hello messages reached. Considering DSL tunnel dead.\n");
        runtime.dsl.tunnel_established = false;
    }

    /* bypass bandwidth */
    if ((runtime.dsl.tunnel_established) && (runtime.dsl.last_bypass_traffic_sent < get_uptime().tv_sec - runtime.haap.bypass_bandwidth_check_interval)) {
        send_grecpnotify_bypasstraffic(10000); /* FIXME: calculate on demand */
    }

    /* reset stats in case of tear down, hello failure and such */
    if ((!runtime.lte.tunnel_established) && (runtime.tunnel_interface_created)) {
        runtime.lte.tunnel_established = false;
        runtime.lte.missed_hellos = 0;
        runtime.lte.last_hello_sent = 0;
        runtime.lte.last_hello_received = 0;
        runtime.lte.tunnel_verification_required = false;
    }
    if ((!runtime.dsl.tunnel_established) && (runtime.tunnel_interface_created)) {
        runtime.dsl.tunnel_established = false;
        runtime.dsl.missed_hellos = 0;
        runtime.dsl.last_hello_sent = 0;
        runtime.dsl.last_hello_received = 0;
        runtime.dsl.last_bypass_traffic_sent = 0;
    }
    if ((!runtime.lte.tunnel_established) && (!runtime.dsl.tunnel_established)) {
        runtime.haap.ip = runtime.haap.anycast_ip;
        runtime.haap.bonding_key = 0;
        runtime.haap.session_id = 0;

        runtime.haap.filter_list.commit_count = 0;
        runtime.filter_list_acked = false;

        if (runtime.dhcp.udhcpc_pid) {
            kill_udhcpc();
            runtime.dhcp.udhcpc_pid = 0;
        } else if (runtime.dhcp.lease_time)
            trigger_event("dhcpdown_ip");
        inet_pton(AF_INET, "0.0.0.0", &runtime.dhcp.ip);
        runtime.dhcp.lease_time = 0;
        runtime.dhcp.lease_obtained = 0;

        if (runtime.dhcp6.udhcpc6_pid) {
            kill_udhcpc6();
            runtime.dhcp6.udhcpc6_pid = 0;
        } else if (runtime.dhcp.lease_time)
            trigger_event("dhcpdown_ip6");
        inet_pton(AF_INET6, "::", &runtime.dhcp6.prefix_address);
        runtime.dhcp6.prefix_length = 0;
        runtime.dhcp6.lease_time = 0;
        runtime.dhcp6.lease_obtained = 0;
    }

    /* create/destroy tunnel devices */
    if (((runtime.lte.tunnel_established) || (runtime.dsl.tunnel_established)) && (!runtime.tunnel_interface_created)) {
        runtime.tunnel_interface_created = create_tunnel_dev();
    } else if ((!runtime.lte.tunnel_established) && (!runtime.dsl.tunnel_established) && (runtime.tunnel_interface_created)) {
        runtime.tunnel_interface_created = !destroy_tunnel_dev();
    }

    /* DHCP */
    if ((runtime.tunnel_interface_created) && (!runtime.dhcp.lease_time) && (!runtime.dhcp.udhcpc_pid))
        runtime.dhcp.udhcpc_pid = start_udhcpc();
    if (runtime.dhcp.udhcpc_pid) {
        int status;
        waitpid(runtime.dhcp.udhcpc_pid, &status, WNOHANG);
        if (WIFEXITED(status)) {
            runtime.dhcp.udhcpc_pid = 0;
            if (WEXITSTATUS(status) != 0) {
                logger(LOG_ERROR, "udhcpc exited with code '%i'.\n", WEXITSTATUS(status));
            } else {
                process_udhcpc_output();
            }
        }
    }
    /* DHCP6 */
    if ((runtime.tunnel_interface_created) && (!runtime.dhcp6.lease_time) && (!runtime.dhcp6.udhcpc6_pid))
        runtime.dhcp6.udhcpc6_pid = start_udhcpc6();
    if (runtime.dhcp6.udhcpc6_pid) {
        int status;
        waitpid(runtime.dhcp6.udhcpc6_pid, &status, WNOHANG);
        if (WIFEXITED(status)) {
            runtime.dhcp6.udhcpc6_pid = 0;
            if (WEXITSTATUS(status) != 0) {
                logger(LOG_ERROR, "udhcpc6 exited with code '%i'.\n", WEXITSTATUS(status));
            } else {
                process_udhcpc6_output();
            }
        }
    }

    /* DHCP: check if leases are still valid */
    if ((runtime.dhcp.lease_time) && (runtime.dhcp.lease_obtained + runtime.dhcp.lease_time <= get_uptime().tv_sec)) {
        char straddr[INET_ADDRSTRLEN] = {};
        inet_ntop(AF_INET, &runtime.dhcp.ip, straddr, INET_ADDRSTRLEN);
        logger(LOG_INFO, "Lease for %s obtained via udhcpc expired.\n", straddr);
        trigger_event("dhcpdown_ip");
        inet_pton(AF_INET, "0.0.0.0", &runtime.dhcp.ip);
        runtime.dhcp.lease_time = 0;
        runtime.dhcp.lease_obtained = 0;
    }
    if ((runtime.dhcp6.lease_time) && (runtime.dhcp6.lease_obtained + runtime.dhcp6.lease_time <= get_uptime().tv_sec)) {
        char straddr[INET6_ADDRSTRLEN] = {};
        inet_ntop(AF_INET6, &runtime.dhcp6.prefix_address, straddr, INET6_ADDRSTRLEN);
        logger(LOG_INFO, "Lease for %s/%u obtained via udhcpc6 expired.\n", straddr, runtime.dhcp6.prefix_length);
        trigger_event("dhcpdown_ip6");
        inet_pton(AF_INET, "0.0.0.0", &runtime.dhcp.ip);
        runtime.dhcp.lease_time = 0;
        runtime.dhcp.lease_obtained = 0;
    }

    /* handle signals */
    if (runtime.signal) {
        switch (runtime.signal) {
            case SIGINT:
            case SIGTERM:
                /* kill udhcp(s) */
                if (runtime.dhcp.udhcpc_pid)
                    kill_udhcpc();
                if (runtime.dhcp6.udhcpc6_pid)
                    kill_udhcpc6();
                if (runtime.dhcp.lease_time)
                    trigger_event("dhcpdown_ip");
                if (runtime.dhcp6.lease_time)
                    trigger_event("dhcpdown_ip6");

                /* remove interfaces */
                if (runtime.tunnel_interface_created)
                    destroy_tunnel_dev();

                /* Protocol doesn't support disconnect. We can exploit the 'link failure' notify message but that only works if both tunnels are up */
                if ((runtime.lte.tunnel_established) && (runtime.dsl.tunnel_established)) {
                    send_grecpnotify_linkfailure(GRECP_TUNTYPE_LTE);
                    send_grecpnotify_linkfailure(GRECP_TUNTYPE_DSL);
                } else if ((runtime.lte.tunnel_established) || (runtime.dsl.tunnel_established))
                    logger(LOG_WARNING, "Due to a limitation of RFC8157 the tunnel session will remain active on the server and you will not be able to reconnect until it times out (max 120 seconds).\n");

                close_socket();
                delete_dhcp_script();
                logger(LOG_INFO, "OpenHybrid stopped.\n");
                trigger_event("shutdown");
                exit(EXIT_SUCCESS);
                break;
            default:
                logger(LOG_WARNING, "Unhandled signal received: %i\n", runtime.signal);
                runtime.signal = 0;
        }
    }
}

void handle_signal(int sig) {
    switch (sig) {
        case SIGINT:
        case SIGTERM:
            logger(LOG_INFO, "Shutdown signal received.\n");
            break;
    }
    runtime.signal = sig;
}

int main(int argc, char **argv, char **envp) {
    if (argc != 2) {
        printf("Usage: %s /path/to/config.file\n", argv[0]);
        return(EXIT_FAILURE);
    }
    read_config(argv[1]);
   
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    create_dhcp_script();
    open_socket();
    logger(LOG_INFO, "OpenHybrid started.\n");
    trigger_event("startup");
    while (true) {
        void *buffer = malloc(MAX_PKT_SIZE);
        struct sockaddr_in6 saddr = {};
        socklen_t saddr_size = sizeof(saddr);
        int size = recvfrom(sockfd, buffer, MAX_PKT_SIZE, 0, (struct sockaddr *)&saddr, &saddr_size);
        if (size < 0) {
            if ((errno != EAGAIN) && (errno != EINTR))
                logger(LOG_ERROR, "Raw socket receive failed: %s\n", strerror(errno));
        } else {
            if (memcmp(&saddr.sin6_addr, &runtime.haap.ip, sizeof(struct in6_addr)) != 0) {
                /* ignore packets with invalid source ips */
            } else
                process_grecpmessage(buffer, size);
        }
        free(buffer);

        execute_timers();
    }
}
