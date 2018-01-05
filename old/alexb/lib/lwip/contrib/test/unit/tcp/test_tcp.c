#include "test_tcp.h"

#include "lwip/tcp_impl.h"
#include "lwip/stats.h"
#include "tcp_helper.h"

#if !LWIP_STATS || !TCP_STATS || !MEMP_STATS
#error "This tests needs TCP- and MEMP-statistics enabled"
#endif

/* Setups/teardown functions */

static void
tcp_setup(void)
{
  tcp_remove_all();
}

static void
tcp_teardown(void)
{
  netif_list = NULL;
  tcp_remove_all();
}


/* Test functions */

/** Call tcp_new() and tcp_abort() and test memp stats */
START_TEST(test_tcp_new_abort)
{
  struct tcp_pcb* pcb;
  LWIP_UNUSED_ARG(_i);

  fail_unless(lwip_stats.memp[MEMP_TCP_PCB].used == 0);

  pcb = tcp_new();
  fail_unless(pcb != NULL);
  if (pcb != NULL) {
    fail_unless(lwip_stats.memp[MEMP_TCP_PCB].used == 1);
    tcp_abort(pcb);
    fail_unless(lwip_stats.memp[MEMP_TCP_PCB].used == 0);
  }
}
END_TEST

/** Create an ESTABLISHED pcb and check if receive callback is called */
START_TEST(test_tcp_recv_inseq)
{
  struct test_tcp_counters counters;
  struct tcp_pcb* pcb;
  struct pbuf* p;
  char data[] = {1, 2, 3, 4};
  ip_addr_t remote_ip, local_ip;
  u16_t data_len;
  u16_t remote_port = 0x100, local_port = 0x101;
  struct netif netif;
  LWIP_UNUSED_ARG(_i);

  /* initialize local vars */
  memset(&netif, 0, sizeof(netif));
  IP4_ADDR(&local_ip, 192, 168, 1, 1);
  IP4_ADDR(&remote_ip, 192, 168, 1, 2);
  data_len = sizeof(data);
  /* initialize counter struct */
  memset(&counters, 0, sizeof(counters));
  counters.expected_data_len = data_len;
  counters.expected_data = data;

  /* create and initialize the pcb */
  pcb = test_tcp_new_counters_pcb(&counters);
  EXPECT_RET(pcb != NULL);
  tcp_set_state(pcb, ESTABLISHED, &local_ip, &remote_ip, local_port, remote_port);

  /* create a segment */
  p = tcp_create_rx_segment(pcb, counters.expected_data, data_len, 0, 0, 0);
  EXPECT(p != NULL);
  if (p != NULL) {
    /* pass the segment to tcp_input */
    test_tcp_input(p, &netif);
    /* check if counters are as expected */
    EXPECT(counters.close_calls == 0);
    EXPECT(counters.recv_calls == 1);
    EXPECT(counters.recved_bytes == data_len);
    EXPECT(counters.err_calls == 0);
  }

  /* make sure the pcb is freed */
  EXPECT(lwip_stats.memp[MEMP_TCP_PCB].used == 1);
  tcp_abort(pcb);
  EXPECT(lwip_stats.memp[MEMP_TCP_PCB].used == 0);
}
END_TEST

/** Provoke fast retransmission by duplicate ACKs and then recover by ACKing all sent data.
 * At the end, send more data. */
START_TEST(test_tcp_fast_retx_recover)
{
  struct netif netif;
  struct test_tcp_txcounters txcounters;
  struct test_tcp_counters counters;
  struct tcp_pcb* pcb;
  struct pbuf* p;
  char data1[] = { 1,  2,  3,  4};
  char data2[] = { 5,  6,  7,  8};
  char data3[] = { 9, 10, 11, 12};
  char data4[] = {13, 14, 15, 16};
  char data5[] = {17, 18, 19, 20};
  char data6[] = {21, 22, 23, 24};
  ip_addr_t remote_ip, local_ip, netmask;
  u16_t remote_port = 0x100, local_port = 0x101;
  err_t err;
  LWIP_UNUSED_ARG(_i);

  /* initialize local vars */
  IP4_ADDR(&local_ip,  192, 168,   1, 1);
  IP4_ADDR(&remote_ip, 192, 168,   1, 2);
  IP4_ADDR(&netmask,   255, 255, 255, 0);
  test_tcp_init_netif(&netif, &txcounters, &local_ip, &netmask);
  memset(&counters, 0, sizeof(counters));

  /* create and initialize the pcb */
  pcb = test_tcp_new_counters_pcb(&counters);
  EXPECT_RET(pcb != NULL);
  tcp_set_state(pcb, ESTABLISHED, &local_ip, &remote_ip, local_port, remote_port);
  pcb->mss = TCP_MSS;
  /* disable initial congestion window (we don't send a SYN here...) */
  pcb->cwnd = pcb->snd_wnd;
  //tcp_nagle_disable(pcb);

  /* send data1 */
  err = tcp_write(pcb, data1, sizeof(data1), TCP_WRITE_FLAG_COPY);
  EXPECT_RET(err == ERR_OK);
  err = tcp_output(pcb);
  EXPECT_RET(err == ERR_OK);
  EXPECT_RET(txcounters.num_tx_calls == 1);
  EXPECT_RET(txcounters.num_tx_bytes == sizeof(data1) + sizeof(struct tcp_hdr) + sizeof(struct ip_hdr));
  memset(&txcounters, 0, sizeof(txcounters));
 /* "recv" ACK for data1 */
  p = tcp_create_rx_segment(pcb, NULL, 0, 0, 4, TCP_ACK);
  EXPECT_RET(p != NULL);
  test_tcp_input(p, &netif);
  EXPECT_RET(txcounters.num_tx_calls == 0);
  EXPECT_RET(pcb->unacked == NULL);
  /* send data2 */
  err = tcp_write(pcb, data2, sizeof(data2), TCP_WRITE_FLAG_COPY);
  EXPECT_RET(err == ERR_OK);
  err = tcp_output(pcb);
  EXPECT_RET(err == ERR_OK);
  EXPECT_RET(txcounters.num_tx_calls == 1);
  EXPECT_RET(txcounters.num_tx_bytes == sizeof(data2) + sizeof(struct tcp_hdr) + sizeof(struct ip_hdr));
  memset(&txcounters, 0, sizeof(txcounters));
  /* duplicate ACK for data1 (data2 is lost) */
  p = tcp_create_rx_segment(pcb, NULL, 0, 0, 0, TCP_ACK);
  EXPECT_RET(p != NULL);
  test_tcp_input(p, &netif);
  EXPECT_RET(txcounters.num_tx_calls == 0);
  EXPECT_RET(pcb->dupacks == 1);
  /* send data3 */
  err = tcp_write(pcb, data3, sizeof(data3), TCP_WRITE_FLAG_COPY);
  EXPECT_RET(err == ERR_OK);
  err = tcp_output(pcb);
  EXPECT_RET(err == ERR_OK);
  /* nagle enabled, no tx calls */
  EXPECT_RET(txcounters.num_tx_calls == 0);
  EXPECT_RET(txcounters.num_tx_bytes == 0);
  memset(&txcounters, 0, sizeof(txcounters));
  /* 2nd duplicate ACK for data1 (data2 and data3 are lost) */
  p = tcp_create_rx_segment(pcb, NULL, 0, 0, 0, TCP_ACK);
  EXPECT_RET(p != NULL);
  test_tcp_input(p, &netif);
  EXPECT_RET(txcounters.num_tx_calls == 0);
  EXPECT_RET(pcb->dupacks == 2);
  /* queue data4, don't send it (unsent-oversize is != 0) */
  err = tcp_write(pcb, data4, sizeof(data4), TCP_WRITE_FLAG_COPY);
  EXPECT_RET(err == ERR_OK);
  /* 3nd duplicate ACK for data1 (data2 and data3 are lost) -> fast retransmission */
  p = tcp_create_rx_segment(pcb, NULL, 0, 0, 0, TCP_ACK);
  EXPECT_RET(p != NULL);
  test_tcp_input(p, &netif);
  //EXPECT_RET(txcounters.num_tx_calls == 1);
  EXPECT_RET(pcb->dupacks == 3);
  memset(&txcounters, 0, sizeof(txcounters));
  // TODO: check expected data?
  
  /* send data5, not output yet */
  err = tcp_write(pcb, data5, sizeof(data5), TCP_WRITE_FLAG_COPY);
  EXPECT_RET(err == ERR_OK);
  //err = tcp_output(pcb);
  //EXPECT_RET(err == ERR_OK);
  EXPECT_RET(txcounters.num_tx_calls == 0);
  EXPECT_RET(txcounters.num_tx_bytes == 0);
  memset(&txcounters, 0, sizeof(txcounters));
  {
    int i = 0;
    do
    {
      err = tcp_write(pcb, data6, TCP_MSS, TCP_WRITE_FLAG_COPY);
      i++;
    }while(err == ERR_OK);
    EXPECT_RET(err != ERR_OK);
  }
  //err = tcp_output(pcb);
  //EXPECT_RET(err == ERR_OK);
  EXPECT_RET(txcounters.num_tx_calls == 0);
  EXPECT_RET(txcounters.num_tx_bytes == 0);
  memset(&txcounters, 0, sizeof(txcounters));

  /* send even more data */
  err = tcp_write(pcb, data5, sizeof(data5), TCP_WRITE_FLAG_COPY);
  EXPECT_RET(err == ERR_OK);
  err = tcp_output(pcb);
  EXPECT_RET(err == ERR_OK);
  /* ...and even more data */
  err = tcp_write(pcb, data5, sizeof(data5), TCP_WRITE_FLAG_COPY);
  EXPECT_RET(err == ERR_OK);
  err = tcp_output(pcb);
  EXPECT_RET(err == ERR_OK);
  /* ...and even more data */
  err = tcp_write(pcb, data5, sizeof(data5), TCP_WRITE_FLAG_COPY);
  EXPECT_RET(err == ERR_OK);
  err = tcp_output(pcb);
  EXPECT_RET(err == ERR_OK);
  /* ...and even more data */
  err = tcp_write(pcb, data5, sizeof(data5), TCP_WRITE_FLAG_COPY);
  EXPECT_RET(err == ERR_OK);
  err = tcp_output(pcb);
  EXPECT_RET(err == ERR_OK);

  /* send ACKs for data2 and data3 */
  p = tcp_create_rx_segment(pcb, NULL, 0, 0, 12, TCP_ACK);
  EXPECT_RET(p != NULL);
  test_tcp_input(p, &netif);
  EXPECT_RET(txcounters.num_tx_calls == 0);

  /* ...and even more data */
  err = tcp_write(pcb, data5, sizeof(data5), TCP_WRITE_FLAG_COPY);
  EXPECT_RET(err == ERR_OK);
  err = tcp_output(pcb);
  EXPECT_RET(err == ERR_OK);
  /* ...and even more data */
  err = tcp_write(pcb, data5, sizeof(data5), TCP_WRITE_FLAG_COPY);
  EXPECT_RET(err == ERR_OK);
  err = tcp_output(pcb);
  EXPECT_RET(err == ERR_OK);

#if 0
  /* create expected segment */
  p1 = tcp_create_rx_segment(pcb, counters.expected_data, data_len, 0, 0, 0);
  EXPECT_RET(p != NULL);
  if (p != NULL) {
    /* pass the segment to tcp_input */
    test_tcp_input(p, &netif);
    /* check if counters are as expected */
    EXPECT_RET(counters.close_calls == 0);
    EXPECT_RET(counters.recv_calls == 1);
    EXPECT_RET(counters.recved_bytes == data_len);
    EXPECT_RET(counters.err_calls == 0);
  }
#endif
  /* make sure the pcb is freed */
  EXPECT_RET(lwip_stats.memp[MEMP_TCP_PCB].used == 1);
  tcp_abort(pcb);
  EXPECT_RET(lwip_stats.memp[MEMP_TCP_PCB].used == 0);
}
END_TEST


/** Create the suite including all tests for this module */
Suite *
tcp_suite(void)
{
  TFun tests[] = {
    test_tcp_new_abort,
    test_tcp_recv_inseq,
    test_tcp_fast_retx_recover,
  };
  return create_suite("TCP", tests, sizeof(tests)/sizeof(TFun), tcp_setup, tcp_teardown);
}
