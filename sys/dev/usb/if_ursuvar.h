/*	$NetBS$	*/
/*	$OpenBSD: if_rsureg.h,v 1.2 2010/12/12 14:03:41 damien Exp $	*/

/*-
 * Copyright (c) 2010 Damien Bergamini <damien.bergamini@free.fr>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Driver definitions.
 */
#define RSU_RX_LIST_COUNT	1
#define RSU_TX_LIST_COUNT	(8 + 1)	/* NB: +1 for FW commands. */
#define RSU_HOST_CMD_RING_COUNT	32

#define RSU_RXBUFSZ	(8 * 1024)
#define RSU_TXBUFSZ	\
	((sizeof(struct r92s_tx_desc) + IEEE80211_MAX_LEN + 3) & ~3)

#define RSU_TX_TIMEOUT	5000	/* ms */
#define RSU_CMD_TIMEOUT	2000	/* ms */

struct ursu_rx_radiotap_header {
	struct ieee80211_radiotap_header wr_ihdr;
	uint8_t		wr_flags;
	uint8_t		wr_rate;
	uint16_t	wr_chan_freq;
	uint16_t	wr_chan_flags;
	uint8_t		wr_dbm_antsignal;
} __packed;

#define RSU_RX_RADIOTAP_PRESENT			\
	(1 << IEEE80211_RADIOTAP_FLAGS |	\
	 1 << IEEE80211_RADIOTAP_RATE |		\
	 1 << IEEE80211_RADIOTAP_CHANNEL |	\
	 1 << IEEE80211_RADIOTAP_DBM_ANTSIGNAL)

struct ursu_tx_radiotap_header {
	struct ieee80211_radiotap_header wt_ihdr;
	uint8_t		wt_flags;
	uint16_t	wt_chan_freq;
	uint16_t	wt_chan_flags;
} __packed;

#define RSU_TX_RADIOTAP_PRESENT			\
	(1 << IEEE80211_RADIOTAP_FLAGS |	\
	 1 << IEEE80211_RADIOTAP_CHANNEL)

/* Queue ids (used by soft only). */
#define RSU_QID_BCN	0
#define RSU_QID_MGT	1
#define RSU_QID_BMC	2
#define RSU_QID_VO	3
#define RSU_QID_VI	4
#define RSU_QID_BE	5
#define RSU_QID_BK	6
#define RSU_QID_RXOFF	7
#define RSU_QID_H2C	8
#define RSU_QID_C2H	9

/* Map AC to queue id. */
static const uint8_t ursu_ac2qid[WME_NUM_AC] = {
	RSU_QID_BE,
	RSU_QID_BK,
	RSU_QID_VI,
	RSU_QID_VO
};

/* Pipe index to endpoint address mapping. */
static const uint8_t r92s_epaddr[] =
    { 0x83, 0x04, 0x06, 0x0d,
      0x05, 0x07,
      0x89, 0x0a, 0x0b, 0x0c };

/* Queue id to pipe index mapping for 4 endpoints configurations. */
static const uint8_t ursu_qid2idx_4ep[] =
    { 3, 3, 3, 1, 1, 2, 2, 0, 3, 0 };

/* Queue id to pipe index mapping for 6 endpoints configurations. */
static const uint8_t ursu_qid2idx_6ep[] =
    { 3, 3, 3, 1, 4, 2, 5, 0, 3, 0 };

/* Queue id to pipe index mapping for 11 endpoints configurations. */
static const uint8_t ursu_qid2idx_11ep[] =
    { 7, 9, 8, 1, 4, 2, 5, 0, 3, 6 };

struct ursu_softc;

struct ursu_rx_data {
	struct ursu_softc	*sc;
	usbd_pipe_handle	pipe;
	usbd_xfer_handle	xfer;
	uint8_t			*buf;
};

struct ursu_tx_data {
	struct ursu_softc		*sc;
	usbd_pipe_handle		pipe;
	usbd_xfer_handle		xfer;
	uint8_t				*buf;
	TAILQ_ENTRY(ursu_tx_data)	next;
};

struct ursu_host_cmd {
	void	(*cb)(struct ursu_softc *, void *);
	uint8_t	data[256];
};

struct ursu_cmd_newstate {
	enum ieee80211_state	state;
	int			arg;
};

struct ursu_cmd_key {
	struct ieee80211_key	key;
};

struct ursu_host_cmd_ring {
	struct ursu_host_cmd	cmd[RSU_HOST_CMD_RING_COUNT];
	int			cur;
	int			next;
	int			queued;
};

struct ursu_softc {
	device_t			sc_dev;
	struct ieee80211com		sc_ic;
	struct ethercom			sc_ec;
#define sc_if   sc_ec.ec_if
	int				(*sc_newstate)(struct ieee80211com *,
					    enum ieee80211_state, int);

	usbd_device_handle		sc_udev;
	usbd_interface_handle		sc_iface;
	int				sc_dying;

	struct usb_task			sc_task;
	callout_t			sc_calib_to;

	kmutex_t			sc_task_mtx;
	kmutex_t			sc_tx_mtx;

	usbd_pipe_handle		sc_pipe[R92S_MAX_EP];
	size_t				sc_npipes;
	const uint8_t			*sc_qid2idx;

	u_int				sc_cut;
	int				sc_scan_pass;

	int				sc_tx_timer;

	uint8_t				sc_macid;

	struct ursu_host_cmd_ring	sc_cmdq;
	struct ursu_rx_data		sc_rx_data[RSU_RX_LIST_COUNT];
	struct ursu_tx_data		sc_tx_data[RSU_TX_LIST_COUNT];
	struct ursu_tx_data		*sc_fwcmd_data;
	uint8_t				sc_cmd_seq;
	TAILQ_HEAD(, ursu_tx_data)	sc_tx_free_list;

	uint8_t				sc_rom[128];

	struct bpf_if *			sc_drvbpf;
	union {
		struct ursu_rx_radiotap_header th;
		uint8_t	pad[64];
	}				sc_rxtapu;
#define sc_rxtap	sc_rxtapu.th
	int				sc_rxtap_len;
	union {
		struct ursu_tx_radiotap_header th;
		uint8_t	pad[64];
	}				sc_txtapu;
#define sc_txtap	sc_txtapu.th
	int				sc_txtap_len;
};
