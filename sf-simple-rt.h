/*
 * Copyright (c) 2016, Yasuyuki Tanaka
 * Copyright (c) 2016, Centre for Development of Advanced Computing (C-DAC).
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/**
 * \file
 *         A 6P Simple Schedule Function
 * \author
 *         Shalu R <shalur@cdac.in>
 *         Lijo Thomas <lijo@cdac.in>
 *         Yasuyuki Tanaka <yasuyuki.tanaka@inf.ethz.ch>sf-simple was used as base for developing RT
 * 
 * sf-simple was used as base for developing RTsed as base for developing RT
 *         Ivanilson Junior <ivanilson.junior@ifrn.edu.br>
 */

#ifndef _SIXTOP_SF_SIMPLE_RT_H_
#define _SIXTOP_SF_SIMPLE_RT_H_

#include "net/linkaddr.h"
#if ROUTING_CONF_RPL_LITE
#include "net/routing/rpl-lite/rpl.h"
#elif ROUTING_CONF_RPL_CLASSIC
#include "net/routing/rpl-classic/rpl.h"
#include "net/routing/rpl-classic/rpl-private.h"
#endif

int sf_simple_add_links(linkaddr_t *peer_addr, uint8_t num_links);
int sf_simple_remove_links(linkaddr_t *peer_addr);

//RippleTrickle definitions
int sf_rippletrickle_remove_links(linkaddr_t *peer_addr, uint8_t num_links);
int sf_rippletrickle_tx_amount_by_peer(linkaddr_t *peer_addr);
int sf_rippletrickle_rx_amount_by_peer(linkaddr_t *peer_addr);
int sf_rippletrickle_rx_amount();
int sf_rippletrickle_tx_amount();
int sf_rippletrickle_check();
int sf_rippletrickle_clean(linkaddr_t *peer_addr);
void rt_tsch_rpl_callback_parent_switch (rpl_parent_t *old, rpl_parent_t *new);

// SF Constants
#define SF_SIMPLE_MAX_LINKS  3
#define RTRICKLE_MAX_LINKS 5
#define RTRICKLE_DemandRate 2 //
#define MinTrickleThreshold 16
#define MidTrickleThreshold 18
#define MaxTrickleThreshold 20
#define MinRankThreshold 4
#define QueueThreshold 4
#define SF_SIMPLE_SFID       0xf0
extern const sixtop_sf_t sf_rt_driver;

#endif /* !_SIXTOP_SF_SIMPLE_RT_H_ */
