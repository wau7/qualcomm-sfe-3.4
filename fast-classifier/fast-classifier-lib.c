#include <netlink/genl/genl.h>
#include <netlink/genl/ctrl.h>
#include <errno.h>
#include <stdio.h>
#include <arpa/inet.h>

#include "fast-classifier-priv.h"

static struct nl_sock *sock;
static struct nl_sock *sock_event;
static int family;
static int grp_id;

static struct nla_policy fast_classifier_genl_policy[FAST_CLASSIFIER_A_MAX + 1] = {
	[FAST_CLASSIFIER_A_TUPLE] = { .type = NLA_UNSPEC },
};

void dump_fc_tuple(struct fast_classifier_tuple *fc_msg) {
	char src_str[INET_ADDRSTRLEN];
	char dst_str[INET_ADDRSTRLEN];

	printf("TUPLE: %d, %s, %s, %d, %d\n", fc_msg->proto,
				inet_ntop(AF_INET, &(fc_msg->src_saddr),  src_str, INET_ADDRSTRLEN),
				inet_ntop(AF_INET, &(fc_msg->dst_saddr),  dst_str, INET_ADDRSTRLEN),
				fc_msg->sport, fc_msg->dport);
}

static int parse_cb(struct nl_msg *msg, void *arg) {
	struct nlmsghdr *nlh = nlmsg_hdr(msg);
	struct genlmsghdr *gnlh = nlmsg_data(nlh);
	struct nlattr *attrs[FAST_CLASSIFIER_A_MAX];

	genlmsg_parse(nlh, 0, attrs, FAST_CLASSIFIER_A_MAX, fast_classifier_genl_policy);

	switch (gnlh->cmd) {
	case FAST_CLASSIFIER_C_OFFLOADED:
		printf("Got a offloaded message\n");
		dump_fc_tuple(nla_data(attrs));
		return NL_OK;
	case FAST_CLASSIFIER_C_DONE:
		printf("Got a done message\n");
		dump_fc_tuple(nla_data(attrs));
		return NL_OK;
	}

	return NL_SKIP;
}

int fast_classifier_init() {
	int err;

	sock = nl_socket_alloc();
	if (sock == NULL) {
		printf("Unable to allocation socket.\n");
		return -1;
	}
	genl_connect(sock);

	sock_event = nl_socket_alloc();
	if (sock_event == NULL) {
	        nl_close(sock);
		nl_socket_free(sock);
		printf("Unable to allocation socket.\n");
		return -1;
	}
	genl_connect(sock_event);

	family = genl_ctrl_resolve(sock, FAST_CLASSIFIER_GENL_NAME);
	if (family < 0) {
		nl_close(sock_event);
	        nl_close(sock);
		nl_socket_free(sock);
		nl_socket_free(sock_event);
		printf("Unable to resolve family\n");
		return -1;
	}

	grp_id = genl_ctrl_resolve_grp(sock, FAST_CLASSIFIER_GENL_NAME,
					FAST_CLASSIFIER_GENL_MCGRP);
	if (grp_id < 0) {
		printf("Unable to resolve mcast group\n");
		return -1;
	}

	err = nl_socket_add_membership(sock_event, grp_id);
	if (err < 0) {
		printf("Unable to add membership\n");
		return -1;
	}

	nl_socket_disable_seq_check(sock_event);
	nl_socket_modify_cb(sock_event, NL_CB_VALID, NL_CB_CUSTOM, parse_cb, NULL);

	return 0;
}

void fast_classifier_close() {
	nl_close(sock_event);
        nl_close(sock);
	nl_socket_free(sock_event);
        nl_socket_free(sock);
}

void fast_classifier_ipv4_offload(unsigned char proto, unsigned long src_saddr,
					 unsigned long dst_saddr, unsigned short sport,
					 unsigned short dport) {
	struct nl_msg *msg;
	int ret;
#ifdef DEBUG
	char src_str[INET_ADDRSTRLEN];
	char dst_str[INET_ADDRSTRLEN];
#endif
	struct fast_classifier_tuple fc_msg;

#ifdef DEBUG
	printf("DEBUG: would offload: %d, %s, %s, %d, %d\n", proto,
				inet_ntop(AF_INET, &src_saddr,  src_str, INET_ADDRSTRLEN),
				inet_ntop(AF_INET, &dst_saddr,  dst_str, INET_ADDRSTRLEN),
				sport, dport);
#endif

	fc_msg.proto = proto;
	fc_msg.src_saddr = src_saddr;
	fc_msg.dst_saddr = dst_saddr;
	fc_msg.sport = sport;
	fc_msg.dport = dport;

	if (fast_classifier_init() < 0) {
		printf("Unable to init generic netlink\n");
		exit(1);
	}

	msg = nlmsg_alloc();
	if (msg == NULL) {
		nl_socket_free(sock);
		printf("Unable to allocate message\n");
		return;
	}

        genlmsg_put(msg, NL_AUTO_PID, NL_AUTO_SEQ, family,
			FAST_CLASSIFIER_GENL_HDRSIZE, NLM_F_REQUEST,
			FAST_CLASSIFIER_C_OFFLOAD, FAST_CLASSIFIER_GENL_VERSION);
        nla_put(msg, 1, sizeof(fc_msg), &fc_msg);

        ret = nl_send_auto_complete(sock, msg);

        nlmsg_free(msg);
        if (ret < 0) {
                printf("nlmsg_free failed");
		nl_close(sock);
		nl_socket_free(sock);
                return;
        }

        ret = nl_wait_for_ack(sock);
        if (ret < 0) {
                printf("wait for ack failed");
		nl_close(sock);
		nl_socket_free(sock);
                return;
        }
}


void fast_classifier_listen_for_messages(void) {
	printf("waiting for netlink events\n");

	while (1) {
		nl_recvmsgs_default(sock_event);
	}
}
