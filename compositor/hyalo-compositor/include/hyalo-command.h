/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LABWC_HYALO_COMMAND_H
#define LABWC_HYALO_COMMAND_H

struct server;

void hyalo_command_bridge_init(struct server *server);
void hyalo_command_bridge_finish(struct server *server);

#endif /* LABWC_HYALO_COMMAND_H */