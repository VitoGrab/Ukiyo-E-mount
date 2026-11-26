#pragma once
#include <stdint.h>
#define EM_ID_LOG_LEN 32
extern volatile uint32_t em_frames_rx;
extern volatile uint32_t em_frames_tx;
extern volatile uint32_t em_cs_edges;
extern volatile uint32_t em_vd_edges;
extern volatile uint32_t em_rx_skipped;
extern volatile uint16_t em_req_dropped;
extern volatile uint16_t em_rec_unknown;
extern volatile uint32_t em_t_cs_first_high;
extern volatile uint32_t em_t_first_frame;
extern volatile uint32_t em_t_init_done;
extern volatile uint32_t em_t_handshake;
extern volatile uint32_t em_t_first_status;
extern volatile uint8_t em_handshake_done;
extern volatile uint8_t em_last_id;
extern volatile uint8_t em_bad_frames;
extern volatile uint8_t em_id_log[EM_ID_LOG_LEN];
extern volatile uint8_t em_id_log_n;
void em_init(void);
void em_poll(void);
