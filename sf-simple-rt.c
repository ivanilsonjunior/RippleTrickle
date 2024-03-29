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
 *         Yasuyuki Tanaka <yasuyuki.tanaka@inf.ethz.ch>
 * 
 * sf-simple was used as base for developing RT
 *         Ivanilson Junior <ivanilson.junior@ifrn.edu.br>
 */

#include "contiki-lib.h"
#include "sys/node-id.h"
#include "lib/assert.h"
#include "net/mac/tsch/tsch.h"
#include "net/mac/tsch/sixtop/sixtop.h"
#include "net/mac/tsch/sixtop/sixp.h"
#include "net/mac/tsch/sixtop/sixp-nbr.h"
#include "net/mac/tsch/sixtop/sixp-pkt.h"
#include "net/mac/tsch/sixtop/sixp-trans.h"

#if ROUTING_CONF_RPL_LITE
#include "net/routing/rpl-lite/rpl.h"
#elif ROUTING_CONF_RPL_CLASSIC
#include "net/routing/rpl-classic/rpl.h"
#include "net/routing/rpl-classic/rpl-private.h"
#endif


#include "sf-simple-rt.h"

#define DEBUG DEBUG_PRINT
#include "net/net-debug.h"

#include "sys/log.h"
#define LOG_MODULE "6top"
#define LOG_LEVEL LOG_LEVEL_6TOP

#define SIXP_PKT_BUFLEN   128
static uint8_t sixp_pkg_data[SIXP_PKT_BUFLEN];

static const sixp_pkt_metadata_t pkt_metadata = 0x0de1;

typedef struct {
  uint16_t timeslot_offset;
  uint16_t channel_offset;
} sf_simple_cell_t;

static const uint16_t slotframe_handle = 0;
static uint8_t res_storage[4 + SF_SIMPLE_MAX_LINKS * 4];
static uint8_t req_storage[4 + SF_SIMPLE_MAX_LINKS * 4];

static void read_cell(const uint8_t *buf, sf_simple_cell_t *cell);
//static void print_cell_list(const uint8_t *cell_list, uint16_t cell_list_len);
static void add_links_to_schedule(const linkaddr_t *peer_addr,
                                  uint8_t link_option,
                                  const uint8_t *cell_list,
                                  uint16_t cell_list_len);
static void remove_links_to_schedule(const uint8_t *cell_list,
                                     uint16_t cell_list_len);
static void add_response_sent_callback(void *arg, uint16_t arg_len,
                                       const linkaddr_t *dest_addr,
                                       sixp_output_status_t status);
static void delete_response_sent_callback(void *arg, uint16_t arg_len,
                                          const linkaddr_t *dest_addr,
                                          sixp_output_status_t status);
static void add_req_input(const uint8_t *body, uint16_t body_len,
                          const linkaddr_t *peer_addr);
static void delete_req_input(const uint8_t *body, uint16_t body_len,
                             const linkaddr_t *peer_addr);
static void input(sixp_pkt_type_t type, sixp_pkt_code_t code,
                  const uint8_t *body, uint16_t body_len,
                  const linkaddr_t *src_addr);
static void request_input(sixp_pkt_cmd_t cmd,
                          const uint8_t *body, uint16_t body_len,
                          const linkaddr_t *peer_addr);
static void response_input(sixp_pkt_rc_t rc,
                           const uint8_t *body, uint16_t body_len,
                           const linkaddr_t *peer_addr);

/*
 * scheduling policy:
 * add: if and only if all the requested cells are available, accept the request
 * delete: if and only if all the requested cells are in use, accept the request
 */

static void
read_cell(const uint8_t *buf, sf_simple_cell_t *cell)
{
  cell->timeslot_offset = buf[0] + (buf[1] << 8);
  cell->channel_offset = buf[2] + (buf[3] << 8);
}

/*
static void
print_cell_list(const uint8_t *cell_list, uint16_t cell_list_len)
{
  uint16_t i;
  sf_simple_cell_t cell;

  for(i = 0; i < cell_list_len; i += sizeof(cell)) {
    read_cell(&cell_list[i], &cell);
    //PRINTF("sf-simple: %u ", cell.timeslot_offset);
  }
}*/

static void
add_links_to_schedule(const linkaddr_t *peer_addr, uint8_t link_option,
                      const uint8_t *cell_list, uint16_t cell_list_len)
{
  /* add only the first valid cell */

  sf_simple_cell_t cell;
  struct tsch_slotframe *slotframe;
  int i;

  assert(cell_list != NULL);

  slotframe = tsch_schedule_get_slotframe_by_handle(slotframe_handle);

  if(slotframe == NULL) {
    return;
  }

  for(i = 0; i < cell_list_len; i += sizeof(cell)) {
    read_cell(&cell_list[i], &cell);
    if(cell.timeslot_offset == 0xffff) {
      continue;
    }

    LOG_INFO("RippleTrickle - sf-simple: Schedule link %d as %s with node ",
           cell.timeslot_offset,
           link_option == LINK_OPTION_RX ? "RX" : "TX");
    LOG_INFO_LLADDR(peer_addr);
    LOG_INFO_("\n");
    tsch_schedule_add_link(slotframe,
                           link_option, LINK_TYPE_NORMAL, peer_addr,
                           cell.timeslot_offset, cell.channel_offset, 1);
    break;
  }
}

static void
remove_links_to_schedule(const uint8_t *cell_list, uint16_t cell_list_len)
{
  /* remove all the cells */

  sf_simple_cell_t cell;
  struct tsch_slotframe *slotframe;
  int i;

  assert(cell_list != NULL);

  slotframe = tsch_schedule_get_slotframe_by_handle(slotframe_handle);

  if(slotframe == NULL) {
    return;
  }

  for(i = 0; i < cell_list_len; i += sizeof(cell)) {
    read_cell(&cell_list[i], &cell);
    if(cell.timeslot_offset == 0xffff) {
      continue;
    }

    tsch_schedule_remove_link_by_timeslot(slotframe,
                                          cell.timeslot_offset,
                                          cell.channel_offset);
    LOG_INFO("RippleTrickle - sf-simple: Removing link %d \n", cell.timeslot_offset);
  }
}

static void
add_response_sent_callback(void *arg, uint16_t arg_len,
                           const linkaddr_t *dest_addr,
                           sixp_output_status_t status)
{
  uint8_t *body = (uint8_t *)arg;
  uint16_t body_len = arg_len;
  const uint8_t *cell_list;
  uint16_t cell_list_len;
  sixp_nbr_t *nbr;

  assert(body != NULL && dest_addr != NULL);

  if(status == SIXP_OUTPUT_STATUS_SUCCESS &&
     sixp_pkt_get_cell_list(SIXP_PKT_TYPE_RESPONSE,
                            (sixp_pkt_code_t)(uint8_t)SIXP_PKT_RC_SUCCESS,
                            &cell_list, &cell_list_len,
                            body, body_len) == 0 &&
     (nbr = sixp_nbr_find(dest_addr)) != NULL) {
    add_links_to_schedule(dest_addr, LINK_OPTION_RX,
                          cell_list, cell_list_len);
  }
}

static void
delete_response_sent_callback(void *arg, uint16_t arg_len,
                              const linkaddr_t *dest_addr,
                              sixp_output_status_t status)
{
  uint8_t *body = (uint8_t *)arg;
  uint16_t body_len = arg_len;
  const uint8_t *cell_list;
  uint16_t cell_list_len;
  sixp_nbr_t *nbr;

  assert(body != NULL && dest_addr != NULL);

  if(status == SIXP_OUTPUT_STATUS_SUCCESS &&
     sixp_pkt_get_cell_list(SIXP_PKT_TYPE_RESPONSE,
                            (sixp_pkt_code_t)(uint8_t)SIXP_PKT_RC_SUCCESS,
                            &cell_list, &cell_list_len,
                            body, body_len) == 0 &&
     (nbr = sixp_nbr_find(dest_addr)) != NULL) {
    remove_links_to_schedule(cell_list, cell_list_len);
  }
  //printf("Ninho - Mandou Apagar: ");
  //  print_cell_list(cell_list, cell_list_len);
  //printf("\n");
}

//static void
//print_cell_list(const uint8_t *cell_list, uint16_t cell_list_len)
//{
//  uint16_t i;
//  sf_simple_cell_t cell;
//
//  for(i = 0; i < cell_list_len; i += sizeof(cell)) {
//    read_cell(&cell_list[i], &cell);
//    PRINTF(" %u, ", cell.timeslot_offset);
//  }
//}

static void
add_req_input(const uint8_t *body, uint16_t body_len, const linkaddr_t *peer_addr)
{
  uint8_t i;
  sf_simple_cell_t cell;
  struct tsch_slotframe *slotframe;
  int feasible_link;
  uint8_t num_cells;
  const uint8_t *cell_list;
  uint16_t cell_list_len;
  uint16_t res_len;

  assert(body != NULL && peer_addr != NULL);

  if(sixp_pkt_get_num_cells(SIXP_PKT_TYPE_REQUEST,
                            (sixp_pkt_code_t)(uint8_t)SIXP_PKT_CMD_ADD,
                            &num_cells,
                            body, body_len) != 0 ||
     sixp_pkt_get_cell_list(SIXP_PKT_TYPE_REQUEST,
                            (sixp_pkt_code_t)(uint8_t)SIXP_PKT_CMD_ADD,
                            &cell_list, &cell_list_len,
                            body, body_len) != 0) {
    LOG_ERR("sf-simple: Parse error on add request\n");
    return;
  }

  //PRINTF("sf-simple: Received a 6P Add Request for %d links from node ",
  //       num_cells);
  //PRINTLLADDR((uip_lladdr_t *)peer_addr);
  //PRINTF(" with LinkList : ");
  //print_cell_list(cell_list, cell_list_len);
  //PRINTF("\n");

  slotframe = tsch_schedule_get_slotframe_by_handle(slotframe_handle);
  if(slotframe == NULL) {
    return;
  }

  if(num_cells > 0 && cell_list_len > 0) {
    memset(res_storage, 0, sizeof(res_storage));
    res_len = 0;

    /* checking availability for requested slots */
    for(i = 0, feasible_link = 0;
        i < cell_list_len && feasible_link < num_cells;
        i += sizeof(cell)) {
      read_cell(&cell_list[i], &cell);
      if(tsch_schedule_get_link_by_timeslot(slotframe,
                                            cell.timeslot_offset,
                                            cell.channel_offset) == NULL) {
        sixp_pkt_set_cell_list(SIXP_PKT_TYPE_RESPONSE,
                               (sixp_pkt_code_t)(uint8_t)SIXP_PKT_RC_SUCCESS,
                               (uint8_t *)&cell, sizeof(cell),
                               feasible_link,
                               res_storage, sizeof(res_storage));
        res_len += sizeof(cell);
        feasible_link++;
      }
    }

    if(feasible_link == num_cells) {
      /* Links are feasible. Create Link Response packet */
      sixp_output(SIXP_PKT_TYPE_RESPONSE,
                  (sixp_pkt_code_t)(uint8_t)SIXP_PKT_RC_SUCCESS,
                  SF_SIMPLE_SFID,
                  res_storage, res_len, peer_addr,
                  add_response_sent_callback, res_storage, res_len);
    }
  }
}

static void
delete_req_input(const uint8_t *body, uint16_t body_len,
                 const linkaddr_t *peer_addr)
{
  uint8_t i;
  sf_simple_cell_t cell;
  struct tsch_slotframe *slotframe;
  uint8_t num_cells;
  const uint8_t *cell_list;
  uint16_t cell_list_len;
  uint16_t res_len;
  int removed_link;

  assert(body != NULL && peer_addr != NULL);

  if(sixp_pkt_get_num_cells(SIXP_PKT_TYPE_REQUEST,
                            (sixp_pkt_code_t)(uint8_t)SIXP_PKT_CMD_DELETE,
                            &num_cells,
                            body, body_len) != 0 ||
     sixp_pkt_get_cell_list(SIXP_PKT_TYPE_REQUEST,
                            (sixp_pkt_code_t)(uint8_t)SIXP_PKT_CMD_DELETE,
                            &cell_list, &cell_list_len,
                            body, body_len) != 0) {
    return;
  }

  slotframe = tsch_schedule_get_slotframe_by_handle(slotframe_handle);
  if(slotframe == NULL) {
    return;
  }

  memset(res_storage, 0, sizeof(res_storage));
  res_len = 0;

  if(num_cells > 0 && cell_list_len > 0) {
    /* ensure before delete */
    for(i = 0, removed_link = 0; i < cell_list_len; i += sizeof(cell)) {
      read_cell(&cell_list[i], &cell);
      if(tsch_schedule_get_link_by_timeslot(slotframe,
                                            cell.timeslot_offset,
                                            cell.channel_offset) != NULL) {
        sixp_pkt_set_cell_list(SIXP_PKT_TYPE_RESPONSE,
                               (sixp_pkt_code_t)(uint8_t)SIXP_PKT_RC_SUCCESS,
                               (uint8_t *)&cell, sizeof(cell),
                               removed_link,
                               res_storage, sizeof(res_storage));
        res_len += sizeof(cell);
      }
    }
  }

  /* Links are feasible. Create Link Response packet */
  sixp_output(SIXP_PKT_TYPE_RESPONSE,
              (sixp_pkt_code_t)(uint8_t)SIXP_PKT_RC_SUCCESS,
              SF_SIMPLE_SFID,
              res_storage, res_len, peer_addr,
              delete_response_sent_callback, res_storage, res_len);
}

static void
count_req_input(const uint8_t *body, uint16_t body_len,
                 const linkaddr_t *peer_addr)
{
  //printf("Entrei no processamento:\n");
  //uint8_t i;
  //sf_simple_cell_t cell;
  struct tsch_slotframe *slotframe;
  //int feasible_link;
  //uint8_t num_cells;
  //const uint8_t *cell_list;
  //uint16_t cell_list_len;
  //uint16_t res_len;

  assert(body != NULL && peer_addr != NULL);

  /*if(sixp_pkt_get_num_cells(SIXP_PKT_TYPE_REQUEST,
                            (sixp_pkt_code_t)(uint8_t)SIXP_PKT_CMD_COUNT,
                            &num_cells,
                            body, body_len) != 0 ||
     sixp_pkt_get_cell_list(SIXP_PKT_TYPE_REQUEST,
                            (sixp_pkt_code_t)(uint8_t)SIXP_PKT_CMD_COUNT,
                            &cell_list, &cell_list_len,
                            body, body_len) != 0) {
    //PRINTF("sf-simple: Parse error on add request\n");
    return;
  }*/

  //PRINTF("sf-simple: Received a 6P Add Request for %d links from node ",
  //       num_cells);
  //PRINTLLADDR((uip_lladdr_t *)peer_addr);
  //PRINTF(" with LinkList : ");
  //print_cell_list(cell_list, cell_list_len);
  //PRINTF("\n");

  slotframe = tsch_schedule_get_slotframe_by_handle(slotframe_handle);
  if(slotframe == NULL) {
    return;
  }

  //struct tsch_slotframe *sf = tsch_schedule_slotframe_head();
  struct tsch_link *l = list_head(slotframe->links_list);
  int tsch_links = 0;
  //printf("Schedule: ");
  while(l != NULL) {
    //linkaddr_t *peer = &l->addr;
    //PRINTLLADDR((uip_lladdr_t *) peer);
    if (l->link_options == LINK_OPTION_TX) {
      //printf(" type: TX - ");
      tsch_links++;
    }
    if (l->link_options == LINK_OPTION_RX) {
      //printf(" type RX - ");
      tsch_links++;
    }
    l = list_item_next(l);
  }
  //printf("\n");
  //return tsch_links;

}

static void
clear_req_input(const uint8_t *body, uint16_t body_len,
                 const linkaddr_t *peer_addr)
{
  uint8_t i = 0;
  struct tsch_slotframe *sf =  tsch_schedule_get_slotframe_by_handle(slotframe_handle);
  struct tsch_link *l;

 
  sf_simple_cell_t cell;

  assert(peer_addr != NULL && sf != NULL);

  for(i = 0; i < TSCH_SCHEDULE_DEFAULT_LENGTH; i++) {
    l = tsch_schedule_get_link_by_timeslot(sf, i, 0);

    if(l) {
      if((linkaddr_cmp(&l->addr, peer_addr)) && (l->link_options == LINK_OPTION_TX)) {
        cell.timeslot_offset = i;
        cell.channel_offset = l->channel_offset;
        if(cell.timeslot_offset == 0xffff) {
          continue;
        }
        tsch_schedule_remove_link_by_timeslot(sf,
                                          cell.timeslot_offset,
                                          cell.channel_offset);
      }

    }
  }
   sixp_output(SIXP_PKT_TYPE_RESPONSE,
              (sixp_pkt_code_t)(uint8_t)SIXP_PKT_RC_SUCCESS,
              SF_SIMPLE_SFID, NULL, 0, peer_addr,
              NULL, NULL, 0);

}

static void
input(sixp_pkt_type_t type, sixp_pkt_code_t code,
      const uint8_t *body, uint16_t body_len, const linkaddr_t *src_addr)
{
  assert(body != NULL && body != NULL);
  switch(type) {
    case SIXP_PKT_TYPE_REQUEST:
      request_input(code.cmd, body, body_len, src_addr);
      break;
    case SIXP_PKT_TYPE_RESPONSE:
      response_input(code.rc, body, body_len, src_addr);
      break;
    default:
      /* unsupported */
      break;
  }
}

static void
request_input(sixp_pkt_cmd_t cmd,
              const uint8_t *body, uint16_t body_len,
              const linkaddr_t *peer_addr)
{
  assert(body != NULL && peer_addr != NULL);

  switch(cmd) {
    case SIXP_PKT_CMD_ADD:
      add_req_input(body, body_len, peer_addr);
      break;
    case SIXP_PKT_CMD_DELETE:
      delete_req_input(body, body_len, peer_addr);
      break;
    case SIXP_PKT_CMD_COUNT:
      //printf("Recebi um count no request:\n");
      count_req_input(body, body_len, peer_addr);
      break;
    case SIXP_PKT_CMD_CLEAR:
      //printf("Recebi um CLEAR no request:\n");
      clear_req_input(body, body_len, peer_addr);
      break;
    default:
      /* unsupported request */
      break;
  }
}
static void
response_input(sixp_pkt_rc_t rc,
               const uint8_t *body, uint16_t body_len,
               const linkaddr_t *peer_addr)
{
  const uint8_t *cell_list;
  uint16_t cell_list_len;
  sixp_nbr_t *nbr;
  sixp_trans_t *trans;

  assert(body != NULL && peer_addr != NULL);

  if((nbr = sixp_nbr_find(peer_addr)) == NULL ||
     (trans = sixp_trans_find(peer_addr)) == NULL) {
    return;
  }

  if(rc == SIXP_PKT_RC_SUCCESS) {
    switch(sixp_trans_get_cmd(trans)) {
      case SIXP_PKT_CMD_ADD:
        if(sixp_pkt_get_cell_list(SIXP_PKT_TYPE_RESPONSE,
                                  (sixp_pkt_code_t)(uint8_t)SIXP_PKT_RC_SUCCESS,
                                  &cell_list, &cell_list_len,
                                  body, body_len) != 0) {
          return;
        }
        add_links_to_schedule(peer_addr, LINK_OPTION_TX,
                              cell_list, cell_list_len);
        break;
      case SIXP_PKT_CMD_DELETE:
        if(sixp_pkt_get_cell_list(SIXP_PKT_TYPE_RESPONSE,
                                  (sixp_pkt_code_t)(uint8_t)SIXP_PKT_RC_SUCCESS,
                                  &cell_list, &cell_list_len,
                                  body, body_len) != 0) {
          return;
        }

        remove_links_to_schedule(cell_list, cell_list_len);
        break;
      case SIXP_PKT_CMD_COUNT:
      case SIXP_PKT_CMD_LIST:
      case SIXP_PKT_CMD_CLEAR:
      default:
        break;
        
    }
  }
}
/*---------------------------------------------------------------------------*/
/* Initiates a Sixtop Link addition
 */
int
sf_simple_add_links(linkaddr_t *peer_addr, uint8_t num_links)
{
  uint8_t i = 0, index = 0;
  struct tsch_slotframe *sf =
    tsch_schedule_get_slotframe_by_handle(slotframe_handle);

  uint8_t req_len;
  sf_simple_cell_t cell_list[SF_SIMPLE_MAX_LINKS];

  /* Flag to prevent repeated slots */
  uint8_t slot_check = 1;
  uint16_t random_slot = 0;

  assert(peer_addr != NULL && sf != NULL);

  do {
    /* Randomly select a slot offset within TSCH_SCHEDULE_DEFAULT_LENGTH */
    random_slot = ((random_rand() & 0xFF)) % TSCH_SCHEDULE_DEFAULT_LENGTH;

    if(tsch_schedule_get_link_by_timeslot(sf, random_slot, 0) == NULL) {

      /* To prevent repeated slots */
      for(i = 0; i < index; i++) {
        if(cell_list[i].timeslot_offset != random_slot) {
          /* Random selection resulted in a free slot */
          if(i == index - 1) { /* Checked till last index of link list */
            slot_check = 1;
            break;
          }
        } else {
          /* Slot already present in CandidateLinkList */
          slot_check++;
          break;
        }
      }

      /* Random selection resulted in a free slot, add it to linklist */
      if(slot_check == 1) {
        cell_list[index].timeslot_offset = random_slot;
        cell_list[index].channel_offset = 0;

        index++;
        slot_check++;
      } else if(slot_check > TSCH_SCHEDULE_DEFAULT_LENGTH) {
        LOG_ERR("RippleTrickle - sf-simple:! Number of trials for free slot exceeded...\n");
        return -1;
        break; /* exit while loop */
      }
    }
  } while(index < SF_SIMPLE_MAX_LINKS);

  /* Create a Sixtop Add Request. Return 0 if Success */
  if(index == 0 ) {
    return -1;
  }

  memset(req_storage, 0, sizeof(req_storage));
  if(sixp_pkt_set_cell_options(SIXP_PKT_TYPE_REQUEST,
                               (sixp_pkt_code_t)(uint8_t)SIXP_PKT_CMD_ADD,
                               SIXP_PKT_CELL_OPTION_TX,
                               req_storage,
                               sizeof(req_storage)) != 0 ||
     sixp_pkt_set_num_cells(SIXP_PKT_TYPE_REQUEST,
                            (sixp_pkt_code_t)(uint8_t)SIXP_PKT_CMD_ADD,
                            num_links,
                            req_storage,
                            sizeof(req_storage)) != 0 ||
     sixp_pkt_set_cell_list(SIXP_PKT_TYPE_REQUEST,
                            (sixp_pkt_code_t)(uint8_t)SIXP_PKT_CMD_ADD,
                            (const uint8_t *)cell_list,
                            index * sizeof(sf_simple_cell_t), 0,
                            req_storage, sizeof(req_storage)) != 0) {
    //PRINTF("sf-simple: Build error on add request\n");
    return -1;
  }

  /* The length of fixed part is 4 bytes: Metadata, CellOptions, and NumCells */
  req_len = 4 + index * sizeof(sf_simple_cell_t);
  sixp_output(SIXP_PKT_TYPE_REQUEST, (sixp_pkt_code_t)(uint8_t)SIXP_PKT_CMD_ADD,
              SF_SIMPLE_SFID,
              req_storage, req_len, peer_addr,
              NULL, NULL, 0);


  return 0;
}

/*---------------------------------------------------------------------------*/
/* Initiates a Sixtop Link deletion
 */
int
sf_simple_remove_links(linkaddr_t *peer_addr)
{
  uint8_t i = 0, index = 0;
  struct tsch_slotframe *sf =
    tsch_schedule_get_slotframe_by_handle(slotframe_handle);
  struct tsch_link *l;

  uint16_t req_len;
  sf_simple_cell_t cell;

  assert(peer_addr != NULL && sf != NULL);

  for(i = 0; i < TSCH_SCHEDULE_DEFAULT_LENGTH; i++) {
    l = tsch_schedule_get_link_by_timeslot(sf, i, 0);

    if(l) {
      /* Non-zero value indicates a scheduled link */
      if((linkaddr_cmp(&l->addr, peer_addr)) && (l->link_options == LINK_OPTION_TX)) {
        /* This link is scheduled as a TX link to the specified neighbor */
        cell.timeslot_offset = i;
        cell.channel_offset = l->channel_offset;
        index++;
        break;   /* delete atmost one */
      }
    }
  }

  if(index == 0) {
    return -1;
  }

  memset(req_storage, 0, sizeof(req_storage));
  if(sixp_pkt_set_num_cells(SIXP_PKT_TYPE_REQUEST,
                            (sixp_pkt_code_t)(uint8_t)SIXP_PKT_CMD_DELETE,
                            1,
                            req_storage,
                            sizeof(req_storage)) != 0 ||
     sixp_pkt_set_cell_list(SIXP_PKT_TYPE_REQUEST,
                            (sixp_pkt_code_t)(uint8_t)SIXP_PKT_CMD_DELETE,
                            (const uint8_t *)&cell, sizeof(cell),
                            0,
                            req_storage, sizeof(req_storage)) != 0) {

    return -1;
  }
  /* The length of fixed part is 4 bytes: Metadata, CellOptions, and NumCells */
  req_len = 4 + sizeof(sf_simple_cell_t);

  sixp_output(SIXP_PKT_TYPE_REQUEST, (sixp_pkt_code_t)(uint8_t)SIXP_PKT_CMD_DELETE,
              SF_SIMPLE_SFID,
              req_storage, req_len, peer_addr,
              NULL, NULL, 0);

  return 0;
}


/*---------------------------------------------------------------------------*/
/* Initiates a Sixtop Link deletion
 */
int
sf_rippletrickle_remove_links(linkaddr_t *peer_addr, uint8_t num_links)
{
  LOG_INFO("RippleTrickle - DELETE %u cells\n", num_links);
  uint8_t i = 0, index = 0;
  struct tsch_slotframe *sf =
    tsch_schedule_get_slotframe_by_handle(slotframe_handle);
  struct tsch_link *l;
  uint16_t req_len;
  sf_simple_cell_t cell;
  sf_simple_cell_t cell_list[SF_SIMPLE_MAX_LINKS];

  assert(peer_addr != NULL && sf != NULL);

  for(i = 0; i < TSCH_SCHEDULE_DEFAULT_LENGTH; i++) {
    l = tsch_schedule_get_link_by_timeslot(sf, i, 0);

    if(l) {
      /* Non-zero value indicates a scheduled link */
      if((linkaddr_cmp(&l->addr, peer_addr)) && (l->link_options == LINK_OPTION_TX)) {
        /* This link is scheduled as a TX link to the specified neighbor */
        cell.timeslot_offset = i;
        cell.channel_offset = l->channel_offset;
        cell_list[index] = cell;
        index++;
        if (index == num_links)  break;
      }
    }
  }

  if(index == 0) {
    return -1;
  }

  memset(req_storage, 0, sizeof(req_storage));
  if(sixp_pkt_set_num_cells(SIXP_PKT_TYPE_REQUEST,
                            (sixp_pkt_code_t)(uint8_t)SIXP_PKT_CMD_DELETE,
                            num_links,
                            req_storage,
                            sizeof(req_storage)) != 0 ||
     sixp_pkt_set_cell_list(SIXP_PKT_TYPE_REQUEST,
                            (sixp_pkt_code_t)(uint8_t)SIXP_PKT_CMD_DELETE,
                            (const uint8_t *)cell_list,
                            index * sizeof(sf_simple_cell_t),
                            0,
                            req_storage, sizeof(req_storage)) != 0) {

    return -1;
  }
  /* The length of fixed part is 4 bytes: Metadata, CellOptions, and NumCells */
  req_len = 4 + sizeof(sf_simple_cell_t);

  sixp_output(SIXP_PKT_TYPE_REQUEST, (sixp_pkt_code_t)(uint8_t)SIXP_PKT_CMD_DELETE,
              SF_SIMPLE_SFID,
              req_storage, req_len, peer_addr,
              NULL, NULL, 0);

  return 0;
}


int 
sf_rippletrickle_tx_amount()
{
  struct tsch_slotframe *sf = tsch_schedule_slotframe_head();
  struct tsch_link *l = list_head(sf->links_list);
  assert( l != NULL && sf != NULL);
  int tsch_links = 0;
  while(l != NULL) {
    if (l->link_options == LINK_OPTION_TX) {
      tsch_links++;
    } 
    l = list_item_next(l);
  }
  return tsch_links;
} 

int 
sf_rippletrickle_tx_amount_by_peer(linkaddr_t *peer_addr)
{
  struct tsch_slotframe *sf = tsch_schedule_slotframe_head();
  struct tsch_link *l = list_head(sf->links_list);
  assert( l != NULL && sf != NULL);
  int tsch_links = 0;
  while(l != NULL) {
    linkaddr_t *peer = &l->addr;
    if ( linkaddr_cmp(peer, peer_addr)) {
      if (l->link_options == LINK_OPTION_TX) {
      tsch_links++;
      }
    } 
    l = list_item_next(l);
  }
  return tsch_links;
} 

//Returns the number of RX cell with 
int 
sf_rippletrickle_rx_amount_by_peer(linkaddr_t *peer_addr)
{
  struct tsch_slotframe *sf = tsch_schedule_slotframe_head();
  struct tsch_link *l = list_head(sf->links_list);
  assert( l != NULL && sf != NULL);
  int tsch_links = 0;
  while(l != NULL) {
    if (l->link_options == LINK_OPTION_RX) {
      if ( linkaddr_cmp(&l->addr, peer_addr)) {
      tsch_links++;
      }
    }
    l = list_item_next(l);
  }
  //printf("Ninho: Retornando: %d\n",tsch_links);
  return tsch_links;
} 

int 
sf_rippletrickle_rx_amount()
{
  struct tsch_slotframe *sf = tsch_schedule_slotframe_head();
  struct tsch_link *l = list_head(sf->links_list);
  assert( l != NULL && sf != NULL);
  int tsch_links = 0;
  while(l != NULL) {
    if (l->link_options == LINK_OPTION_RX) {
      tsch_links++;
    }
    l = list_item_next(l);
  }
  return tsch_links;
} 



/*Check for inconsistences*/
int 
sf_rippletrickle_check()
{
  struct tsch_slotframe *sf = tsch_schedule_get_slotframe_by_handle(slotframe_handle);
  struct tsch_link *l = list_head(sf->links_list);
  assert( l != NULL && sf != NULL);
  while(l != NULL) {
    int quantidade = sf_rippletrickle_rx_amount_by_peer(&l->addr);
    if (quantidade > RTRICKLE_MAX_LINKS) {
      LOG_DBG("RippleTrickle - ");
      sixp_trans_t *trans = sixp_trans_find(&l->addr);
      if (trans != NULL) {
        LOG_DBG_("Transacion detected aborting.\n");
        return -1;
      }
      LOG_DBG_("Cleaning schedule with: ");
      LOG_DBG_LLADDR(&l->addr);
      LOG_DBG_("\n");
      sf_rippletrickle_clean(&l->addr);
      break;
    }
    l = list_item_next(l);
  }
 return 0;
}

/*Flush all RX cells and sent a 6p CLEAN to peer*/
int
sf_rippletrickle_clean(linkaddr_t *peer_addr)
{

  uint8_t i = 0;
  struct tsch_slotframe *sf =  tsch_schedule_get_slotframe_by_handle(slotframe_handle);
  struct tsch_link *l;
  sf_simple_cell_t cell;

  assert(peer_addr != NULL && sf != NULL);

  for(i = 0; i < TSCH_SCHEDULE_DEFAULT_LENGTH; i++) {
    l = tsch_schedule_get_link_by_timeslot(sf, i, 0);
    assert(l != NULL);
    if(l) {
      if((linkaddr_cmp(&l->addr, peer_addr)) && (l->link_options == LINK_OPTION_RX)) {
        cell.timeslot_offset = i;
        cell.channel_offset = l->channel_offset;
        if(cell.timeslot_offset == 0xffff) {
          continue;
        }
        tsch_schedule_remove_link_by_timeslot(sf,
                                          cell.timeslot_offset,
                                          cell.channel_offset);
      }
    }
  }
  memset(sixp_pkg_data, 0, sizeof(sixp_pkg_data)); 
  assert( sixp_pkt_set_metadata(SIXP_PKT_TYPE_REQUEST,
                               (sixp_pkt_code_t)(uint8_t)SIXP_PKT_CMD_CLEAR,
                               pkt_metadata,
                               sixp_pkg_data,
                               sizeof(sixp_pkg_data)) == 0);
  
  assert( sixp_output(SIXP_PKT_TYPE_REQUEST,
                                (sixp_pkt_code_t)(uint8_t)SIXP_PKT_CMD_CLEAR,
                                SF_SIMPLE_SFID,
                                sixp_pkg_data, sizeof(sixp_pkt_metadata_t),
                                peer_addr, NULL, NULL, 0) == 0);
  return 0;
}

void rt_tsch_rpl_callback_parent_switch (rpl_parent_t *old, rpl_parent_t *new) {
  /* Map the TSCH time source on the RPL preferred parent */
  if(tsch_is_associated == 1) {
    tsch_queue_update_time_source((const linkaddr_t *)uip_ds6_nbr_lladdr_from_ipaddr(rpl_parent_get_ipaddr(new)));
    if (old != NULL){
      const linkaddr_t *oldaddr;
      oldaddr =  (const linkaddr_t *) uip_ds6_nbr_lladdr_from_ipaddr(rpl_parent_get_ipaddr(old));
      sf_rippletrickle_clean((linkaddr_t *)oldaddr);
      LOG_INFO("RippleTrickle - Parent Switch, cleaning scheduling with old parent: ");
      LOG_INFO_LLADDR(oldaddr);
      LOG_INFO_("\n");
    }
  
  }
}

const sixtop_sf_t sf_rt_driver = {
  SF_SIMPLE_SFID,
  CLOCK_SECOND,
  NULL,
  input,
  NULL,
  NULL
};
