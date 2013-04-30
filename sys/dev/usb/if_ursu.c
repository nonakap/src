/*	$NetBSD$	*/
/*	$OpenBSD: if_rsu.c,v 1.15 2013/02/04 23:13:41 kettenis Exp $	*/
#define	IEEE80211_NO_HT
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
 * Driver for Realtek RTL8188SU/RTL8191SU/RTL8192SU.
 */

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");

#include <sys/param.h>
#include <sys/sockio.h>
#include <sys/sysctl.h>
#include <sys/mbuf.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/conf.h>
#include <sys/device.h>

#include <sys/bus.h>
#include <machine/endian.h>
#include <sys/intr.h>

#include <net/bpf.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <net/if_dl.h>
#include <net/if_ether.h>
#include <net/if_media.h>
#include <net/if_types.h>

#include <net80211/ieee80211_netbsd.h>
#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_radiotap.h>

#include <dev/firmload.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdivar.h>
#include <dev/usb/usbdi_util.h>
#include <dev/usb/usbdevs.h>

#include <dev/usb/if_ursureg.h>
#include <dev/usb/if_ursuvar.h>

#ifdef USB_DEBUG
#ifndef URSU_DEBUG
#define URSU_DEBUG
#endif
#endif

#ifdef URSU_DEBUG
#define DPRINTF(x)	do { if (ursu_debug) printf x; } while (/*CONSTCOND*/0)
#define DPRINTFN(n, x)	do { if (ursu_debug >= (n)) printf x; } while (/*CONSTCOND*/0)
int ursu_debug = 9;
#else
#define DPRINTF(x)
#define DPRINTFN(n, x)
#endif

/*
 * NB: When updating this list of devices, beware to also update the list
 * of devices that have HT support disabled below, if applicable.
 */
static const struct usb_devno ursu_devs[] = {
	{ USB_VENDOR_ACCTON,		USB_PRODUCT_ACCTON_RTL8192SU },
	{ USB_VENDOR_ASUSTEK,		USB_PRODUCT_ASUSTEK_USBN10 },
	{ USB_VENDOR_ASUSTEK,		USB_PRODUCT_ASUSTEK_RTL8192SU_1 },
	{ USB_VENDOR_AZUREWAVE,		USB_PRODUCT_AZUREWAVE_RTL8192SU_1 },
	{ USB_VENDOR_AZUREWAVE,		USB_PRODUCT_AZUREWAVE_RTL8192SU_2 },
	{ USB_VENDOR_AZUREWAVE,		USB_PRODUCT_AZUREWAVE_RTL8192SU_3 },
	{ USB_VENDOR_AZUREWAVE,		USB_PRODUCT_AZUREWAVE_RTL8192SU_4 },
	{ USB_VENDOR_AZUREWAVE,		USB_PRODUCT_AZUREWAVE_RTL8192SU_5 },
	{ USB_VENDOR_BELKIN,		USB_PRODUCT_BELKIN_RTL8192SU_1 },
	{ USB_VENDOR_BELKIN,		USB_PRODUCT_BELKIN_RTL8192SU_2 },
	{ USB_VENDOR_BELKIN,		USB_PRODUCT_BELKIN_RTL8192SU_3 },
	{ USB_VENDOR_CONCEPTRONIC,	USB_PRODUCT_CONCEPTRONIC_RTL8192SU_1 },
	{ USB_VENDOR_CONCEPTRONIC,	USB_PRODUCT_CONCEPTRONIC_RTL8192SU_2 },
	{ USB_VENDOR_CONCEPTRONIC,	USB_PRODUCT_CONCEPTRONIC_RTL8192SU_3 },
	{ USB_VENDOR_COREGA,		USB_PRODUCT_COREGA_RTL8192SU },
	{ USB_VENDOR_DLINK2,		USB_PRODUCT_DLINK2_DWA131A1 },
	{ USB_VENDOR_DLINK2,		USB_PRODUCT_DLINK2_RTL8192SU_1 },
	{ USB_VENDOR_DLINK2,		USB_PRODUCT_DLINK2_RTL8192SU_2 },
	{ USB_VENDOR_EDIMAX,		USB_PRODUCT_EDIMAX_RTL8192SU_1 },
	{ USB_VENDOR_EDIMAX,		USB_PRODUCT_EDIMAX_RTL8192SU_2 },
	{ USB_VENDOR_EDIMAX,		USB_PRODUCT_EDIMAX_RTL8192SU_3 },
	{ USB_VENDOR_GUILLEMOT,		USB_PRODUCT_GUILLEMOT_HWGUN54 },
	{ USB_VENDOR_GUILLEMOT,		USB_PRODUCT_GUILLEMOT_HWNUM300 },
	{ USB_VENDOR_HAWKING,		USB_PRODUCT_HAWKING_RTL8192SU_1 },
	{ USB_VENDOR_HAWKING,		USB_PRODUCT_HAWKING_RTL8192SU_2 },
	{ USB_VENDOR_PLANEX2,		USB_PRODUCT_PLANEX2_GWUSH300N },
	{ USB_VENDOR_PLANEX2,		USB_PRODUCT_PLANEX2_GWUSNANO },
	{ USB_VENDOR_PLANEX2,		USB_PRODUCT_PLANEX2_GWUSMICRON2 },
	{ USB_VENDOR_REALTEK,		USB_PRODUCT_REALTEK_RTL8171 },
	{ USB_VENDOR_REALTEK,		USB_PRODUCT_REALTEK_RTL8172 },
	{ USB_VENDOR_REALTEK,		USB_PRODUCT_REALTEK_RTL8173 },
	{ USB_VENDOR_REALTEK,		USB_PRODUCT_REALTEK_RTL8174 },
	{ USB_VENDOR_REALTEK,		USB_PRODUCT_REALTEK_RTL8192SU },
	{ USB_VENDOR_REALTEK,		USB_PRODUCT_REALTEK_RTL8712 },
	{ USB_VENDOR_REALTEK,		USB_PRODUCT_REALTEK_RTL8713 },
	{ USB_VENDOR_SENAO,		USB_PRODUCT_SENAO_RTL8192SU_1 },
	{ USB_VENDOR_SENAO,		USB_PRODUCT_SENAO_RTL8192SU_2 },
	{ USB_VENDOR_SITECOMEU,		USB_PRODUCT_SITECOMEU_WL349V1 },
	{ USB_VENDOR_SITECOMEU,		USB_PRODUCT_SITECOMEU_WL353 },
	{ USB_VENDOR_SWEEX2,		USB_PRODUCT_SWEEX2_LW154 }
};

#ifndef IEEE80211_NO_HT
/* List of devices that have HT support disabled. */
static const struct usb_devno ursu_devs_noht[] = {
	{ USB_VENDOR_ASUSTEK,		USB_PRODUCT_ASUS_RTL8192SU_1 },
	{ USB_VENDOR_AZUREWAVE,		USB_PRODUCT_AZUREWAVE_RTL8192SU_4 }
};
#endif

static int	ursu_match(device_t, cfdata_t, void *);
static void	ursu_attach(device_t, device_t, void *);
static int	ursu_detach(device_t, int);
static int	ursu_activate(device_t, enum devact);

CFATTACH_DECL_NEW(ursu, sizeof(struct ursu_softc),
    ursu_match, ursu_attach, ursu_detach, ursu_activate);

static int	ursu_open_pipes(struct ursu_softc *);
static void	ursu_close_pipes(struct ursu_softc *);
static int	ursu_alloc_rx_list(struct ursu_softc *);
static void	ursu_free_rx_list(struct ursu_softc *);
static int	ursu_alloc_tx_list(struct ursu_softc *);
static void	ursu_free_tx_list(struct ursu_softc *);
static void	ursu_task(void *);
static void	ursu_do_async(struct ursu_softc *,
		    void (*)(struct ursu_softc *, void *), void *, int);
static void	ursu_wait_async(struct ursu_softc *);
static int	ursu_write_region_1(struct ursu_softc *, uint16_t, uint8_t *,
		    int);
static void	ursu_write_1(struct ursu_softc *, uint16_t, uint8_t);
static void	ursu_write_2(struct ursu_softc *, uint16_t, uint16_t);
static void	ursu_write_4(struct ursu_softc *, uint16_t, uint32_t);
static int	ursu_read_region_1(struct ursu_softc *, uint16_t, uint8_t *,
		    int);
static uint8_t	ursu_read_1(struct ursu_softc *, uint16_t);
static uint16_t	ursu_read_2(struct ursu_softc *, uint16_t);
static uint32_t	ursu_read_4(struct ursu_softc *, uint16_t);
static int	ursu_fw_iocmd(struct ursu_softc *, uint32_t);
static uint8_t	ursu_efuse_read_1(struct ursu_softc *, uint16_t);
static int	ursu_read_rom(struct ursu_softc *);
#ifdef URSU_DEBUG
static const char *ursu_cmd_name(uint8_t);
#endif
static int	ursu_fw_cmd(struct ursu_softc *, uint8_t, void *, int,
		    usbd_callback);
static int	ursu_media_change(struct ifnet *);
static void	ursu_calib_to(void *);
static void	ursu_calib_to_cb(struct ursu_softc *, void *);
static int	ursu_newstate(struct ieee80211com *, enum ieee80211_state,
		    int);
static void	ursu_newstate_cb(struct ursu_softc *, void *);
static int	ursu_site_survey(struct ursu_softc *);
static int	ursu_join_bss(struct ursu_softc *, struct ieee80211_node *);
static int	ursu_set_sta_key(struct ursu_softc *);
static void	ursu_set_sta_key_cb(struct ursu_softc *, void *);
static void	ursu_set_sta_key_resp(usbd_xfer_handle, usbd_private_handle,
		    usbd_status);
static int	ursu_disconnect(struct ursu_softc *);
static void	ursu_event_survey(struct ursu_softc *, uint8_t *, int);
static void	ursu_event_join_bss(struct ursu_softc *, uint8_t *, int);
#ifdef URSU_DEBUG
static const char *ursu_event_name(uint8_t);
#endif
static void	ursu_rx_event(struct ursu_softc *, uint8_t, uint8_t *, int);
static void	ursu_rx_multi_event(struct ursu_softc *, uint8_t *, int);
static int8_t	ursu_get_rssi(struct ursu_softc *, int, void *);
static void	ursu_rx_frame(struct ursu_softc *, uint8_t *, int);
static void	ursu_rx_multi_frame(struct ursu_softc *, uint8_t *, int);
static void	ursu_rxeof(usbd_xfer_handle, usbd_private_handle,
		    usbd_status);
static void	ursu_txeof(usbd_xfer_handle, usbd_private_handle,
		    usbd_status);
static int	ursu_tx(struct ursu_softc *, struct mbuf *,
		    struct ieee80211_node *, struct ursu_tx_data *);
static void	ursu_start(struct ifnet *);
static void	ursu_watchdog(struct ifnet *);
static int	ursu_ioctl(struct ifnet *, u_long, void *);
static void	ursu_power_on_acut(struct ursu_softc *);
static void	ursu_power_on_bcut(struct ursu_softc *);
static void	ursu_power_off(struct ursu_softc *);
static int	ursu_fw_loadsection(struct ursu_softc *, uint8_t *, int);
static int	ursu_load_firmware(struct ursu_softc *);
static int	ursu_init(struct ifnet *);
static void	ursu_stop(struct ifnet *, int);
#ifdef URSU_DEBUG
static void	ursu_dump_data(const char *, const void *, size_t);
#endif

static int
ursu_match(device_t parent, cfdata_t match, void *aux)
{
	struct usb_attach_arg *uaa = aux;

	return ((usb_lookup(ursu_devs, uaa->vendor, uaa->product) != NULL) ?
	    UMATCH_VENDOR_PRODUCT : UMATCH_NONE);
}

static void
ursu_attach(device_t parent, device_t self, void *aux)
{
	struct ursu_softc *sc = device_private(self);
	struct usb_attach_arg *uaa = aux;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &sc->sc_if;
	char *devinfop;
	size_t i;
	int error;

	sc->sc_dev = self;
	sc->sc_udev = uaa->device;

	aprint_naive("\n");
	aprint_normal("\n");

	devinfop = usbd_devinfo_alloc(sc->sc_udev, 0);
	aprint_normal_dev(self, "%s\n", devinfop);
	usbd_devinfo_free(devinfop);

	mutex_init(&sc->sc_task_mtx, MUTEX_DEFAULT, IPL_NET);
	mutex_init(&sc->sc_tx_mtx, MUTEX_DEFAULT, IPL_NONE);

	usb_init_task(&sc->sc_task, ursu_task, sc, 0);

	callout_init(&sc->sc_calib_to, 0);
	callout_setfunc(&sc->sc_calib_to, ursu_calib_to, sc);

	if (usbd_set_config_no(sc->sc_udev, 1, 0) != 0) {
		aprint_error_dev(self, "could not set configuration no\n");
		goto fail;
	}

	/* Get the first interface handle. */
	error = usbd_device2interface_handle(sc->sc_udev, 0, &sc->sc_iface);
	if (error != 0) {
		aprint_error_dev(self, "could not get interface handle\n");
		goto fail;
	}

	/* Read chip revision. */
	sc->sc_cut = MS(ursu_read_4(sc, R92S_PMC_FSM), R92S_PMC_FSM_CUT);
	if (sc->sc_cut != 3)
		sc->sc_cut = (sc->sc_cut >> 1) + 1;

	error = ursu_read_rom(sc);
	if (error != 0) {
		aprint_error_dev(self, "could not read ROM\n");
		goto fail;
	}
	IEEE80211_ADDR_COPY(ic->ic_myaddr, &sc->sc_rom[0x12]);

	aprint_normal_dev(self, "MAC/BB RTL8712 cut %d, address %s\n",
	    sc->sc_cut, ether_sprintf(ic->ic_myaddr));

	if (ursu_open_pipes(sc) != 0) {
		goto fail;
	}

	sc->sc_macid = R92S_MACID_BSS;

#ifdef URSU_DEBUG
	ic->ic_debug = IEEE80211_MSG_ANY;
#endif
	ic->ic_ifp = ifp;
	ic->ic_phytype = IEEE80211_T_OFDM;	/* Not only, but not used. */
	ic->ic_opmode = IEEE80211_M_STA;	/* Default to BSS mode. */
	ic->ic_state = IEEE80211_S_INIT;

	/* Set device capabilities. */
	ic->ic_caps =
	    IEEE80211_C_SHPREAMBLE |	/* Short preamble supported. */
	    IEEE80211_C_SHSLOT |	/* Short slot time supported. */
	    IEEE80211_C_WEP |		/* WEP. */
	    IEEE80211_C_WPA;		/* WPA/RSN. */
#ifndef IEEE80211_NO_HT
	/* Check if HT support is present. */
	if (usb_lookup(ursu_devs_noht, uaa->vendor, uaa->product) == NULL) {
		/* Set HT capabilities. */
		ic->ic_htcaps =
		    IEEE80211_HTCAP_CBW20_40 |
		    IEEE80211_HTCAP_DSSSCCK40;
		/* Set supported HT rates. */
		for (i = 0; i < 2; i++)
			ic->ic_sup_mcs[i] = 0xff;
	}
#endif

	/* Set supported .11b and .11g rates. */
	ic->ic_sup_rates[IEEE80211_MODE_11B] = ieee80211_std_rateset_11b;
	ic->ic_sup_rates[IEEE80211_MODE_11G] = ieee80211_std_rateset_11g;

	/* Set supported .11b and .11g channels (1 through 14). */
	for (i = 1; i <= 14; i++) {
		ic->ic_channels[i].ic_freq =
		    ieee80211_ieee2mhz(i, IEEE80211_CHAN_2GHZ);
		ic->ic_channels[i].ic_flags =
		    IEEE80211_CHAN_CCK | IEEE80211_CHAN_OFDM |
		    IEEE80211_CHAN_DYN | IEEE80211_CHAN_2GHZ;
	}

	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_init = ursu_init;
	ifp->if_ioctl = ursu_ioctl;
	ifp->if_start = ursu_start;
	ifp->if_watchdog = ursu_watchdog;
	IFQ_SET_READY(&ifp->if_snd);
	memcpy(ifp->if_xname, device_xname(sc->sc_dev), IFNAMSIZ);

	if_attach(ifp);
	ieee80211_ifattach(ic);

	/* Override state transition machine. */
	sc->sc_newstate = ic->ic_newstate;
	ic->ic_newstate = ursu_newstate;
	ieee80211_media_init(ic, ursu_media_change, ieee80211_media_status);

	bpf_attach2(ifp, DLT_IEEE802_11_RADIO,
	    sizeof(struct ieee80211_frame) + IEEE80211_RADIOTAP_HDRLEN,
	    &sc->sc_drvbpf);

	sc->sc_rxtap_len = sizeof(sc->sc_rxtapu);
	sc->sc_rxtap.wr_ihdr.it_len = htole16(sc->sc_rxtap_len);
	sc->sc_rxtap.wr_ihdr.it_present = htole32(RSU_RX_RADIOTAP_PRESENT);

	sc->sc_txtap_len = sizeof(sc->sc_txtapu);
	sc->sc_txtap.wt_ihdr.it_len = htole16(sc->sc_txtap_len);
	sc->sc_txtap.wt_ihdr.it_present = htole32(RSU_TX_RADIOTAP_PRESENT);

	ieee80211_announce(ic);

	usbd_add_drv_event(USB_EVENT_DRIVER_ATTACH, sc->sc_udev, sc->sc_dev);

	return;

 fail:
	sc->sc_dying = 1;
	aprint_error_dev(self, "attach failed\n");
}

static int
ursu_detach(device_t self, int flags)
{
	struct ursu_softc *sc = device_private(self);
	struct ifnet *ifp = &sc->sc_if;
	int s;

	DPRINTFN(9, ("%s: %s\n", device_xname(self), __func__));

	if (ifp->if_softc == NULL)
		return (0);

	s = splnet();

	sc->sc_dying = 1;

	callout_stop(&sc->sc_calib_to);

	if (ifp->if_flags & IFF_RUNNING) {
		usb_rem_task(sc->sc_udev, &sc->sc_task);
		ursu_stop(ifp, 0);
	}

	ifp->if_flags &= ~(IFF_RUNNING | IFF_OACTIVE);
	bpf_detach(ifp);
	ieee80211_ifdetach(&sc->sc_ic);
	if_detach(ifp);

	/* Abort and close Tx/Rx pipes. */
	ursu_close_pipes(sc);

	/* Free Tx/Rx buffers. */
	ursu_free_tx_list(sc);
	ursu_free_rx_list(sc);

	splx(s);

	usbd_add_drv_event(USB_EVENT_DRIVER_DETACH, sc->sc_udev, sc->sc_dev);

	callout_destroy(&sc->sc_calib_to);

	mutex_destroy(&sc->sc_tx_mtx);
	mutex_destroy(&sc->sc_task_mtx);

	return (0);
}

static int
ursu_activate(device_t self, enum devact act)
{
	struct ursu_softc *sc = device_private(self);

	DPRINTFN(9, ("%s: %s\n", device_xname(self), __func__));

	switch (act) {
	case DVACT_DEACTIVATE:
		if_deactivate(sc->sc_ic.ic_ifp);
		return (0);
	default:
		return (EOPNOTSUPP);
	}
}

static int
ursu_open_pipes(struct ursu_softc *sc)
{
	usb_interface_descriptor_t *id;
	size_t i, count;
	int error;

	DPRINTFN(9, ("%s: %s\n", device_xname(sc->sc_dev), __func__));

	/*
	 * Determine the number of Tx/Rx endpoints (there are chips with
	 * 4, 6 or 11 endpoints).
	 */
	id = usbd_get_interface_descriptor(sc->sc_iface);
	sc->sc_npipes = id->bNumEndpoints;
	if (sc->sc_npipes == 4)
		sc->sc_qid2idx = ursu_qid2idx_4ep;
	else if (sc->sc_npipes == 6)
		sc->sc_qid2idx = ursu_qid2idx_6ep;
	else	/* Assume npipes==11; will fail below otherwise. */
		sc->sc_qid2idx = ursu_qid2idx_11ep;
	aprint_normal_dev(sc->sc_dev, "%d endpoints\n", sc->sc_npipes);

	/* Open all pipes. */
	count = MIN(sc->sc_npipes, (int)__arraycount(r92s_epaddr));
	for (i = 0; i < count; i++) {
		uint8_t epaddr = r92s_epaddr[i];
		error = usbd_open_pipe(sc->sc_iface, epaddr, 0, &sc->sc_pipe[i]);
		if (error != 0) {
			aprint_error_dev(sc->sc_dev,
			    "could not open bulk pipe 0x%02x\n", epaddr);
			break;
		}
	}
	if (error != 0)
		ursu_close_pipes(sc);
	return (error);
}

static void
ursu_close_pipes(struct ursu_softc *sc)
{
	usbd_pipe_handle pipe;
	size_t i;

	DPRINTFN(9, ("%s: %s\n", device_xname(sc->sc_dev), __func__));

	/* Close all pipes. */
	for (i = 0; i < sc->sc_npipes; i++) {
		pipe = atomic_swap_ptr(&sc->sc_pipe[i], NULL);
		if (pipe != NULL) {
			usbd_abort_pipe(pipe);
			usbd_close_pipe(pipe);
		}
	}
}

static int
ursu_alloc_rx_list(struct ursu_softc *sc)
{
	struct ursu_rx_data *data;
	size_t i;
	int error = 0;

	DPRINTFN(9, ("%s: %s\n", device_xname(sc->sc_dev), __func__));

	for (i = 0; i < RSU_RX_LIST_COUNT; i++) {
		data = &sc->sc_rx_data[i];

		data->sc = sc;	/* Backpointer for callbacks. */

		data->xfer = usbd_alloc_xfer(sc->sc_udev);
		if (data->xfer == NULL) {
			aprint_error_dev(sc->sc_dev,
			    "could not allocate xfer\n");
			error = ENOMEM;
			break;
		}
		data->buf = usbd_alloc_buffer(data->xfer, RSU_RXBUFSZ);
		if (data->buf == NULL) {
			aprint_error_dev(sc->sc_dev,
			    "could not allocate xfer buffer\n");
			error = ENOMEM;
			break;
		}
	}
	if (error != 0)
		ursu_free_rx_list(sc);
	return (error);
}

static void
ursu_free_rx_list(struct ursu_softc *sc)
{
	usbd_xfer_handle xfer;
	size_t i;

	DPRINTFN(9, ("%s: %s\n", device_xname(sc->sc_dev), __func__));

	/* NB: Caller must abort pipe first. */
	for (i = 0; i < RSU_RX_LIST_COUNT; i++) {
		CTASSERT(sizeof(xfer) == sizeof(void *));
		xfer = atomic_swap_ptr(&sc->sc_rx_data[i].xfer, NULL);
		if (xfer != NULL)
			usbd_free_xfer(xfer);
	}
}

static int
ursu_alloc_tx_list(struct ursu_softc *sc)
{
	struct ursu_tx_data *data;
	size_t i;
	int error = 0;

	DPRINTFN(9, ("%s: %s\n", device_xname(sc->sc_dev), __func__));

	mutex_enter(&sc->sc_tx_mtx);
	TAILQ_INIT(&sc->sc_tx_free_list);
	for (i = 0; i < RSU_TX_LIST_COUNT; i++) {
		data = &sc->sc_tx_data[i];

		data->sc = sc;	/* Backpointer for callbacks. */

		data->xfer = usbd_alloc_xfer(sc->sc_udev);
		if (data->xfer == NULL) {
			aprint_error_dev(sc->sc_dev,
			    "could not allocate xfer\n");
			error = ENOMEM;
			goto fail;
		}

		data->buf = usbd_alloc_buffer(data->xfer, RSU_TXBUFSZ);
		if (data->buf == NULL) {
			aprint_error_dev(sc->sc_dev,
			    "could not allocate xfer buffer\n");
			error = ENOMEM;
			goto fail;
		}

		/* Append this Tx buffer to our free list. */
		TAILQ_INSERT_TAIL(&sc->sc_tx_free_list, data, next);
	}
	mutex_exit(&sc->sc_tx_mtx);
	return (0);

 fail:
	ursu_free_tx_list(sc);
	mutex_exit(&sc->sc_tx_mtx);
	return (error);
}

static void
ursu_free_tx_list(struct ursu_softc *sc)
{
	usbd_xfer_handle xfer;
	size_t i;

	DPRINTFN(9, ("%s: %s\n", device_xname(sc->sc_dev), __func__));

	/* NB: Caller must abort pipe first. */
	for (i = 0; i < RSU_TX_LIST_COUNT; i++) {
		CTASSERT(sizeof(xfer) == sizeof(void *));
		xfer = atomic_swap_ptr(&sc->sc_tx_data[i].xfer, NULL);
		if (xfer != NULL)
			usbd_free_xfer(xfer);
	}
}

static void
ursu_task(void *arg)
{
	struct ursu_softc *sc = arg;
	struct ursu_host_cmd_ring *ring = &sc->sc_cmdq;
	struct ursu_host_cmd *cmd;
	int s;

	DPRINTFN(9, ("%s: %s\n", device_xname(sc->sc_dev), __func__));

	/* Process host commands. */
	s = splusb();
	mutex_spin_enter(&sc->sc_task_mtx);
	while (ring->next != ring->cur) {
		cmd = &ring->cmd[ring->next];
		mutex_spin_exit(&sc->sc_task_mtx);
		splx(s);
		/* Invoke callback. */
		cmd->cb(sc, cmd->data);
		s = splusb();
		mutex_spin_enter(&sc->sc_task_mtx);
		ring->queued--;
		ring->next = (ring->next + 1) % RSU_HOST_CMD_RING_COUNT;
	}
	mutex_spin_exit(&sc->sc_task_mtx);
	wakeup(&sc->sc_cmdq);
	splx(s);
}

static void
ursu_do_async(struct ursu_softc *sc, void (*cb)(struct ursu_softc *, void *),
    void *arg, int len)
{
	struct ursu_host_cmd_ring *ring = &sc->sc_cmdq;
	struct ursu_host_cmd *cmd;
	int s;

	DPRINTFN(9, ("%s: %s\n", device_xname(sc->sc_dev), __func__));

	s = splusb();
	mutex_spin_enter(&sc->sc_task_mtx);
	cmd = &ring->cmd[ring->cur];
	cmd->cb = cb;
	KASSERT(len <= sizeof(cmd->data));
	memcpy(cmd->data, arg, len);
	ring->cur = (ring->cur + 1) % RSU_HOST_CMD_RING_COUNT;

	/* If there is no pending command already, schedule a task. */
	if (!sc->sc_dying && ++ring->queued == 1) {
		mutex_spin_exit(&sc->sc_task_mtx);
		usb_add_task(sc->sc_udev, &sc->sc_task, USB_TASKQ_DRIVER);
	} else
		mutex_spin_exit(&sc->sc_task_mtx);
	splx(s);
}

static void
ursu_wait_async(struct ursu_softc *sc)
{

	DPRINTFN(9, ("%s: %s\n", device_xname(sc->sc_dev), __func__));

	/* Wait for all queued asynchronous commands to complete. */
	while (sc->sc_cmdq.queued > 0)
		tsleep(&sc->sc_cmdq, 0, "ursutask", 0);
}

static int
ursu_write_region_1(struct ursu_softc *sc, uint16_t addr, uint8_t *buf, int len)
{
	usb_device_request_t req;
	usbd_status error;

	req.bmRequestType = UT_WRITE_VENDOR_DEVICE;
	req.bRequest = R92S_REQ_REGS;
	USETW(req.wValue, addr);
	USETW(req.wIndex, 0);
	USETW(req.wLength, len);
	error = usbd_do_request(sc->sc_udev, &req, buf);
	if (error != USBD_NORMAL_COMPLETION) {
		DPRINTFN(10, ("%s: %s: error=%d: addr=0x%x, len=%d\n",
		    device_xname(sc->sc_dev), __func__, error, addr, len));
	}
	return (error);
}

static void
ursu_write_1(struct ursu_softc *sc, uint16_t addr, uint8_t val)
{

	DPRINTFN(10, ("%s: %s: addr=0x%x, val=0x%x\n",
	    device_xname(sc->sc_dev), __func__, addr, val));

	ursu_write_region_1(sc, addr, &val, 1);
}

static void
ursu_write_2(struct ursu_softc *sc, uint16_t addr, uint16_t val)
{
	uint8_t buf[2];

	DPRINTFN(10, ("%s: %s: addr=0x%x, val=0x%x\n",
	    device_xname(sc->sc_dev), __func__, addr, val));

	buf[0] = (uint8_t)val;
	buf[1] = (uint8_t)(val >> 8);
	ursu_write_region_1(sc, addr, buf, 2);
}

static void
ursu_write_4(struct ursu_softc *sc, uint16_t addr, uint32_t val)
{
	uint8_t buf[4];

	DPRINTFN(10, ("%s: %s: addr=0x%x, val=0x%x\n",
	    device_xname(sc->sc_dev), __func__, addr, val));

	buf[0] = (uint8_t)val;
	buf[1] = (uint8_t)(val >> 8);
	buf[2] = (uint8_t)(val >> 16);
	buf[3] = (uint8_t)(val >> 24);
	ursu_write_region_1(sc, addr, buf, 4);
}

static int
ursu_read_region_1(struct ursu_softc *sc, uint16_t addr, uint8_t *buf, int len)
{
	usb_device_request_t req;
	usbd_status error;

	req.bmRequestType = UT_READ_VENDOR_DEVICE;
	req.bRequest = R92S_REQ_REGS;
	USETW(req.wValue, addr);
	USETW(req.wIndex, 0);
	USETW(req.wLength, len);
	error = usbd_do_request(sc->sc_udev, &req, buf);
	if (error != USBD_NORMAL_COMPLETION) {
		DPRINTFN(10, ("%s: %s: error=%d: addr=0x%x, len=%d\n",
		    device_xname(sc->sc_dev), __func__, error, addr, len));
	}
	return (error);
}

static uint8_t
ursu_read_1(struct ursu_softc *sc, uint16_t addr)
{
	uint8_t val;

	if (ursu_read_region_1(sc, addr, &val, 1) != USBD_NORMAL_COMPLETION)
		return (0xff);

	DPRINTFN(10, ("%s: %s: addr=0x%x, val=0x%x\n",
	    device_xname(sc->sc_dev), __func__, addr, val));
	return (val);
}

static uint16_t
ursu_read_2(struct ursu_softc *sc, uint16_t addr)
{
	uint8_t buf[2];
	uint16_t val;

	if (ursu_read_region_1(sc, addr, buf, 2) != USBD_NORMAL_COMPLETION)
		return (0xffff);

	val = LE_READ_2(&buf[0]);
	DPRINTFN(10, ("%s: %s: addr=0x%x, val=0x%x\n",
	    device_xname(sc->sc_dev), __func__, addr, val));
	return (val);
}

static uint32_t
ursu_read_4(struct ursu_softc *sc, uint16_t addr)
{
	uint8_t buf[4];
	uint32_t val;

	if (ursu_read_region_1(sc, addr, buf, 4) != USBD_NORMAL_COMPLETION)
		return (0xffffffff);

	val = LE_READ_4(&buf[0]);
	DPRINTFN(10, ("%s: %s: addr=0x%x, val=0x%x\n",
	    device_xname(sc->sc_dev), __func__, addr, val));
	return (val);
}

static int
ursu_fw_iocmd(struct ursu_softc *sc, uint32_t iocmd)
{
	int ntries;

	ursu_write_4(sc, R92S_IOCMD_CTRL, iocmd);
	DELAY(100);
	for (ntries = 0; ntries < 50; ntries++) {
		if (ursu_read_4(sc, R92S_IOCMD_CTRL) == 0)
			return (0);
		DELAY(10);
	}
	return (ETIMEDOUT);
}

static uint8_t
ursu_efuse_read_1(struct ursu_softc *sc, uint16_t addr)
{
	uint32_t reg;
	int ntries;

	reg = ursu_read_4(sc, R92S_EFUSE_CTRL);
	reg = RW(reg, R92S_EFUSE_CTRL_ADDR, addr);
	reg &= ~R92S_EFUSE_CTRL_VALID;
	ursu_write_4(sc, R92S_EFUSE_CTRL, reg);

	/* Wait for read operation to complete. */
	for (ntries = 0; ntries < 100; ntries++) {
		reg = ursu_read_4(sc, R92S_EFUSE_CTRL);
		if (reg & R92S_EFUSE_CTRL_VALID)
			return (MS(reg, R92S_EFUSE_CTRL_DATA));
		DELAY(5);
	}
	aprint_error_dev(sc->sc_dev,
	    "could not read efuse byte at address 0x%x\n", addr);
	return (0xff);
}

static int
ursu_read_rom(struct ursu_softc *sc)
{
	uint8_t *rom = sc->sc_rom;
	uint16_t addr = 0;
	uint32_t reg;
	uint8_t off, msk;
	int i;

	DPRINTFN(9, ("%s: %s\n", device_xname(sc->sc_dev), __func__));

	/* Make sure that ROM type is eFuse and that autoload succeeded. */
	reg = ursu_read_1(sc, R92S_EE_9346CR);
	if ((reg & (R92S_9356SEL | R92S_EEPROM_EN)) != R92S_EEPROM_EN)
		return (EIO);

	/* Turn on 2.5V to prevent eFuse leakage. */
	reg = ursu_read_1(sc, R92S_EFUSE_TEST + 3);
	ursu_write_1(sc, R92S_EFUSE_TEST + 3, reg | 0x80);
	DELAY(1000);
	ursu_write_1(sc, R92S_EFUSE_TEST + 3, reg & ~0x80);

	/* Read full ROM image. */
	memset(&sc->sc_rom, 0xff, sizeof(sc->sc_rom));
	while (addr < 512) {
		reg = ursu_efuse_read_1(sc, addr);
		if (reg == 0xff)
			break;
		addr++;
		off = reg >> 4;
		msk = reg & 0xf;
		for (i = 0; i < 4; i++) {
			if (msk & (1 << i))
				continue;
			rom[off * 8 + i * 2 + 0] =
			    ursu_efuse_read_1(sc, addr);
			addr++;
			rom[off * 8 + i * 2 + 1] =
			    ursu_efuse_read_1(sc, addr);
			addr++;
		}
	}
#ifdef URSU_DEBUG
	if (ursu_debug >= 5) {
		/* Dump ROM content. */
		ursu_dump_data("ROM", sc->sc_rom, sizeof(sc->sc_rom));
	}
#endif
	return (0);
}

#ifdef URSU_DEBUG
static const char *
ursu_cmd_name(uint8_t code)
{

	switch (code) {
	case R92S_CMD_READ_MACREG:	return "R92S_CMD_READ_MACREG";
	case R92S_CMD_WRITE_MACREG:	return "R92S_CMD_WRITE_MACREG";
	case R92S_CMD_READ_BBREG:	return "R92S_CMD_READ_BBREG";
	case R92S_CMD_WRITE_BBREG:	return "R92S_CMD_WRITE_BBREG";
	case R92S_CMD_READ_RFREG:	return "R92S_CMD_READ_RFREG";
	case R92S_CMD_WRITE_RFREG:	return "R92S_CMD_WRITE_RFREG";
	case R92S_CMD_READ_EEPROM:	return "R92S_CMD_READ_EEPROM";
	case R92S_CMD_WRITE_EEPROM:	return "R92S_CMD_WRITE_EEPROM";
	case R92S_CMD_READ_EFUSE:	return "R92S_CMD_READ_EFUSE";
	case R92S_CMD_WRITE_EFUSE:	return "R92S_CMD_WRITE_EFUSE";
	case R92S_CMD_READ_CAM:		return "R92S_CMD_READ_CAM";
	case R92S_CMD_WRITE_CAM:	return "R92S_CMD_WRITE_CAM";
	case R92S_CMD_SET_BCNITV:	return "R92S_CMD_SET_BCNITV";
	case R92S_CMD_SET_MBIDCFG:	return "R92S_CMD_SET_MBIDCFG";
	case R92S_CMD_JOIN_BSS:		return "R92S_CMD_JOIN_BSS";
	case R92S_CMD_DISCONNECT:	return "R92S_CMD_DISCONNECT";
	case R92S_CMD_CREATE_BSS:	return "R92S_CMD_CREATE_BSS";
	case R92S_CMD_SET_OPMODE:	return "R92S_CMD_SET_OPMODE";
	case R92S_CMD_SITE_SURVEY:	return "R92S_CMD_SITE_SURVEY";
	case R92S_CMD_SET_AUTH:		return "R92S_CMD_SET_AUTH";
	case R92S_CMD_SET_KEY:		return "R92S_CMD_SET_KEY";
	case R92S_CMD_SET_STA_KEY:	return "R92S_CMD_SET_STA_KEY";
	case R92S_CMD_SET_ASSOC_STA:	return "R92S_CMD_SET_ASSOC_STA";
	case R92S_CMD_DEL_ASSOC_STA:	return "R92S_CMD_DEL_ASSOC_STA";
	case R92S_CMD_SET_STAPWRSTATE:	return "R92S_CMD_SET_STAPWRSTATE";
	case R92S_CMD_SET_BASIC_RATE:	return "R92S_CMD_SET_BASIC_RATE";
	case R92S_CMD_GET_BASIC_RATE:	return "R92S_CMD_GET_BASIC_RATE";
	case R92S_CMD_SET_DATA_RATE:	return "R92S_CMD_SET_DATA_RATE";
	case R92S_CMD_GET_DATA_RATE:	return "R92S_CMD_GET_DATA_RATE";
	case R92S_CMD_SET_PHY_INFO:	return "R92S_CMD_SET_PHY_INFO";
	case R92S_CMD_GET_PHY_INFO:	return "R92S_CMD_GET_PHY_INFO";
	case R92S_CMD_SET_PHY:		return "R92S_CMD_SET_PHY";
	case R92S_CMD_GET_PHY:		return "R92S_CMD_GET_PHY";
	case R92S_CMD_READ_RSSI:	return "R92S_CMD_READ_RSSI";
	case R92S_CMD_READ_GAIN:	return "R92S_CMD_READ_GAIN";
	case R92S_CMD_SET_ATIM:		return "R92S_CMD_SET_ATIM";
	case R92S_CMD_SET_PWR_MODE:	return "R92S_CMD_SET_PWR_MODE";
	case R92S_CMD_JOIN_BSS_RPT:	return "R92S_CMD_JOIN_BSS_RPT";
	case R92S_CMD_SET_RA_TABLE:	return "R92S_CMD_SET_RA_TABLE";
	case R92S_CMD_GET_RA_TABLE:	return "R92S_CMD_GET_RA_TABLE";
	case R92S_CMD_GET_CCX_REPORT:	return "R92S_CMD_GET_CCX_REPORT";
	case R92S_CMD_GET_DTM_REPORT:	return "R92S_CMD_GET_DTM_REPORT";
	case R92S_CMD_GET_TXRATE_STATS:	return "R92S_CMD_GET_TXRATE_STATS";
	case R92S_CMD_SET_USB_SUSPEND:	return "R92S_CMD_SET_USB_SUSPEND";
	case R92S_CMD_SET_H2C_LBK:	return "R92S_CMD_SET_H2C_LBK";
	case R92S_CMD_ADDBA_REQ:	return "R92S_CMD_ADDBA_REQ";
	case R92S_CMD_SET_CHANNEL:	return "R92S_CMD_SET_CHANNEL";
	case R92S_CMD_SET_TXPOWER:	return "R92S_CMD_SET_TXPOWER";
	case R92S_CMD_SWITCH_ANTENNA:	return "R92S_CMD_SWITCH_ANTENNA";
	case R92S_CMD_SET_CRYSTAL_CAL:	return "R92S_CMD_SET_CRYSTAL_CAL";
	case R92S_CMD_SET_SINGLE_CARRIER_TX: return "R92S_CMD_SET_SINGLE_CARRIER_TX";
	case R92S_CMD_SET_SINGLE_TONE_TX: return "R92S_CMD_SET_SINGLE_TONE_TX";
	case R92S_CMD_SET_CARRIER_SUPPR_TX: return "R92S_CMD_SET_CARRIER_SUPPR_TX";
	case R92S_CMD_SET_CONTINUOUS_TX: return "R92S_CMD_SET_CONTINUOUS_TX";
	case R92S_CMD_SWITCH_BANDWIDTH:	return "R92S_CMD_SWITCH_BANDWIDTH";
	case R92S_CMD_TX_BEACON:	return "R92S_CMD_TX_BEACON";
	case R92S_CMD_SET_POWER_TRACKING: return "R92S_CMD_SET_POWER_TRACKING";
	case R92S_CMD_AMSDU_TO_AMPDU:	return "R92S_CMD_AMSDU_TO_AMPDU";
	case R92S_CMD_SET_MAC_ADDRESS:	return "R92S_CMD_SET_MAC_ADDRESS";
	case R92S_CMD_GET_H2C_LBK:	return "R92S_CMD_GET_H2C_LBK";
	case R92S_CMD_SET_PBREQ_IE:	return "R92S_CMD_SET_PBREQ_IE";
	case R92S_CMD_SET_ASSOCREQ_IE:	return "R92S_CMD_SET_ASSOCREQ_IE";
	case R92S_CMD_SET_PBRESP_IE:	return "R92S_CMD_SET_PBRESP_IE";
	case R92S_CMD_SET_ASSOCRESP_IE:	return "R92S_CMD_SET_ASSOCRESP_IE";
	case R92S_CMD_GET_CURDATARATE:	return "R92S_CMD_GET_CURDATARATE";
	case R92S_CMD_GET_TXRETRY_CNT:	return "R92S_CMD_GET_TXRETRY_CNT";
	case R92S_CMD_GET_RXRETRY_CNT:	return "R92S_CMD_GET_RXRETRY_CNT";
	case R92S_CMD_GET_BCNOK_CNT:	return "R92S_CMD_GET_BCNOK_CNT";
	case R92S_CMD_GET_BCNERR_CNT:	return "R92S_CMD_GET_BCNERR_CNT";
	case R92S_CMD_GET_CURTXPWR_LEVEL: return "R92S_CMD_GET_CURTXPWR_LEVEL";
	case R92S_CMD_SET_DIG:		return "R92S_CMD_SET_DIG";
	case R92S_CMD_SET_RA:		return "R92S_CMD_SET_RA";
	case R92S_CMD_SET_PT:		return "R92S_CMD_SET_PT";
	case R92S_CMD_READ_TSSI:	return "R92S_CMD_READ_TSSI";
	}
	return "unknown";
}
#endif

static int
ursu_fw_cmd(struct ursu_softc *sc, uint8_t code, void *buf, int len,
    usbd_callback callback)
{
	struct ieee80211com *ic = &sc->sc_ic;
	const struct ieee80211_rsnparms *rsn = &ic->ic_bss->ni_rsn;
	struct ursu_tx_data *data;
	struct r92s_tx_desc *txd;
	struct r92s_fw_cmd_hdr *cmd;
	usbd_pipe_handle pipe;
	int cmdsz, xferlen;

	DPRINTFN(9, ("%s: %s\n", device_xname(sc->sc_dev), __func__));
#ifdef URSU_DEBUG
	if (ursu_debug >= 5) {
		/* Dump frame */
		ursu_dump_data(ursu_cmd_name(code), buf, len);
	}
#endif

	data = sc->sc_fwcmd_data;

	/* Round-up command length to a multiple of 8 bytes. */
	cmdsz = (len + 7) & ~7;

	xferlen = sizeof(*txd) + sizeof(*cmd) + cmdsz;
	KASSERT(xferlen <= RSU_TXBUFSZ);
	memset(data->buf, 0, xferlen);

	/* Setup Tx descriptor. */
	txd = (struct r92s_tx_desc *)data->buf;
	txd->txdw0 = htole32(
	    SM(R92S_TXDW0_OFFSET, sizeof(*txd)) |
	    SM(R92S_TXDW0_PKTLEN, sizeof(*cmd) + cmdsz) |
	    R92S_TXDW0_OWN | R92S_TXDW0_FSG | R92S_TXDW0_LSG);
	txd->txdw1 = htole32(SM(R92S_TXDW1_QSEL, R92S_TXDW1_QSEL_H2C));
	if (rsn->rsn_ucastcipher == IEEE80211_CIPHER_AES_CCM) {
		txd->txdw1 |= htole32(SM(R92S_TXDW1_CIPHER,
		    R92S_TXDW1_CIPHER_AES));
		DPRINTFN(2, ("%s: %s: chipher: AES\n",
		    device_xname(sc->sc_dev), __func__));
	} else if (rsn->rsn_ucastcipher == IEEE80211_CIPHER_TKIP) {
		txd->txdw1 |= htole32(SM(R92S_TXDW1_CIPHER,
		    R92S_TXDW1_CIPHER_TKIP));
		DPRINTFN(2, ("%s: %s: chipher: TKIP\n",
		    device_xname(sc->sc_dev), __func__));
	} else if (rsn->rsn_ucastcipher == IEEE80211_CIPHER_WEP) {
		txd->txdw1 |= htole32(SM(R92S_TXDW1_CIPHER,
		    R92S_TXDW1_CIPHER_WEP));
		DPRINTFN(2, ("%s: %s: chipher: WEP\n",
		    device_xname(sc->sc_dev), __func__));
	}

	/* Setup command header. */
	cmd = (struct r92s_fw_cmd_hdr *)&txd[1];
	cmd->len = htole16(cmdsz);
	cmd->code = code;
	cmd->seq = sc->sc_cmd_seq;
	sc->sc_cmd_seq = (sc->sc_cmd_seq + 1) & 0x7f;

	/* Copy command payload. */
	memcpy(&cmd[1], buf, len);

	DPRINTFN(2, ("%s: %s: Tx cmd code=%d(%s), len=%d\n",
	    device_xname(sc->sc_dev), __func__, code, ursu_cmd_name(code),
	    cmdsz));
	pipe = sc->sc_pipe[sc->sc_qid2idx[RSU_QID_H2C]];
	usbd_setup_xfer(data->xfer, pipe, data, data->buf, xferlen,
	    USBD_SHORT_XFER_OK | USBD_NO_COPY, RSU_CMD_TIMEOUT, callback);
	return usbd_sync_transfer(data->xfer);
}

static int
ursu_media_change(struct ifnet *ifp)
{
#ifdef URSU_DEBUG
	struct ursu_softc *sc = ifp->if_softc;
#endif
	int error;

	DPRINTFN(9, ("%s: %s\n", device_xname(sc->sc_dev), __func__));

	if ((error = ieee80211_media_change(ifp)) != ENETRESET)
		return (error);

	if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) ==
	    (IFF_UP | IFF_RUNNING)) {
		ursu_init(ifp);
	}
	return (0);
}

static void
ursu_calib_to(void *arg)
{
	struct ursu_softc *sc = arg;

	DPRINTFN(9, ("%s: %s\n", device_xname(sc->sc_dev), __func__));

	if (sc->sc_dying)
		return;

	/* Do it in a process context. */
	ursu_do_async(sc, ursu_calib_to_cb, NULL, 0);
	/* next timeout will be rescheduled in the calibration task */
}

static void
ursu_calib_to_cb(struct ursu_softc *sc, void *arg)
{
	uint32_t reg;

	DPRINTFN(9, ("%s: %s\n", device_xname(sc->sc_dev), __func__));

#ifdef notyet
	/* Read WPS PBC status. */
	ursu_write_1(sc, R92S_MAC_PINMUX_CTRL,
	    R92S_GPIOMUX_EN | SM(R92S_GPIOSEL_GPIO, R92S_GPIOSEL_GPIO_JTAG));
	ursu_write_1(sc, R92S_GPIO_IO_SEL,
	    ursu_read_1(sc, R92S_GPIO_IO_SEL) & ~R92S_GPIO_WPS);
	reg = ursu_read_1(sc, R92S_GPIO_CTRL);
	if (reg != 0xff && (reg & R92S_GPIO_WPS))
		DPRINTF(("WPS PBC is pushed\n"));
#endif
	/* Read current signal level. */
	if (ursu_fw_iocmd(sc, 0xf4000001) == 0) {
		reg = ursu_read_4(sc, R92S_IOCMD_DATA);
		DPRINTFN(8, ("%s: %s: RSSI=%d%%\n", device_xname(sc->sc_dev),
		    __func__, reg >> 4));
	}

	if (!sc->sc_dying)
		callout_schedule(&sc->sc_calib_to, hz);
}

static int
ursu_newstate(struct ieee80211com *ic, enum ieee80211_state nstate, int arg)
{
	struct ursu_softc *sc = ic->ic_ifp->if_softc;
	struct ursu_cmd_newstate cmd;

	DPRINTFN(9, ("%s: %s: nstate=%s(%d), arg=%d\n",
	    device_xname(sc->sc_dev), __func__,
	    ieee80211_state_name[nstate], nstate, arg));

	callout_stop(&sc->sc_calib_to);

	/* Do it in a process context. */
	cmd.state = nstate;
	cmd.arg = arg;
	ursu_do_async(sc, ursu_newstate_cb, &cmd, sizeof(cmd));
	return (0);
}

static void
ursu_newstate_cb(struct ursu_softc *sc, void *arg)
{
	struct ursu_cmd_newstate *cmd = arg;
	struct ieee80211com *ic = &sc->sc_ic;
	enum ieee80211_state ostate = ic->ic_state;
	enum ieee80211_state nstate = cmd->state;
	int error, s;

	s = splnet();

	callout_stop(&sc->sc_calib_to);

	DPRINTFN(9, ("%s: %s: %s(%d)->%s(%d)\n",
	    device_xname(sc->sc_dev), __func__,
	    ieee80211_state_name[ostate], ostate,
	    ieee80211_state_name[nstate], nstate));

	switch (ostate) {
	case IEEE80211_S_INIT:
	case IEEE80211_S_SCAN:
	case IEEE80211_S_AUTH:
	case IEEE80211_S_ASSOC:
		break;

	case IEEE80211_S_RUN:
		/* Stop calibration. */
		callout_stop(&sc->sc_calib_to);
		/* Disassociate from our current BSS. */
		(void) ursu_disconnect(sc);
		break;
	}

	switch (nstate) {
	case IEEE80211_S_INIT:
		break;

	case IEEE80211_S_SCAN:
		error = ursu_site_survey(sc);
		if (error != 0) {
			aprint_error_dev(sc->sc_dev,
			    "could not send site survey command\n");
		}
		/* handle this ourselves */
		ic->ic_state = nstate;
		goto out;

	case IEEE80211_S_AUTH:
		error = ursu_join_bss(sc, ic->ic_bss);
		if (error != 0) {
			aprint_error_dev(sc->sc_dev,
			    "could not send join command\n");
			ieee80211_begin_scan(ic, 0);
		} else {
			/* handle this ourselves */
			ic->ic_state = nstate;
		}
		goto out;

	case IEEE80211_S_ASSOC:
		/* handle this ourselves */
		ic->ic_state = nstate;
		goto out;

	case IEEE80211_S_RUN:
		/* Indicate highest supported rate. */
		ic->ic_bss->ni_txrate = ic->ic_bss->ni_rates.rs_nrates - 1;

		/* Start periodic calibration. */
		if (!sc->sc_dying)
			callout_schedule(&sc->sc_calib_to, hz);
		break;
	}

	(*sc->sc_newstate)(ic, nstate, cmd->arg);

 out:
	splx(s);
}

static int
ursu_site_survey(struct ursu_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct r92s_fw_cmd_sitesurvey cmd;

	DPRINTFN(9, ("%s: %s\n", device_xname(sc->sc_dev), __func__));

	memset(&cmd, 0, sizeof(cmd));
	if ((ic->ic_flags & IEEE80211_F_ASCAN) || sc->sc_scan_pass == 1) {
		cmd.active = htole32(1);
	}
	cmd.limit = htole32(48);
	if (sc->sc_scan_pass == 1) {
		/* Do a directed scan for second pass. */
		cmd.ssidlen = htole32(ic->ic_des_esslen);
		memcpy(cmd.ssid, ic->ic_des_essid, ic->ic_des_esslen);
	}
	DPRINTF(("%s: %s: sending site survey command, pass=%d\n",
	    device_xname(sc->sc_dev), __func__, sc->sc_scan_pass));
	return ursu_fw_cmd(sc, R92S_CMD_SITE_SURVEY, &cmd, sizeof(cmd), NULL);
}

/* XXX */
static uint8_t *
ursu_ieee80211_setup_wpa_ie(struct ieee80211com *ic, uint8_t *ie)
{
#define	WPA_OUI_BYTES		0x00, 0x50, 0xf2
#define	ADDSHORT(frm, v) do {			\
	frm[0] = (v) & 0xff;			\
	frm[1] = (v) >> 8;			\
	frm += 2;				\
} while (0)
#define	ADDSELECTOR(frm, sel) do {		\
	memcpy(frm, sel, 4);			\
	frm += 4;				\
} while (0)
	static const uint8_t cipher_suite[][4] = {
		{ WPA_OUI_BYTES, WPA_CSE_WEP40 },	/* NB: 40-bit */
		{ WPA_OUI_BYTES, WPA_CSE_TKIP },
		{ 0x00, 0x00, 0x00, 0x00 },		/* XXX WRAP */
		{ WPA_OUI_BYTES, WPA_CSE_CCMP },
		{ 0x00, 0x00, 0x00, 0x00 },		/* XXX CKIP */
		{ WPA_OUI_BYTES, WPA_CSE_NULL },
	};
	static const uint8_t wep104_suite[4] =
		{ WPA_OUI_BYTES, WPA_CSE_WEP104 };
	static const uint8_t key_mgt_unspec[4] =
		{ WPA_OUI_BYTES, WPA_ASE_8021X_UNSPEC };
	static const uint8_t key_mgt_psk[4] =
		{ WPA_OUI_BYTES, WPA_ASE_8021X_PSK };
	const struct ieee80211_rsnparms *rsn = &ic->ic_bss->ni_rsn;
	uint8_t *frm = ie;
	uint8_t *selcnt;
#ifdef URSU_DEBUG
	struct ursu_softc *sc = ic->ic_ifp->if_softc;
#endif

	*frm++ = IEEE80211_ELEMID_VENDOR;
	*frm++ = 0;				/* length filled in below */

	/* XXX filter out CKIP */

	/* multicast cipher */
	DPRINTFN(2, ("%s: %s: multicast cipher=%d, len=%d\n",
	    device_xname(sc->sc_dev), __func__, 
	    rsn->rsn_mcastcipher, rsn->rsn_mcastkeylen));
	if (rsn->rsn_mcastcipher == IEEE80211_CIPHER_WEP &&
	    rsn->rsn_mcastkeylen >= 13)
		ADDSELECTOR(frm, wep104_suite);
	else
		ADDSELECTOR(frm, cipher_suite[rsn->rsn_mcastcipher]);

	/* unicast cipher list */
	DPRINTFN(2, ("%s: %s: unicast cipherset=0x%x, cipher=%d\n",
	    device_xname(sc->sc_dev), __func__, 
	    rsn->rsn_ucastcipherset, rsn->rsn_ucastcipher));
	selcnt = frm;
	ADDSHORT(frm, 0);			/* selector count */
	if (rsn->rsn_ucastcipherset & (1<<IEEE80211_CIPHER_AES_CCM)) {
		selcnt[0]++;
		ADDSELECTOR(frm, cipher_suite[IEEE80211_CIPHER_AES_CCM]);
	}
	if (rsn->rsn_ucastcipherset & (1<<IEEE80211_CIPHER_TKIP)) {
		selcnt[0]++;
		ADDSELECTOR(frm, cipher_suite[IEEE80211_CIPHER_TKIP]);
	}

	/* authenticator selector list */
	DPRINTFN(2, ("%s: %s: authenticator selector: keymgmtset=0x%x\n",
	    device_xname(sc->sc_dev), __func__, rsn->rsn_keymgmtset));
	selcnt = frm;
	ADDSHORT(frm, 0);			/* selector count */
	if (rsn->rsn_keymgmtset & WPA_ASE_8021X_UNSPEC) {
		selcnt[0]++;
		ADDSELECTOR(frm, key_mgt_unspec);
	}
	if (rsn->rsn_keymgmtset & WPA_ASE_8021X_PSK) {
		selcnt[0]++;
		ADDSELECTOR(frm, key_mgt_psk);
	}

	/* optional capabilities */
	if (rsn->rsn_caps != 0 && rsn->rsn_caps != RSN_CAP_PREAUTH)
		ADDSHORT(frm, rsn->rsn_caps);

	/* calculate element length */
	ie[1] = frm - ie - 2;
	IASSERT((size_t)ie[1]+2 <= sizeof(struct ieee80211_ie_wpa),
		("WPA IE too big, %u > %zu",
		ie[1]+2, sizeof(struct ieee80211_ie_wpa)));
	return frm;
#undef ADDSHORT
#undef ADDSELECTOR
#undef WPA_OUI_BYTES
}

static int
ursu_join_bss(struct ursu_softc *sc, struct ieee80211_node *ni)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ndis_wlan_bssid_ex *bss;
	struct ndis_802_11_fixed_ies *fixed;
	struct r92s_fw_cmd_auth auth;
	uint8_t buf[sizeof(*bss) + 128], *frm;
	uint8_t opmode;
	int error;

	DPRINTFN(9, ("%s: %s\n", device_xname(sc->sc_dev), __func__));

	/* Let the FW decide the opmode based on the capinfo field. */
	opmode = NDIS802_11AUTOUNKNOWN;
	DPRINTF(("%s: %s: setting operating mode to %d\n",
	    device_xname(sc->sc_dev), __func__, opmode));
	error = ursu_fw_cmd(sc, R92S_CMD_SET_OPMODE, &opmode, sizeof(opmode),
	    NULL);
	if (error != 0)
		return (error);

	memset(&auth, 0, sizeof(auth));
	if (ic->ic_flags & IEEE80211_F_WPA) {
		auth.mode = R92S_AUTHMODE_WPA;
		auth.dot1x = 0; /* XXX */
	} else {
		auth.mode = R92S_AUTHMODE_OPEN;
	}
	DPRINTF(("%s: %s: setting auth mode to %d\n",
	    device_xname(sc->sc_dev), __func__, auth.mode));
	error = ursu_fw_cmd(sc, R92S_CMD_SET_AUTH, &auth, sizeof(auth), NULL);
	if (error != 0)
		return (error);

	memset(buf, 0, sizeof(buf));
	bss = (struct ndis_wlan_bssid_ex *)buf;
	IEEE80211_ADDR_COPY(bss->macaddr, ni->ni_bssid);
	bss->ssid.ssidlen = htole32(ni->ni_esslen);
	memcpy(bss->ssid.ssid, ni->ni_essid, ni->ni_esslen);
	if (ic->ic_flags & (IEEE80211_F_PRIVACY|IEEE80211_F_WPA)) {
		bss->privacy = htole32(1);
	}
	bss->rssi = htole32(ni->ni_rssi);
	if (ic->ic_curmode == IEEE80211_MODE_11B) {
		bss->networktype = htole32(NDIS802_11DS);
	} else {
		bss->networktype = htole32(NDIS802_11OFDM24);
	}
	bss->config.len = htole32(sizeof(bss->config));
	bss->config.bintval = htole32(ni->ni_intval);
	bss->config.dsconfig = htole32(ieee80211_chan2ieee(ic, ni->ni_chan));
	bss->inframode = htole32(NDIS802_11INFRASTRUCTURE);
	memcpy(bss->supprates, ni->ni_rates.rs_rates, ni->ni_rates.rs_nrates);
	/* Write the fixed fields of the beacon frame. */
	fixed = (struct ndis_802_11_fixed_ies *)&bss[1];
	memcpy(&fixed->tstamp, ni->ni_tstamp.data, sizeof(ni->ni_tstamp));
	fixed->bintval = htole16(ni->ni_intval);
	fixed->capabilities = htole16(ni->ni_capinfo);
	/* Write IEs to be included in the association request. */
	frm = (uint8_t *)&fixed[1];
	if (ic->ic_flags & IEEE80211_F_WPA2) {
		frm = ieee80211_add_wpa(frm, ic);
		DPRINTF(("%s: %s: WPA2\n", device_xname(sc->sc_dev), __func__));
	}
#if 0 /* XXX */
	if (ni->ni_flags & IEEE80211_NODE_QOS) {
		frm = ieee80211_add_qos_capability(frm, ic);
	}
#endif /* XXX */
#ifndef IEEE80211_NO_HT
	if (ni->ni_flags & IEEE80211_NODE_HT) {
		frm = ieee80211_add_htcaps(frm, ic);
	}
#endif
	if (ic->ic_flags & IEEE80211_F_WPA1) {
		DPRINTF(("%s: %s: WPA\n", device_xname(sc->sc_dev), __func__));
		frm = ursu_ieee80211_setup_wpa_ie(ic, frm);
	}
	bss->ieslen = htole32(frm - (uint8_t *)fixed);
	bss->len = htole32(((frm - buf) + 3) & ~3);
	DPRINTF(("%s: %s: sending join bss command to %s chan %d\n",
	    device_xname(sc->sc_dev), __func__,
	    ether_sprintf(bss->macaddr), le32toh(bss->config.dsconfig)));
	return ursu_fw_cmd(sc, R92S_CMD_JOIN_BSS, buf, sizeof(buf), NULL);
}

static int
ursu_set_sta_key(struct ursu_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_node *ni = ic->ic_bss;
	const struct ieee80211_rsnparms *rsn = &ni->ni_rsn;
	const struct ieee80211_key *key = &ni->ni_ucastkey;
	struct r92s_fw_cmd_set_sta_key cmd;

	DPRINTFN(9, ("%s: %s\n", device_xname(sc->sc_dev), __func__));

	/* Do it in a process context. */
	memset(&cmd, 0, sizeof(cmd));
	IEEE80211_ADDR_COPY(cmd.macaddr, ni->ni_bssid);
	// XXX unicast only
	if (rsn->rsn_ucastcipher == IEEE80211_CIPHER_AES_CCM) {
		cmd.algo = R92S_KEY_ALGO_AES;
		DPRINTFN(2, ("%s: %s: chipher: AES\n",
		    device_xname(sc->sc_dev), __func__));
	} else if (rsn->rsn_ucastcipher == IEEE80211_CIPHER_TKIP) {
		cmd.algo = R92S_KEY_ALGO_TKIP;
		DPRINTFN(2, ("%s: %s: chipher: TKIP\n",
		    device_xname(sc->sc_dev), __func__));
	} else if (rsn->rsn_ucastcipher == IEEE80211_CIPHER_WEP) {
		if (key->wk_keylen == 104 / NBBY) {
			cmd.algo = R92S_KEY_ALGO_WEP104;
			DPRINTFN(2, ("%s: %s: chipher: WEP104\n",
			    device_xname(sc->sc_dev), __func__));
		} else {
			cmd.algo = R92S_KEY_ALGO_WEP40;
			DPRINTFN(2, ("%s: %s: chipher: WEP40\n",
			    device_xname(sc->sc_dev), __func__));
		}
	}
	memcpy(cmd.key, key->wk_key, key->wk_keylen);
	ursu_do_async(sc, ursu_set_sta_key_cb, &cmd, sizeof(cmd));
	return (0);
}

static void
ursu_set_sta_key_cb(struct ursu_softc *sc, void *arg)
{
	struct r92s_fw_cmd_set_sta_key *sta_key = arg;

	DPRINTFN(9, ("%s: %s\n", device_xname(sc->sc_dev), __func__));

	DPRINTF(("%s: %s: setting sta key to %s\n", device_xname(sc->sc_dev),
	    __func__, ether_sprintf(sta_key->macaddr)));
	ursu_fw_cmd(sc, R92S_CMD_SET_STA_KEY, sta_key, sizeof(*sta_key),
	    ursu_set_sta_key_resp);
}

static void
ursu_set_sta_key_resp(usbd_xfer_handle xfer, usbd_private_handle priv,
    usbd_status status)
{
	struct ursu_tx_data *data = priv;
	struct ursu_softc *sc = data->sc;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_node *ni;
	struct r92s_fw_resp_set_sta_key *rsp;
	int len;

	DPRINTFN(9, ("%s: %s: status=%d\n", device_xname(sc->sc_dev),
	    __func__, status));

	if (status == USBD_NORMAL_COMPLETION) {
		usbd_get_xfer_status(xfer, NULL, NULL, &len, NULL);
		if (__predict_false(len < (int)sizeof(*rsp))) {
			DPRINTF(("%s: %s: xfer too short %d\n",
			    device_xname(sc->sc_dev), __func__, len));
			return;
		}
#ifdef URSU_DEBUG
		if (ursu_debug >= 5) {
			/* Dump response */
			ursu_dump_data("R92S_RESP_SET_STA_KEY", data->buf, len);
		}
#endif

		/* XXX */
		rsp = (struct r92s_fw_resp_set_sta_key *)data->buf;
		ni = ieee80211_find_node(&ic->ic_scan, rsp->macaddr);
		if (ni != NULL) {
			sc->sc_macid = rsp->macid;
			ieee80211_free_node(ni);
		}

		ieee80211_new_state(ic, IEEE80211_S_ASSOC, 0);
		ieee80211_new_state(ic, IEEE80211_S_RUN,
		    IEEE80211_FC0_SUBTYPE_ASSOC_RESP);
	}
}

static int
ursu_disconnect(struct ursu_softc *sc)
{
	uint32_t zero = 0;	/* :-) */

	/* Disassociate from our current BSS. */
	DPRINTF(("%s: %s: sending disconnect command\n",
	    device_xname(sc->sc_dev), __func__));
	return ursu_fw_cmd(sc, R92S_CMD_DISCONNECT, &zero, sizeof(zero), NULL);
}

static void
ursu_event_survey(struct ursu_softc *sc, uint8_t *buf, int len)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = ic->ic_ifp;
	struct ieee80211_node *ni;
	struct ieee80211_frame *wh;
	struct ndis_wlan_bssid_ex *bss;
	struct mbuf *m;
	int pktlen;

	DPRINTFN(9, ("%s: %s\n", device_xname(sc->sc_dev), __func__));

	if (__predict_false(len < (int)sizeof(*bss)))
		return;
	bss = (struct ndis_wlan_bssid_ex *)buf;
	if (__predict_false(len < (int)(sizeof(*bss) + le32toh(bss->ieslen))))
		return;

	DPRINTFN(2, ("%s: %s: found BSS %s: len=%d chan=%d inframode=%d "
	    "networktype=%d privacy=%d\n",
	    device_xname(sc->sc_dev), __func__,
	    ether_sprintf(bss->macaddr), le32toh(bss->len),
	    le32toh(bss->config.dsconfig), le32toh(bss->inframode),
	    le32toh(bss->networktype), le32toh(bss->privacy)));

	/* Build a fake beacon frame to let net80211 do all the parsing. */
	pktlen = sizeof(*wh) + le32toh(bss->ieslen);
	if (__predict_false(pktlen > MCLBYTES))
		return;
	MGETHDR(m, M_DONTWAIT, MT_DATA);
	if (__predict_false(m == NULL))
		return;
	if (pktlen > (int)MHLEN) {
		MCLGET(m, M_DONTWAIT);
		if (!(m->m_flags & M_EXT)) {
			m_free(m);
			return;
		}
	}
	wh = mtod(m, struct ieee80211_frame *);
	wh->i_fc[0] = IEEE80211_FC0_VERSION_0 | IEEE80211_FC0_TYPE_MGT |
	    IEEE80211_FC0_SUBTYPE_BEACON;
	wh->i_fc[1] = IEEE80211_FC1_DIR_NODS;
	*(uint16_t *)wh->i_dur = 0;
	IEEE80211_ADDR_COPY(wh->i_addr1, etherbroadcastaddr);
	IEEE80211_ADDR_COPY(wh->i_addr2, bss->macaddr);
	IEEE80211_ADDR_COPY(wh->i_addr3, bss->macaddr);
	*(uint16_t *)wh->i_seq = 0;
	memcpy(&wh[1], (uint8_t *)&bss[1], le32toh(bss->ieslen));

	/* Finalize mbuf. */
	m->m_pkthdr.len = m->m_len = pktlen;
	m->m_pkthdr.rcvif = ifp;

	ni = ieee80211_find_rxnode(ic, (struct ieee80211_frame_min *)wh);

	/* push the frame up to the 802.11 stack */
	ieee80211_input(ic, m, ni, bss->rssi, 0);

	/* Node is no longer needed. */
	ieee80211_free_node(ni);
}

static void
ursu_event_join_bss(struct ursu_softc *sc, uint8_t *buf, int len)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_node *ni = ic->ic_bss;
	const struct ieee80211_rsnparms *rsn = &ni->ni_rsn;
	struct r92s_event_join_bss *rsp;
	int res;

	DPRINTFN(9, ("%s: %s\n", device_xname(sc->sc_dev), __func__));

	if (__predict_false(len < (int)sizeof(*rsp)))
		return;
	rsp = (struct r92s_event_join_bss *)buf;
	res = (int)le32toh(rsp->join_res);

	DPRINTF(("%s: %s: Rx join BSS event len=%d, res=%d\n",
	    device_xname(sc->sc_dev), __func__, len, res));
#ifdef URSU_DEBUG
	if (ursu_debug >= 5) {
		/* Dump response */
		ursu_dump_data("R92S_EVT_JOIN_BSS", buf, len);
	}
#endif
	if (res <= 0) {
		ic->ic_stats.is_rx_auth_fail++;
		ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);
		return;
	}
	DPRINTF(("%s: %s: associated with %s associd=%d\n",
	    device_xname(sc->sc_dev), __func__,
	    ether_sprintf(rsp->bss.macaddr), le32toh(rsp->associd)));

	ni->ni_associd = le32toh(rsp->associd) | 0xc000;

	if (rsn->rsn_ucastcipher != IEEE80211_CIPHER_NONE) {
		ursu_set_sta_key(sc);
	} else {
		ieee80211_new_state(ic, IEEE80211_S_ASSOC, 0);
		ieee80211_new_state(ic, IEEE80211_S_RUN,
		    IEEE80211_FC0_SUBTYPE_ASSOC_RESP);
	}
}

#ifdef URSU_DEBUG
static const char *
ursu_event_name(uint8_t code)
{

	switch (code) {
	case R92S_EVT_READ_MACREG:	return "R92S_EVT_READ_MACREG";
	case R92S_EVT_READ_BBREG:	return "R92S_EVT_READ_BBREG";
	case R92S_EVT_READ_RFREG:	return "R92S_EVT_READ_RFREG";
	case R92S_EVT_READ_EEPROM:	return "R92S_EVT_READ_EEPROM";
	case R92S_EVT_READ_EFUSE:	return "R92S_EVT_READ_EFUSE";
	case R92S_EVT_READ_CAM:		return "R92S_EVT_READ_CAM";
	case R92S_EVT_GET_BASICRATE:	return "R92S_EVT_GET_BASICRATE";
	case R92S_EVT_GET_DATARATE:	return "R92S_EVT_GET_DATARATE";
	case R92S_EVT_SURVEY:		return "R92S_EVT_SURVEY";
	case R92S_EVT_SURVEY_DONE:	return "R92S_EVT_SURVEY_DONE";
	case R92S_EVT_JOIN_BSS:		return "R92S_EVT_JOIN_BSS";
	case R92S_EVT_ADD_STA:		return "R92S_EVT_ADD_STA";
	case R92S_EVT_DEL_STA:		return "R92S_EVT_DEL_STA";
	case R92S_EVT_ATIM_DONE:	return "R92S_EVT_ATIM_DONE";
	case R92S_EVT_TX_REPORT:	return "R92S_EVT_TX_REPORT";
	case R92S_EVT_CCX_REPORT:	return "R92S_EVT_CCX_REPORT";
	case R92S_EVT_DTM_REPORT:	return "R92S_EVT_DTM_REPORT";
	case R92S_EVT_TXRATE_STATS:	return "R92S_EVT_TXRATE_STATS";
	case R92S_EVT_C2H_LBK:		return "R92S_EVT_C2H_LBK";
	case R92S_EVT_FWDBG:		return "R92S_EVT_FWDBG";
	case R92S_EVT_C2H_FEEDBACK:	return "R92S_EVT_C2H_FEEDBACK";
	case R92S_EVT_ADDBA:		return "R92S_EVT_ADDBA";
	case R92S_EVT_C2H_BCN:		return "R92S_EVT_C2H_BCN";
	case R92S_EVT_PWR_STATE:	return "R92S_EVT_PWR_STATE";
	case R92S_EVT_WPS_PBC:		return "R92S_EVT_WPS_PBC";
	case R92S_EVT_ADDBA_REQ_REPORT:	return "R92S_EVT_ADDBA_REQ_REPORT";
	}
	return "unknown";
}
#endif

static void
ursu_rx_event(struct ursu_softc *sc, uint8_t code, uint8_t *buf, int len)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = ic->ic_ifp;

	DPRINTFN(4, ("%s: %s: Rx event code=%d(%s), len=%d\n",
	    device_xname(sc->sc_dev), __func__, code, ursu_event_name(code),
	    len));

	switch (code) {
	case R92S_EVT_SURVEY:
		if (ic->ic_state == IEEE80211_S_SCAN) {
			ursu_event_survey(sc, buf, len);
		}
		break;

	case R92S_EVT_SURVEY_DONE:
		DPRINTF(("%s: %s: site survey pass %d done, found %d BSS\n",
		    device_xname(sc->sc_dev), __func__,
		    sc->sc_scan_pass, le32toh(*(uint32_t *)buf)));
		if (ic->ic_state != IEEE80211_S_SCAN)
			break;	/* Ignore if not scanning. */
		if (sc->sc_scan_pass == 0 && ic->ic_des_esslen != 0) {
			/* Schedule a directed scan for hidden APs. */
			sc->sc_scan_pass = 1;
			ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);
		} else {
			DPRINTF(("%s: %s: site survey end\n",
			    device_xname(sc->sc_dev), __func__));
			ieee80211_end_scan(ic);
			sc->sc_scan_pass = 0;
		}
		DPRINTF(("%s: %s: site survey done\n",
		    device_xname(sc->sc_dev), __func__));
		break;

	case R92S_EVT_JOIN_BSS:
		if (ic->ic_state == IEEE80211_S_AUTH) {
			ursu_event_join_bss(sc, buf, len);
		}
		break;

	case R92S_EVT_DEL_STA:
		DPRINTF(("%s: %s: disassociated from %s\n",
		    device_xname(sc->sc_dev), __func__, ether_sprintf(buf)));
		if (ic->ic_state == IEEE80211_S_RUN &&
		    IEEE80211_ADDR_EQ(ic->ic_bss->ni_bssid, buf)) {
			ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);
		}
		break;

	case R92S_EVT_WPS_PBC:
		DPRINTF(("%s: %s: WPS PBC pushed.\n", device_xname(sc->sc_dev),
		    __func__));
		break;

	case R92S_EVT_FWDBG:
		if (ifp->if_flags & IFF_DEBUG) {
			buf[60] = '\0';
			printf("%s: %s: FWDBG: %s\n", device_xname(sc->sc_dev),
			    __func__, (char *)buf);
		}
		break;
	}
}

static void
ursu_rx_multi_event(struct ursu_softc *sc, uint8_t *buf, int len)
{
	struct r92s_fw_cmd_hdr *cmd;
	int cmdsz;

	DPRINTFN(6, ("%s: %s: Rx events len=%d\n", device_xname(sc->sc_dev),
	    __func__, len));

	/* Skip Rx status. */
	buf += sizeof(struct r92s_rx_stat);
	len -= sizeof(struct r92s_rx_stat);

	/* Process all events. */
	for (;;) {
		/* Check that command header fits. */
		if (__predict_false(len < (int)sizeof(*cmd)))
			break;

		cmd = (struct r92s_fw_cmd_hdr *)buf;
		/* Check that command payload fits. */
		cmdsz = le16toh(cmd->len);
		if (__predict_false(len < (int)(sizeof(*cmd) + cmdsz)))
			break;

		/* Process firmware event. */
		ursu_rx_event(sc, cmd->code, (uint8_t *)&cmd[1], cmdsz);

		if (!(cmd->seq & R92S_FW_CMD_MORE))
			break;
		buf += sizeof(*cmd) + cmdsz;
		len -= sizeof(*cmd) + cmdsz;
	}
}

static int8_t
ursu_get_rssi(struct ursu_softc *sc, int rate, void *physt)
{
	static const int8_t cckoff[] = { 14, -2, -20, -40 };
	struct r92s_rx_phystat *phy;
	struct r92s_rx_cck *cck;
	uint8_t rpt;
	int8_t rssi;

	DPRINTFN(9, ("%s: %s\n", device_xname(sc->sc_dev), __func__));

	if (rate <= 3) {
		cck = (struct r92s_rx_cck *)physt;
		rpt = (cck->agc_rpt >> 6) & 0x3;
		rssi = cck->agc_rpt & 0x3e;
		rssi = cckoff[rpt] - rssi;
	} else {	/* OFDM/HT. */
		phy = (struct r92s_rx_phystat *)physt;
		rssi = ((le32toh(phy->phydw1) >> 1) & 0x7f) - 106;
	}
	return (rssi);
}

static void
ursu_rx_frame(struct ursu_softc *sc, uint8_t *buf, int pktlen)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = ic->ic_ifp;
	struct ieee80211_frame *wh;
	struct ieee80211_node *ni;
	struct r92s_rx_stat *stat;
	uint32_t rxdw0, rxdw3;
	struct mbuf *m;
	uint8_t rate;
	int8_t rssi = 0;
	int s, infosz;

	DPRINTFN(9, ("%s: %s\n", device_xname(sc->sc_dev), __func__));

	stat = (struct r92s_rx_stat *)buf;
	rxdw0 = le32toh(stat->rxdw0);
	rxdw3 = le32toh(stat->rxdw3);

	if (__predict_false(rxdw0 & R92S_RXDW0_CRCERR)) {
		DPRINTFN(5, ("%s: %s: CRC error\n",
		    device_xname(sc->sc_dev), __func__));
		ifp->if_ierrors++;
		return;
	}
	/*
	 * XXX: This will drop most control packets.  Do we really
	 * want this in IEEE80211_M_MONITOR mode?
	 */
	/* if (__predict_false(pktlen < (int)sizeof(*wh))) { */
	if (__predict_false(pktlen < (int)sizeof(struct ieee80211_frame_ack))) {
		DPRINTFN(5, ("%s: %s: packet too short %d\n",
		    device_xname(sc->sc_dev), __func__, pktlen));
		ic->ic_stats.is_rx_tooshort++;
		ifp->if_ierrors++;
		return;
	}
	if (pktlen > (int)MCLBYTES) {
		DPRINTFN(5, ("%s: %s: packet too big %d\n",
		    device_xname(sc->sc_dev), __func__, pktlen));
		ifp->if_ierrors++;
		return;
	}

	rate = MS(rxdw3, R92S_RXDW3_RATE);
	infosz = MS(rxdw0, R92S_RXDW0_INFOSZ) * 8;

	/* Get RSSI from PHY status descriptor if present. */
	if (infosz != 0) {
		rssi = ursu_get_rssi(sc, rate, &stat[1]);
	}

	DPRINTFN(5, ("%s: %s: Rx frame len=%d rate=%d infosz=%d rssi=%d\n",
	    device_xname(sc->sc_dev), __func__, pktlen, rate, infosz, rssi));

	MGETHDR(m, M_DONTWAIT, MT_DATA);
	if (__predict_false(m == NULL)) {
		aprint_error_dev(sc->sc_dev, "couldn't allocate rx mbuf\n");
		ifp->if_ierrors++;
		return;
	}
	if (pktlen > (int)MHLEN) {
		MCLGET(m, M_DONTWAIT);
		if (__predict_false(!(m->m_flags & M_EXT))) {
			aprint_error_dev(sc->sc_dev,
			    "couldn't allocate rx mbuf cluster\n");
			ifp->if_ierrors++;
			m_freem(m);
			return;
		}
	}

	/* Finalize mbuf. */
	m->m_pkthdr.rcvif = ifp;
	/* Hardware does Rx TCP checksum offload. */
	if (rxdw3 & R92S_RXDW3_TCPCHKVALID) {
		m->m_pkthdr.csum_flags |= M_CSUM_TCPv4;
		if (__predict_false(rxdw3 & R92S_RXDW3_TCPCHKRPT))
			m->m_pkthdr.csum_flags |= M_CSUM_TCP_UDP_BAD;
	}
	wh = (struct ieee80211_frame *)((uint8_t *)&stat[1] + infosz);
	memcpy(mtod(m, uint8_t *), wh, pktlen);
	m->m_pkthdr.len = m->m_len = pktlen;

	s = splnet();
	if (__predict_false(sc->sc_drvbpf != NULL)) {
		struct ursu_rx_radiotap_header *tap = &sc->sc_rxtap;

		tap->wr_flags = 0;
		/* Map HW rate index to 802.11 rate. */
		tap->wr_flags = 2;
		if (!(rxdw3 & R92S_RXDW3_HTC)) {
			switch (rate) {
			/* CCK. */
			case  0: tap->wr_rate =   2; break;
			case  1: tap->wr_rate =   4; break;
			case  2: tap->wr_rate =  11; break;
			case  3: tap->wr_rate =  22; break;
			/* OFDM. */
			case  4: tap->wr_rate =  12; break;
			case  5: tap->wr_rate =  18; break;
			case  6: tap->wr_rate =  24; break;
			case  7: tap->wr_rate =  36; break;
			case  8: tap->wr_rate =  48; break;
			case  9: tap->wr_rate =  72; break;
			case 10: tap->wr_rate =  96; break;
			case 11: tap->wr_rate = 108; break;
			}
		} else if (rate >= 12) {	/* MCS0~15. */
			/* Bit 7 set means HT MCS instead of rate. */
			tap->wr_rate = 0x80 | (rate - 12);
		}
		tap->wr_dbm_antsignal = rssi;
		tap->wr_chan_freq = htole16(ic->ic_ibss_chan->ic_freq);
		tap->wr_chan_flags = htole16(ic->ic_ibss_chan->ic_flags);

		bpf_mtap2(sc->sc_drvbpf, tap, sc->sc_rxtap_len, m);
	}

	ni = ieee80211_find_rxnode(ic, (struct ieee80211_frame_min *)wh);

	/* push the frame up to the 802.11 stack */
	ieee80211_input(ic, m, ni, rssi, 0);

	/* Node is no longer needed. */
	ieee80211_free_node(ni);

	splx(s);
}

static void
ursu_rx_multi_frame(struct ursu_softc *sc, uint8_t *buf, int len)
{
	struct r92s_rx_stat *stat;
	uint32_t rxdw0;
	int totlen, pktlen, infosz, npkts;

	/* Get the number of encapsulated frames. */
	stat = (struct r92s_rx_stat *)buf;
	npkts = MS(le32toh(stat->rxdw2), R92S_RXDW2_PKTCNT);
	DPRINTFN(6, ("%s: %s: Rx %d frames in one chunk\n",
	    device_xname(sc->sc_dev), __func__, npkts));

	/* Process all of them. */
	while (npkts-- > 0) {
		if (__predict_false(len < (int)sizeof(*stat)))
			break;

		stat = (struct r92s_rx_stat *)buf;
		rxdw0 = le32toh(stat->rxdw0);

		pktlen = MS(rxdw0, R92S_RXDW0_PKTLEN);
		if (__predict_false(pktlen == 0))
			break;

		infosz = MS(rxdw0, R92S_RXDW0_INFOSZ) * 8;

		/* Make sure everything fits in xfer. */
		totlen = sizeof(*stat) + infosz + pktlen;
		if (__predict_false(totlen > len))
			break;

		/* Process 802.11 frame. */
		ursu_rx_frame(sc, buf, pktlen);

		/* Next chunk is 128-byte aligned. */
		totlen = (totlen + 127) & ~127;
		buf += totlen;
		len -= totlen;
	}
}

static void
ursu_rxeof(usbd_xfer_handle xfer, usbd_private_handle priv, usbd_status status)
{
	struct ursu_rx_data *data = priv;
	struct ursu_softc *sc = data->sc;
	struct r92s_rx_stat *stat;
	int len;

	DPRINTFN(9, ("%s: %s\n", device_xname(sc->sc_dev), __func__));

	if (__predict_false(status != USBD_NORMAL_COMPLETION)) {
		DPRINTF(("%s: %s: RX status=%d\n", device_xname(sc->sc_dev),
		    __func__, status));
		if (status == USBD_STALLED)
			usbd_clear_endpoint_stall_async(data->pipe);
		else if (status != USBD_CANCELLED)
			goto resubmit;
		return;
	}
	usbd_get_xfer_status(xfer, NULL, NULL, &len, NULL);

	if (__predict_false(len < (int)sizeof(*stat))) {
		DPRINTF(("%s: %s: xfer too short %d\n",
		    device_xname(sc->sc_dev), __func__, len));
		goto resubmit;
	}

	/* Determine if it is a firmware C2H event or an 802.11 frame. */
	stat = (struct r92s_rx_stat *)data->buf;
	if ((le32toh(stat->rxdw1) & 0x1ff) == 0x1ff)
		ursu_rx_multi_event(sc, data->buf, len);
	else
		ursu_rx_multi_frame(sc, data->buf, len);

 resubmit:
	/* Setup a new transfer. */
	usbd_setup_xfer(xfer, data->pipe, data, data->buf, RSU_RXBUFSZ,
	    USBD_SHORT_XFER_OK | USBD_NO_COPY, USBD_NO_TIMEOUT, ursu_rxeof);
	(void) usbd_transfer(xfer);
}

static void
ursu_txeof(usbd_xfer_handle xfer, usbd_private_handle priv,
    usbd_status status)
{
	struct ursu_tx_data *data = priv;
	struct ursu_softc *sc = data->sc;
	struct ifnet *ifp = sc->sc_ic.ic_ifp;
	int s;

	DPRINTFN(9, ("%s: %s\n", device_xname(sc->sc_dev), __func__));

	mutex_enter(&sc->sc_tx_mtx);
	/* Put this Tx buffer back to our free list. */
	TAILQ_INSERT_TAIL(&sc->sc_tx_free_list, data, next);
	mutex_exit(&sc->sc_tx_mtx);

	s = splnet();
	sc->sc_tx_timer = 0;
	ifp->if_flags &= ~IFF_OACTIVE;

	if (__predict_false(status != USBD_NORMAL_COMPLETION)) {
		DPRINTF(("TX status=%d\n", status));
		if (status != USBD_NOT_STARTED && status != USBD_CANCELLED) {
			if (status == USBD_STALLED)
				usbd_clear_endpoint_stall_async(data->pipe);
			ifp->if_oerrors++;
		}
		splx(s);
		return;
	}

	ifp->if_opackets++;
	ursu_start(ifp);

	splx(s);
}

static int
ursu_tx(struct ursu_softc *sc, struct mbuf *m, struct ieee80211_node *ni,
    struct ursu_tx_data *data)
{
	struct ieee80211com *ic = &sc->sc_ic;
	const struct ieee80211_rsnparms *rsn = &ic->ic_bss->ni_rsn;
	struct ieee80211_frame *wh;
	struct ieee80211_key *k = NULL;
	struct r92s_tx_desc *txd;
	usbd_pipe_handle pipe;
	size_t xferlen;
	uint8_t type, qid, tid;
	int s, hasqos, error;

	DPRINTFN(9, ("%s: %s\n", device_xname(sc->sc_dev), __func__));

	wh = mtod(m, struct ieee80211_frame *);
	type = wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK;

	if (wh->i_fc[1] & IEEE80211_FC1_WEP) {
		k = ieee80211_crypto_encap(ic, ni, m);
		if (k == NULL)
			return (ENOBUFS);

		/* packet header may have moved, reset our local pointer */
		wh = mtod(m, struct ieee80211_frame *);
	}

	if (__predict_false(sc->sc_drvbpf != NULL)) {
		struct ursu_tx_radiotap_header *tap = &sc->sc_txtap;

		tap->wt_flags = 0;
		tap->wt_chan_freq = htole16(ic->ic_bss->ni_chan->ic_freq);
		tap->wt_chan_flags = htole16(ic->ic_bss->ni_chan->ic_flags);
		if (wh->i_fc[1] & IEEE80211_FC1_WEP)
			tap->wt_flags |= IEEE80211_RADIOTAP_F_WEP;

		/* XXX: set tap->wt_rate? */

		bpf_mtap2(sc->sc_drvbpf, tap, sc->sc_txtap_len, m);
	}

	if ((hasqos = IEEE80211_QOS_HAS_SEQ(wh))) {
		/* data frames in 11n mode */
		struct ieee80211_qosframe *qwh = (void *)wh;
		tid = qwh->i_qos[0] & IEEE80211_QOS_TID;
		qid = ursu_ac2qid[TID_TO_WME_AC(tid)];
	} else {
		tid = 0;
		qid = RSU_QID_BE;
	}

	/* Get the USB pipe to use for this queue id. */
	pipe = sc->sc_pipe[sc->sc_qid2idx[qid]];

	/* Fill Tx descriptor. */
	txd = (struct r92s_tx_desc *)data->buf;
	memset(txd, 0, sizeof(*txd));

	if (type == IEEE80211_FC0_TYPE_DATA) {
		txd->txdw0 |= htole32(
		    SM(R92S_TXDW0_PKTLEN, m->m_pkthdr.len) |
		    SM(R92S_TXDW0_OFFSET, sizeof(*txd)) |
		    R92S_TXDW0_OWN | R92S_TXDW0_FSG | R92S_TXDW0_LSG);

		txd->txdw1 |= htole32(
		    SM(R92S_TXDW1_MACID, sc->sc_macid) |
		    SM(R92S_TXDW1_QSEL, R92S_TXDW1_QSEL_BE));
		if (!hasqos)
			txd->txdw1 |= htole32(R92S_TXDW1_NONQOS);
		if (rsn->rsn_ucastcipher == IEEE80211_CIPHER_AES_CCM) {
			txd->txdw1 |= htole32(SM(R92S_TXDW1_CIPHER,
			    R92S_TXDW1_CIPHER_AES));
			DPRINTFN(2, ("%s: %s: chipher: AES\n",
			    device_xname(sc->sc_dev), __func__));
		} else if (rsn->rsn_ucastcipher == IEEE80211_CIPHER_TKIP) {
			txd->txdw1 |= htole32(SM(R92S_TXDW1_CIPHER,
			    R92S_TXDW1_CIPHER_TKIP));
			DPRINTFN(2, ("%s: %s: chipher: TKIP\n",
			    device_xname(sc->sc_dev), __func__));
		} else if (rsn->rsn_ucastcipher == IEEE80211_CIPHER_WEP) {
			txd->txdw1 |= htole32(SM(R92S_TXDW1_CIPHER,
			    R92S_TXDW1_CIPHER_WEP));
			DPRINTFN(2, ("%s: %s: chipher: WEP\n",
			    device_xname(sc->sc_dev), __func__));
		}

		txd->txdw2 |= htole32(R92S_TXDW2_BK);
		if (IEEE80211_IS_MULTICAST(wh->i_addr1))
			txd->txdw2 |= htole32(R92S_TXDW2_BMCAST);
		/*
		 * Firmware will use and increment the sequence number for the
		 * specified TID.
		 */
		txd->txdw3 |= htole32(SM(R92S_TXDW3_SEQ, tid));
	} else if (type == IEEE80211_FC0_TYPE_MGT) {
		txd->txdw1 |= htole32(
		    SM(R92S_TXDW1_MACID, R92S_MACID_BSS) |
		    SM(R92S_TXDW1_QSEL, R92S_TXDW1_QSEL_BE) |
		    R92S_TXDW1_NONQOS);
		if (IEEE80211_IS_MULTICAST(wh->i_addr1))
			txd->txdw2 |= htole32(R92S_TXDW2_BMCAST);
		txd->txdw3 |= htole32(SM(R92S_TXDW3_SEQ, tid));
		txd->txdw4 |= htole32(0x80002040);
		/* Use 1Mbps */
		txd->txdw5 |= htole32(0x001f8000);
	} else {
		txd->txdw1 |= htole32(
		    SM(R92S_TXDW1_QSEL, R92S_TXDW1_QSEL_BE) |
		    R92S_TXDW1_NONQOS);
		txd->txdw3 |= htole32(SM(R92S_TXDW3_SEQ, tid));
		txd->txdw4 |= htole32(0x80002040);
		txd->txdw5 |= htole32(0x001f9600);
	}

	xferlen = sizeof(*txd) + m->m_pkthdr.len;
	m_copydata(m, 0, m->m_pkthdr.len, (void *)&txd[1]);

#ifdef URSU_DEBUG
	if (ursu_debug >= 5) {
		ursu_dump_data("TX", data->buf, xferlen);
	}
#endif
	s = splnet();
	data->pipe = pipe;
	usbd_setup_xfer(data->xfer, pipe, data, data->buf, xferlen,
	    USBD_FORCE_SHORT_XFER | USBD_NO_COPY, RSU_TX_TIMEOUT,
	    ursu_txeof);
	error = usbd_transfer(data->xfer);
	if (__predict_false(error != USBD_NORMAL_COMPLETION &&
	    error != USBD_IN_PROGRESS)) {
		splx(s);
		return (error);
	}
	splx(s);
	return (0);
}

static void
ursu_start(struct ifnet *ifp)
{
	struct ursu_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ursu_tx_data *data;
	struct ether_header *eh;
	struct ieee80211_node *ni;
	struct mbuf *m;
	int error;

	DPRINTFN(9, ("%s: %s\n", device_xname(sc->sc_dev), __func__));

	if ((ifp->if_flags & (IFF_RUNNING | IFF_OACTIVE)) != IFF_RUNNING)
		return;

	data = NULL;
	for (;;) {
		mutex_enter(&sc->sc_tx_mtx);
		if (data == NULL && !TAILQ_EMPTY(&sc->sc_tx_free_list)) {
			data = TAILQ_FIRST(&sc->sc_tx_free_list);
			TAILQ_REMOVE(&sc->sc_tx_free_list, data, next);
		}
		mutex_exit(&sc->sc_tx_mtx);

		if (data == NULL) {
			ifp->if_flags |= IFF_OACTIVE;
			return;
		}

		if (ic->ic_state != IEEE80211_S_RUN)
			break;

		/* Encapsulate and send data frames. */
		IFQ_DEQUEUE(&ifp->if_snd, m);
		if (m == NULL)
			break;

		if (m->m_len < (int)sizeof(*eh) &&
		    (m = m_pullup(m, sizeof(*eh))) == NULL) {
			ifp->if_oerrors++;
			continue;
		}
		eh = mtod(m, struct ether_header *);
		ni = ieee80211_find_txnode(ic, eh->ether_dhost);
		if (ni == NULL) {
			m_freem(m);
			ifp->if_oerrors++;
			continue;
		}

		bpf_mtap(ifp, m);

		if ((m = ieee80211_encap(ic, m, ni)) == NULL) {
			ieee80211_free_node(ni);
			ifp->if_oerrors++;
			continue;
		}

		bpf_mtap3(ic->ic_rawbpf, m);

		error = ursu_tx(sc, m, ni, data);
		m_freem(m);
		ieee80211_free_node(ni);
		if (error != 0) {
			ifp->if_oerrors++;
			continue;
		}

		data = NULL;	/* we're finished with this data buffer */
		sc->sc_tx_timer = 5;
		ifp->if_timer = 1;
	}

	/* Return the Tx buffer to the free list */
	mutex_enter(&sc->sc_tx_mtx);
	TAILQ_INSERT_TAIL(&sc->sc_tx_free_list, data, next);
	mutex_exit(&sc->sc_tx_mtx);
}

static void
ursu_watchdog(struct ifnet *ifp)
{
	struct ursu_softc *sc = ifp->if_softc;

	DPRINTFN(9, ("%s: %s\n", device_xname(sc->sc_dev), __func__));

	ifp->if_timer = 0;

	if (sc->sc_tx_timer > 0) {
		if (--sc->sc_tx_timer == 0) {
			aprint_error_dev(sc->sc_dev, "device timeout\n");
			/* ursu_init(ifp); XXX needs a process context! */
			ifp->if_oerrors++;
			return;
		}
		ifp->if_timer = 1;
	}
	ieee80211_watchdog(&sc->sc_ic);
}

static int
ursu_ioctl(struct ifnet *ifp, u_long cmd, void *data)
{
	struct ursu_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	int s, error = 0;

	DPRINTFN(9, ("%s: %s: cmd=0x%08lx, data=%p\n",
	    device_xname(sc->sc_dev), __func__, cmd, data));

	s = splnet();

	switch (cmd) {
	case SIOCSIFFLAGS:
		if ((error = ifioctl_common(ifp, cmd, data)) != 0)
			break;
		switch (ifp->if_flags & (IFF_UP|IFF_RUNNING)) {
		case IFF_UP|IFF_RUNNING:
			break;
		case IFF_UP:
			ursu_init(ifp);
			break;
		case IFF_RUNNING:
			ursu_stop(ifp, 1);
			break;
		case 0:
			break;
		}
		break;

	case SIOCADDMULTI:
	case SIOCDELMULTI:
		if ((error = ether_ioctl(ifp, cmd, data)) == ENETRESET) {
			/* setup multicast filter, etc */
			error = 0;
		}
		break;

	default:
		error = ieee80211_ioctl(ic, cmd, data);
		break;
	}

	if (error == ENETRESET) {
		if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) ==
		    (IFF_UP | IFF_RUNNING)) {
			ursu_init(ifp);
		}
		error = 0;
	}

	splx(s);

	return (error);
}

/*
 * Power on sequence for A-cut adapters.
 */
static void
ursu_power_on_acut(struct ursu_softc *sc)
{
	uint32_t reg;

	DPRINTFN(9, ("%s: %s\n", device_xname(sc->sc_dev), __func__));

	ursu_write_1(sc, R92S_SPS0_CTRL + 1, 0x53);
	ursu_write_1(sc, R92S_SPS0_CTRL + 0, 0x57);

	/* Enable AFE macro block's bandgap and Mbias. */
	ursu_write_1(sc, R92S_AFE_MISC,
	    ursu_read_1(sc, R92S_AFE_MISC) |
	    R92S_AFE_MISC_BGEN | R92S_AFE_MISC_MBEN);
	/* Enable LDOA15 block. */
	ursu_write_1(sc, R92S_LDOA15_CTRL,
	    ursu_read_1(sc, R92S_LDOA15_CTRL) | R92S_LDA15_EN);

	ursu_write_1(sc, R92S_SPS1_CTRL,
	    ursu_read_1(sc, R92S_SPS1_CTRL) | R92S_SPS1_LDEN);
	usbd_delay_ms(sc->sc_udev, 2);
	/* Enable switch regulator block. */
	ursu_write_1(sc, R92S_SPS1_CTRL,
	    ursu_read_1(sc, R92S_SPS1_CTRL) | R92S_SPS1_SWEN);

	ursu_write_4(sc, R92S_SPS1_CTRL, 0x00a7b267);

	ursu_write_1(sc, R92S_SYS_ISO_CTRL + 1,
	    ursu_read_1(sc, R92S_SYS_ISO_CTRL + 1) | 0x08);

	ursu_write_1(sc, R92S_SYS_FUNC_EN + 1,
	    ursu_read_1(sc, R92S_SYS_FUNC_EN + 1) | 0x20);

	ursu_write_1(sc, R92S_SYS_ISO_CTRL + 1,
	    ursu_read_1(sc, R92S_SYS_ISO_CTRL + 1) & ~0x90);

	/* Enable AFE clock. */
	ursu_write_1(sc, R92S_AFE_XTAL_CTRL + 1,
	    ursu_read_1(sc, R92S_AFE_XTAL_CTRL + 1) & ~0x04);
	/* Enable AFE PLL macro block. */
	ursu_write_1(sc, R92S_AFE_PLL_CTRL,
	    ursu_read_1(sc, R92S_AFE_PLL_CTRL) | 0x11);
	/* Attach AFE PLL to MACTOP/BB. */
	ursu_write_1(sc, R92S_SYS_ISO_CTRL,
	    ursu_read_1(sc, R92S_SYS_ISO_CTRL) & ~0x11);

	/* Switch to 40MHz clock instead of 80MHz. */
	ursu_write_2(sc, R92S_SYS_CLKR,
	    ursu_read_2(sc, R92S_SYS_CLKR) & ~R92S_SYS_CLKSEL);

	/* Enable MAC clock. */
	ursu_write_2(sc, R92S_SYS_CLKR,
	    ursu_read_2(sc, R92S_SYS_CLKR) | R92S_MAC_CLK_EN | R92S_SYS_CLK_EN);

	ursu_write_1(sc, R92S_PMC_FSM, 0x02);

	/* Enable digital core and IOREG R/W. */
	ursu_write_1(sc, R92S_SYS_FUNC_EN + 1,
	    ursu_read_1(sc, R92S_SYS_FUNC_EN + 1) | 0x08);

	ursu_write_1(sc, R92S_SYS_FUNC_EN + 1,
	    ursu_read_1(sc, R92S_SYS_FUNC_EN + 1) | 0x80);

	/* Switch the control path to firmware. */
	reg = ursu_read_2(sc, R92S_SYS_CLKR);
	reg = (reg & ~R92S_SWHW_SEL) | R92S_FWHW_SEL;
	ursu_write_2(sc, R92S_SYS_CLKR, reg);

	ursu_write_2(sc, R92S_CR, 0x37fc);

	/* Fix USB RX FIFO issue. */
	ursu_write_1(sc, 0xfe5c, ursu_read_1(sc, 0xfe5c) | 0x80);
	ursu_write_1(sc, 0x00ab, ursu_read_1(sc, 0x00ab) | 0xc0);

	ursu_write_1(sc, R92S_SYS_CLKR,
	    ursu_read_1(sc, R92S_SYS_CLKR) & ~R92S_SYS_CPU_CLKSEL);
}

/*
 * Power on sequence for B-cut and C-cut adapters.
 */
static void
ursu_power_on_bcut(struct ursu_softc *sc)
{
	uint32_t reg;
	int ntries;

	DPRINTFN(9, ("%s: %s\n", device_xname(sc->sc_dev), __func__));

	/* Prevent eFuse leakage. */
	ursu_write_1(sc, 0x37, 0xb0);
	usbd_delay_ms(sc->sc_udev, 10);
	ursu_write_1(sc, 0x37, 0x30);

	/* Switch the control path to hardware. */
	reg = ursu_read_2(sc, R92S_SYS_CLKR);
	if (reg & R92S_FWHW_SEL) {
		ursu_write_2(sc, R92S_SYS_CLKR,
		    reg & ~(R92S_SWHW_SEL | R92S_FWHW_SEL));
	}
	ursu_write_1(sc, R92S_SYS_FUNC_EN + 1,
	    ursu_read_1(sc, R92S_SYS_FUNC_EN + 1) & ~0x8c);
	DELAY(1000);

	ursu_write_1(sc, R92S_SPS0_CTRL + 1, 0x53);
	ursu_write_1(sc, R92S_SPS0_CTRL + 0, 0x57);

	reg = ursu_read_1(sc, R92S_AFE_MISC);
	ursu_write_1(sc, R92S_AFE_MISC, reg | R92S_AFE_MISC_BGEN);
	ursu_write_1(sc, R92S_AFE_MISC, reg | R92S_AFE_MISC_BGEN |
	    R92S_AFE_MISC_MBEN | R92S_AFE_MISC_I32_EN);

	/* Enable PLL. */
	ursu_write_1(sc, R92S_LDOA15_CTRL,
	    ursu_read_1(sc, R92S_LDOA15_CTRL) | R92S_LDA15_EN);

	ursu_write_1(sc, R92S_LDOV12D_CTRL,
	    ursu_read_1(sc, R92S_LDOV12D_CTRL) | R92S_LDV12_EN);

	ursu_write_1(sc, R92S_SYS_ISO_CTRL + 1,
	    ursu_read_1(sc, R92S_SYS_ISO_CTRL + 1) | 0x08);

	ursu_write_1(sc, R92S_SYS_FUNC_EN + 1,
	    ursu_read_1(sc, R92S_SYS_FUNC_EN + 1) | 0x20);

	/* Support 64KB IMEM. */
	ursu_write_1(sc, R92S_SYS_ISO_CTRL + 1,
	    ursu_read_1(sc, R92S_SYS_ISO_CTRL + 1) & ~0x97);

	/* Enable AFE clock. */
	ursu_write_1(sc, R92S_AFE_XTAL_CTRL + 1,
	    ursu_read_1(sc, R92S_AFE_XTAL_CTRL + 1) & ~0x04);
	/* Enable AFE PLL macro block. */
	reg = ursu_read_1(sc, R92S_AFE_PLL_CTRL);
	ursu_write_1(sc, R92S_AFE_PLL_CTRL, reg | 0x11);
	DELAY(500);
	ursu_write_1(sc, R92S_AFE_PLL_CTRL, reg | 0x51);
	DELAY(500);
	ursu_write_1(sc, R92S_AFE_PLL_CTRL, reg | 0x11);
	DELAY(500);

	/* Attach AFE PLL to MACTOP/BB. */
	ursu_write_1(sc, R92S_SYS_ISO_CTRL,
	    ursu_read_1(sc, R92S_SYS_ISO_CTRL) & ~0x11);

	/* Switch to 40MHz clock. */
	ursu_write_1(sc, R92S_SYS_CLKR, 0x00);
	/* Disable CPU clock and 80MHz SSC. */
	ursu_write_1(sc, R92S_SYS_CLKR,
	    ursu_read_1(sc, R92S_SYS_CLKR) | 0xa0);
	/* Enable MAC clock. */
	ursu_write_2(sc, R92S_SYS_CLKR,
	    ursu_read_2(sc, R92S_SYS_CLKR) | R92S_MAC_CLK_EN | R92S_SYS_CLK_EN);

	ursu_write_1(sc, R92S_PMC_FSM, 0x02);

	/* Enable digital core and IOREG R/W. */
	ursu_write_1(sc, R92S_SYS_FUNC_EN + 1,
	    ursu_read_1(sc, R92S_SYS_FUNC_EN + 1) | 0x08);

	ursu_write_1(sc, R92S_SYS_FUNC_EN + 1,
	    ursu_read_1(sc, R92S_SYS_FUNC_EN + 1) | 0x80);

	/* Switch the control path to firmware. */
	reg = ursu_read_2(sc, R92S_SYS_CLKR);
	reg = (reg & ~R92S_SWHW_SEL) | R92S_FWHW_SEL;
	ursu_write_2(sc, R92S_SYS_CLKR, reg);

	ursu_write_2(sc, R92S_CR, 0x37fc);

	/* Fix USB RX FIFO issue. */
	ursu_write_1(sc, 0xfe5c, ursu_read_1(sc, 0xfe5c) | 0x80);

	ursu_write_1(sc, R92S_SYS_CLKR,
	    ursu_read_1(sc, R92S_SYS_CLKR) & ~R92S_SYS_CPU_CLKSEL);

	ursu_write_1(sc, 0xfe1c, 0x80);

	/* Make sure TxDMA is ready to download firmware. */
	for (ntries = 0; ntries < 20; ntries++) {
		reg = ursu_read_1(sc, R92S_TCR);
		if ((reg & (R92S_TCR_IMEM_CHK_RPT | R92S_TCR_EMEM_CHK_RPT)) ==
		    (R92S_TCR_IMEM_CHK_RPT | R92S_TCR_EMEM_CHK_RPT))
			break;
		DELAY(5);
	}
	if (ntries == 20) {
		/* Reset TxDMA. */
		reg = ursu_read_1(sc, R92S_CR);
		ursu_write_1(sc, R92S_CR, reg & ~R92S_CR_TXDMA_EN);
		DELAY(2);
		ursu_write_1(sc, R92S_CR, reg | R92S_CR_TXDMA_EN);
	}
}

static void
ursu_power_off(struct ursu_softc *sc)
{

	DPRINTFN(9, ("%s: %s\n", device_xname(sc->sc_dev), __func__));

	/* Turn RF off. */
	ursu_write_1(sc, R92S_RF_CTRL, 0x00);
	usbd_delay_ms(sc->sc_udev, 5);

	/* Turn MAC off. */
	/* Switch control path. */
	ursu_write_1(sc, R92S_SYS_CLKR + 1, 0x38);
	/* Reset MACTOP. */
	ursu_write_1(sc, R92S_SYS_FUNC_EN + 1, 0x70);
	ursu_write_1(sc, R92S_PMC_FSM, 0x06);
	ursu_write_1(sc, R92S_SYS_ISO_CTRL + 0, 0xf9);
	ursu_write_1(sc, R92S_SYS_ISO_CTRL + 1, 0xe8);

	/* Disable AFE PLL. */
	ursu_write_1(sc, R92S_AFE_PLL_CTRL, 0x00);
	/* Disable A15V. */
	ursu_write_1(sc, R92S_LDOA15_CTRL, 0x54);
	/* Disable eFuse 1.2V. */
	ursu_write_1(sc, R92S_SYS_FUNC_EN + 1, 0x50);
	ursu_write_1(sc, R92S_LDOV12D_CTRL, 0x24);
	/* Enable AFE macro block's bandgap and Mbias. */
	ursu_write_1(sc, R92S_AFE_MISC, 0x30);
	/* Disable 1.6V LDO. */
	ursu_write_1(sc, R92S_SPS0_CTRL + 0, 0x56);
	ursu_write_1(sc, R92S_SPS0_CTRL + 1, 0x43);
}

static int
ursu_fw_loadsection(struct ursu_softc *sc, uint8_t *buf, int len)
{
	struct ursu_tx_data *data;
	struct r92s_tx_desc *txd;
	usbd_pipe_handle pipe;
	int mlen, error;

	DPRINTFN(9, ("%s: %s\n", device_xname(sc->sc_dev), __func__));

	data = sc->sc_fwcmd_data;
	pipe = sc->sc_pipe[sc->sc_qid2idx[RSU_QID_VO]];
	txd = (struct r92s_tx_desc *)data->buf;
	while (len > 0) {
		memset(txd, 0, sizeof(*txd));
		if (len <= (int)(RSU_TXBUFSZ - sizeof(*txd))) {
			/* Last chunk. */
			txd->txdw0 |= htole32(R92S_TXDW0_LINIP);
			mlen = len;
		} else
			mlen = RSU_TXBUFSZ - sizeof(*txd);
		txd->txdw0 |= htole32(SM(R92S_TXDW0_PKTLEN, mlen));
		memcpy(&txd[1], buf, mlen);

		usbd_setup_xfer(data->xfer, pipe, NULL, data->buf,
		    sizeof(*txd) + mlen, USBD_SHORT_XFER_OK | USBD_NO_COPY,
		    RSU_TX_TIMEOUT, NULL);
		error = usbd_sync_transfer(data->xfer);
		if (error != 0)
			return (error);
		buf += mlen;
		len -= mlen;
	}
	return (0);
}

static int
ursu_load_firmware(struct ursu_softc *sc)
{
	firmware_handle_t fwh;
	const char *fwname = "rsu-rtl8712fw";
#ifndef	IEEE80211_NO_HT
	struct ieee80211com *ic = &sc->sc_ic;
#endif
	struct r92s_fw_hdr *hdr;
	struct r92s_fw_priv *dmem;
	uint8_t *imem, *emem;
	int imemsz, ememsz;
	uint8_t *fw;
	size_t size;
	uint32_t reg;
	int ntries, error;

	DPRINTFN(9, ("%s: %s\n", device_xname(sc->sc_dev), __func__));

	/* Read firmware image from the filesystem. */
	if ((error = firmware_open("if_ursu", fwname, &fwh)) != 0) {
		aprint_error_dev(sc->sc_dev,
		    "failed loadfirmware of file %s (error %d)\n",
		    fwname, error);
		return (error);
	}
	size = firmware_get_size(fwh);
	fw = firmware_malloc(size);
	if (fw == NULL) {
		aprint_error_dev(sc->sc_dev,
		    "failed to allocate firmware memory\n");
		firmware_close(fwh);
		return (ENOMEM);
	}
	error = firmware_read(fwh, 0, fw, size);
	firmware_close(fwh);
	if (error != 0) {
		aprint_error_dev(sc->sc_dev,
		    "failed to read firmware (error %d)\n", error);
		firmware_free(fw, 0);
		return (error);
	}

	if (size < sizeof(*hdr)) {
		aprint_error_dev(sc->sc_dev, "firmware too short\n");
		error = EINVAL;
		goto fail;
	}
	hdr = (struct r92s_fw_hdr *)fw;
	if (hdr->signature != htole16(0x8712) &&
	    hdr->signature != htole16(0x8192)) {
		aprint_error_dev(sc->sc_dev,
		    "invalid firmware signature 0x%x\n",
		    le16toh(hdr->signature));
		error = EINVAL;
		goto fail;
	}
	DPRINTF(("%s: %s: FW V%d %02x-%02x %02x:%02x\n",
	    device_xname(sc->sc_dev), __func__, le16toh(hdr->version),
	    hdr->month, hdr->day, hdr->hour, hdr->minute));

	/* Make sure that driver and firmware are in sync. */
	if (hdr->privsz != htole32(sizeof(*dmem))) {
		aprint_error_dev(sc->sc_dev, "unsupported firmware image\n");
		error = EINVAL;
		goto fail;
	}
	/* Get FW sections sizes. */
	imemsz = le32toh(hdr->imemsz);
	ememsz = le32toh(hdr->sramsz);
	/* Check that all FW sections fit in image. */
	if (size < sizeof(*hdr) + imemsz + ememsz) {
		aprint_error_dev(sc->sc_dev, "firmware too short\n");
		error = EINVAL;
		goto fail;
	}
	imem = (uint8_t *)&hdr[1];
	emem = imem + imemsz;

	/* Load IMEM section. */
	error = ursu_fw_loadsection(sc, imem, imemsz);
	if (error != 0) {
		aprint_error_dev(sc->sc_dev,
		    "could not load firmware section %s\n", "IMEM");
		goto fail;
	}
	/* Wait for load to complete. */
	for (ntries = 0; ntries < 10; ntries++) {
		reg = ursu_read_2(sc, R92S_TCR);
		if (reg & R92S_TCR_IMEM_CODE_DONE)
			break;
		DELAY(10);
	}
	if (ntries == 10 || !(reg & R92S_TCR_IMEM_CHK_RPT)) {
		aprint_error_dev(sc->sc_dev,
		    "timeout waiting for %s transfer\n", "IMEM");
		error = ETIMEDOUT;
		goto fail;
	}

	/* Load EMEM section. */
	error = ursu_fw_loadsection(sc, emem, ememsz);
	if (error != 0) {
		aprint_error_dev(sc->sc_dev,
		    "could not load firmware section %s\n", "EMEM");
		goto fail;
	}
	/* Wait for load to complete. */
	for (ntries = 0; ntries < 10; ntries++) {
		reg = ursu_read_2(sc, R92S_TCR);
		if (reg & R92S_TCR_EMEM_CODE_DONE)
			break;
		DELAY(10);
	}
	if (ntries == 10 || !(reg & R92S_TCR_EMEM_CHK_RPT)) {
		aprint_error_dev(sc->sc_dev,
		    "timeout waiting for %s transfer\n", "EMEM");
		error = ETIMEDOUT;
		goto fail;
	}

	/* Enable CPU. */
	ursu_write_1(sc, R92S_SYS_CLKR,
	    ursu_read_1(sc, R92S_SYS_CLKR) | R92S_SYS_CPU_CLKSEL);
	if (!(ursu_read_1(sc, R92S_SYS_CLKR) & R92S_SYS_CPU_CLKSEL)) {
		aprint_error_dev(sc->sc_dev,
		    "could not enable system clock\n");
		error = EIO;
		goto fail;
	}
	ursu_write_2(sc, R92S_SYS_FUNC_EN,
	    ursu_read_2(sc, R92S_SYS_FUNC_EN) | R92S_FEN_CPUEN);
	if (!(ursu_read_2(sc, R92S_SYS_FUNC_EN) & R92S_FEN_CPUEN)) {
		aprint_error_dev(sc->sc_dev,
		    "could not enable microcontroller\n");
		error = EIO;
		goto fail;
	}
	/* Wait for CPU to initialize. */
	for (ntries = 0; ntries < 100; ntries++) {
		if (ursu_read_2(sc, R92S_TCR) & R92S_TCR_IMEM_RDY)
			break;
		DELAY(1000);
	}
	if (ntries == 100) {
		aprint_error_dev(sc->sc_dev,
		    "timeout waiting for microcontroller\n");
		error = ETIMEDOUT;
		goto fail;
	}

	/* Update DMEM section before loading. */
	dmem = &hdr->priv;
	memset(dmem, 0, sizeof(*dmem));
	dmem->hci_sel = R92S_HCI_SEL_USB | R92S_HCI_SEL_8172;
	dmem->nendpoints = sc->sc_npipes;
	dmem->rf_config = 0x11;	/* 1T1R */
	dmem->vcs_type = R92S_VCS_TYPE_AUTO;
	dmem->vcs_mode = R92S_VCS_MODE_RTS_CTS;
#ifndef	IEEE80211_NO_HT
	dmem->bw40_en = (ic->ic_htcaps & IEEE80211_HTCAP_CBW20_40) != 0;
	dmem->turbo_mode = 1;
#endif
	/* Load DMEM section. */
	error = ursu_fw_loadsection(sc, (uint8_t *)dmem, sizeof(*dmem));
	if (error != 0) {
		aprint_error_dev(sc->sc_dev,
		    "could not load firmware section %s\n", "DMEM");
		goto fail;
	}
	/* Wait for load to complete. */
	for (ntries = 0; ntries < 100; ntries++) {
		if (ursu_read_2(sc, R92S_TCR) & R92S_TCR_DMEM_CODE_DONE)
			break;
		DELAY(1000);
	}
	if (ntries == 100) {
		aprint_error_dev(sc->sc_dev,
		    "timeout waiting for %s transfer\n", "DMEM");
		error = ETIMEDOUT;
		goto fail;
	}
	/* Wait for firmware readiness. */
	for (ntries = 0; ntries < 60; ntries++) {
		if (!(ursu_read_2(sc, R92S_TCR) & R92S_TCR_FWRDY))
			break;
		DELAY(1000);
	}
	if (ntries == 60) {
		aprint_error_dev(sc->sc_dev,
		    "timeout waiting for firmware readiness\n");
		error = ETIMEDOUT;
		goto fail;
	}
 fail:
	free(fw, M_DEVBUF);
	return (error);
}

static int
ursu_init(struct ifnet *ifp)
{
	struct ursu_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	struct r92s_set_pwr_mode cmd;
	struct ursu_rx_data *data;
	int i, error;

	DPRINTFN(9, ("%s: %s\n", device_xname(sc->sc_dev), __func__));

	ursu_stop(ifp, 0);

	/* Init host async commands ring. */
	mutex_enter(&sc->sc_task_mtx);
	sc->sc_cmdq.cur = sc->sc_cmdq.next = sc->sc_cmdq.queued = 0;
	mutex_exit(&sc->sc_task_mtx);

	/* Allocate Tx/Rx buffers. */
	error = ursu_alloc_rx_list(sc);
	if (error != 0) {
		aprint_error_dev(sc->sc_dev,
		    "could not allocate Rx buffers\n");
		goto fail;
	}
	error = ursu_alloc_tx_list(sc);
	if (error != 0) {
		aprint_error_dev(sc->sc_dev,
		    "could not allocate Tx buffers\n");
		goto fail;
	}

	/* Reserve one Tx buffer for firmware commands. */
	sc->sc_fwcmd_data = TAILQ_FIRST(&sc->sc_tx_free_list);
	TAILQ_REMOVE(&sc->sc_tx_free_list, sc->sc_fwcmd_data, next);

	/* Power on adapter. */
	if (sc->sc_cut == 1)
		ursu_power_on_acut(sc);
	else
		ursu_power_on_bcut(sc);

	/* Load firmware. */
	error = ursu_load_firmware(sc);
	if (error != 0)
		goto fail;

	/* Enable Rx TCP checksum offload. */
	ursu_write_4(sc, R92S_RCR, ursu_read_4(sc, R92S_RCR) | 0x04000000);
	/* Append PHY status. */
	ursu_write_4(sc, R92S_RCR, ursu_read_4(sc, R92S_RCR) | 0x02000000);

	ursu_write_4(sc, R92S_CR, ursu_read_4(sc, R92S_CR) & ~0xff000000);

	/* Use 128 bytes pages. */
	ursu_write_1(sc, 0x00b5, ursu_read_1(sc, 0x00b5) | 0x01);
	/* Enable USB Rx aggregation. */
	ursu_write_1(sc, 0x00bd, ursu_read_1(sc, 0x00bd) | 0x80);
	/* Set USB Rx aggregation threshold. */
	ursu_write_1(sc, 0x00d9, 0x01);
	/* Set USB Rx aggregation timeout (1.7ms/4). */
	ursu_write_1(sc, 0xfe5b, 0x04);
	/* Fix USB Rx FIFO issue. */
	ursu_write_1(sc, 0xfe5c, ursu_read_1(sc, 0xfe5c) | 0x80);

	/* Set MAC address. */
	IEEE80211_ADDR_COPY(ic->ic_myaddr, CLLADDR(ifp->if_sadl));
	ursu_write_region_1(sc, R92S_MACID, ic->ic_myaddr, IEEE80211_ADDR_LEN);

	/* Queue Rx xfers (XXX C2H pipe for 11-pipe configurations?) */
	for (i = 0; i < RSU_RX_LIST_COUNT; i++) {
		data = &sc->sc_rx_data[i];
		data->pipe = sc->sc_pipe[sc->sc_qid2idx[RSU_QID_RXOFF]];
		usbd_setup_xfer(data->xfer, data->pipe, data, data->buf,
		    RSU_RXBUFSZ, USBD_SHORT_XFER_OK | USBD_NO_COPY,
		    USBD_NO_TIMEOUT, ursu_rxeof);
		error = usbd_transfer(data->xfer);
		if (__predict_false(error != USBD_NORMAL_COMPLETION &&
		    error != USBD_IN_PROGRESS))
			goto fail;
	}

	/* NB: it really takes that long for firmware to boot. */
	usbd_delay_ms(sc->sc_udev, 1500);

	DPRINTF(("%s: %s: setting MAC address to %s\n",
	    device_xname(sc->sc_dev), __func__, ether_sprintf(ic->ic_myaddr)));
	error = ursu_fw_cmd(sc, R92S_CMD_SET_MAC_ADDRESS, ic->ic_myaddr,
	    IEEE80211_ADDR_LEN, NULL);
	if (error != 0) {
		aprint_error_dev(sc->sc_dev, "could not set MAC address\n");
		goto fail;
	}

	ursu_write_1(sc, R92S_USB_HRPWM,
	    R92S_USB_HRPWM_PS_ST_ACTIVE | R92S_USB_HRPWM_PS_ALL_ON);

	memset(&cmd, 0, sizeof(cmd));
	cmd.mode = R92S_PS_MODE_ACTIVE;
	DPRINTF(("%s: %s: setting ps mode to %d\n", device_xname(sc->sc_dev),
	    __func__, cmd.mode));
	error = ursu_fw_cmd(sc, R92S_CMD_SET_PWR_MODE, &cmd, sizeof(cmd), NULL);
	if (error != 0) {
		aprint_error_dev(sc->sc_dev, "could not set PS mode\n");
		goto fail;
	}

#ifndef	IEEE80211_NO_HT
	if (ic->ic_htcaps & IEEE80211_HTCAP_CBW20_40) {
		/* Enable 40MHz mode. */
		erro
		error = ursu_fw_iocmd(sc,
		    SM(R92S_IOCMD_CLASS, 0xf4) |
		    SM(R92S_IOCMD_INDEX, 0x00) |
		    SM(R92S_IOCMD_VALUE, 0x0007));
		if (error != 0) {
			aprint_error_dev(sc->sc_dev,
			    "could not enable 40MHz mode\n");
			goto fail;
		}
	}
#endif

	/* Set default channel. */
	ic->ic_bss->ni_chan = ic->ic_ibss_chan;

	/* We're ready to go. */
	ifp->if_flags &= ~IFF_OACTIVE;
	ifp->if_flags |= IFF_RUNNING;

	sc->sc_scan_pass = 0;
	ieee80211_begin_scan(ic, 1);
	return (0);

 fail:
	ursu_stop(ifp, 1);
	return (error);
}

static void
ursu_stop(struct ifnet *ifp, int disable)
{
	struct ursu_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	size_t i;
	int s;

	DPRINTFN(9, ("%s: %s\n", device_xname(sc->sc_dev), __func__));

	s = splusb();
	ieee80211_new_state(ic, IEEE80211_S_INIT, -1);
	ursu_wait_async(sc);
	splx(s);

	sc->sc_tx_timer = 0;
	ifp->if_timer = 0;
	ifp->if_flags &= ~(IFF_RUNNING | IFF_OACTIVE);

	callout_stop(&sc->sc_calib_to);

	/* Abort Tx/Rx. */
	for (i = 0; i < sc->sc_npipes; i++)
		usbd_abort_pipe(sc->sc_pipe[i]);

	/* Free Tx/Rx buffers. */
	ursu_free_tx_list(sc);
	ursu_free_rx_list(sc);

	if (disable)
		ursu_power_off(sc);
}

#ifdef URSU_DEBUG
static void
ursu_dump_data(const char *title, const void *ptr, size_t size)
{
	char buf[80];
	char cbuf[16];
	char t[4];
	const uint8_t *p = ptr;
	size_t i, j;

	printf("%s: %s\n", __func__, (title != NULL) ? title : "");
	printf("--------+--------------------------------------------------+------------------+\n");
	printf("offset  | +0 +1 +2 +3 +4 +5 +6 +7  +8 +9 +a +b +c +d +e +f | data             |\n");
	printf("--------+--------------------------------------------------+------------------+\n");
	for (i = 0; i < size; i++) {
		if ((i % 16) == 0) {
			snprintf(buf, sizeof(buf), "%08x| ", i);
		} else if ((i % 16) == 8) {
			strlcat(buf, " ", sizeof(buf));
		}

		snprintf(t, sizeof(t), "%02x ", p[i]);
		strlcat(buf, t, sizeof(buf));
		cbuf[i % 16] = p[i];

		if ((i % 16) == 15) {
			strlcat(buf, "| ", sizeof(buf));
			for (j = 0; j < 16; j++) {
				if (cbuf[j] >= 0x20 && cbuf[j] <= 0x7e) {
					snprintf(t, sizeof(t), "%c", cbuf[j]);
					strlcat(buf, t, sizeof(buf));
				} else {
					strlcat(buf, ".", sizeof(buf));
				}
			}
			printf("%s |\n", buf);
		}
	}
	j = i % 16;
	if (j != 0) {
		for (; j < 16; j++) {
			strlcat(buf, "   ", sizeof(buf));
			if ((j % 16) == 8) {
				strlcat(buf, " ", sizeof(buf));
			}
		}

		strlcat(buf, "| ", sizeof(buf));
		for (j = 0; j < (i % 16); j++) {
			if (cbuf[j] >= 0x20 && cbuf[j] <= 0x7e) {
				snprintf(t, sizeof(t), "%c", cbuf[j]);
				strlcat(buf, t, sizeof(buf));
			} else {
				strlcat(buf, ".", sizeof(buf));
			}
		}
		for (; j < 16; j++) {
			strlcat(buf, " ", sizeof(buf));
		}
		printf("%s |\n", buf);
	}
	printf("--------+--------------------------------------------------+------------------+\n");
}
#endif

MODULE(MODULE_CLASS_DRIVER, if_ursu, "bpf");

#ifdef _MODULE
#include "ioconf.c"
#endif

static int
if_ursu_modcmd(modcmd_t cmd, void *aux)
{
	int error = 0;

	switch (cmd) {
	case MODULE_CMD_INIT:
#ifdef _MODULE
		error = config_init_component(cfdriver_ioconf_ursu,
		    cfattach_ioconf_ursu, cfdata_ioconf_ursu);
#endif
		return (error);
	case MODULE_CMD_FINI:
#ifdef _MODULE
		error = config_fini_component(cfdriver_ioconf_ursu,
		    cfattach_ioconf_ursu, cfdata_ioconf_ursu);
#endif
		return (error);
	default:
		return (ENOTTY);
	}
}

