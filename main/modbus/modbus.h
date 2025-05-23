#pragma once

#include "levitree_cpcd.pb-c.h"

#define BUF_SIZE (1024)
#define RD_BUF_SIZE (BUF_SIZE)

void modbus_init();
LevitreeCpcd__RoutableMessage* write_mb_packet(LevitreeCpcd__RoutableMessage* packet);
void write_mb_packet_no_response(LevitreeCpcd__RoutableMessage* packet);