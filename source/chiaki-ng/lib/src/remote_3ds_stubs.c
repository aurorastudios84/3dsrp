/*
 * remote_3ds_stubs.c -- Nintendo 3DS port
 *
 * The Nintendo 3DS build deliberately leaves out "Remote Play over the
 * internet": it needs miniupnpc (UPnP port mapping) and libcurl websockets,
 * neither of which is available on, or needed for, couch-local streaming on
 * the console.  See lib/CMakeLists.txt, where src/remote/holepunch.c,
 * src/remote/rudp.c and src/remote/rudpsendbuffer.c are removed for this
 * target.
 *
 * Parts of the *local* path are nevertheless shared with that feature and
 * still reference its teardown/probe entry points: session.c keeps a
 * ChiakiRudp / ChiakiHolepunchSession around and frees or interrogates it.
 * Instead of dragging the whole feature back in, this file supplies those
 * entry points as inert stubs, so the library links and the local streaming
 * path stays clean.
 *
 * Every stub behaves as "feature absent": handles come back NULL, and the
 * send/query entry points report CHIAKI_ERR_UNINITIALIZED, which callers
 * already treat exactly like a failed remote connection.  Nothing here ever
 * touches a socket or a key: it is a compile/link boundary, not a fake
 * implementation.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <chiaki/remote/rudp.h>
#include <chiaki/remote/holepunch.h>
#include <chiaki/log.h>
#include <string.h>

/* ------------------------------------------------------------------ *
 * R-UDP -- the reliable transport used for internet play.
 * ------------------------------------------------------------------ */

ChiakiRudp chiaki_rudp_init(chiaki_socket_t *sock, ChiakiLog *log)
{
	(void)sock;
	if(log)
		CHIAKI_LOGW(log, "R-UDP (Remote Play over the internet) is not part of this 3DS build; not initialising");
	return NULL;
}

ChiakiErrorCode chiaki_rudp_fini(ChiakiRudp rudp)
{
	(void)rudp;
	return CHIAKI_ERR_SUCCESS;
}

void chiaki_rudp_reset_counter_header(ChiakiRudp rudp)
{
	(void)rudp;
}

ChiakiErrorCode chiaki_rudp_send_init_message(ChiakiRudp rudp)
{
	(void)rudp;
	return CHIAKI_ERR_UNINITIALIZED;
}

ChiakiErrorCode chiaki_rudp_send_cookie_message(ChiakiRudp rudp, uint8_t *response_buf, size_t response_size)
{
	(void)rudp; (void)response_buf; (void)response_size;
	return CHIAKI_ERR_UNINITIALIZED;
}

ChiakiErrorCode chiaki_rudp_send_session_message(ChiakiRudp rudp, uint16_t remote_counter,
	uint8_t *session_msg, size_t session_msg_size)
{
	(void)rudp; (void)remote_counter; (void)session_msg; (void)session_msg_size;
	return CHIAKI_ERR_UNINITIALIZED;
}

ChiakiErrorCode chiaki_rudp_send_ack_message(ChiakiRudp rudp, uint16_t remote_counter)
{
	(void)rudp; (void)remote_counter;
	return CHIAKI_ERR_UNINITIALIZED;
}

ChiakiErrorCode chiaki_rudp_send_ctrl_message(ChiakiRudp rudp, uint8_t *ctrl_message, size_t ctrl_message_size)
{
	(void)rudp; (void)ctrl_message; (void)ctrl_message_size;
	return CHIAKI_ERR_UNINITIALIZED;
}

ChiakiErrorCode chiaki_rudp_send_switch_to_stream_connection_message(ChiakiRudp rudp)
{
	(void)rudp;
	return CHIAKI_ERR_UNINITIALIZED;
}

ChiakiErrorCode chiaki_rudp_send_raw(ChiakiRudp rudp, uint8_t *buf, size_t buf_size)
{
	(void)rudp; (void)buf; (void)buf_size;
	return CHIAKI_ERR_UNINITIALIZED;
}

ChiakiErrorCode chiaki_rudp_send_recv(ChiakiRudp rudp, RudpMessage *message, uint8_t *buf, size_t buf_size,
	uint16_t remote_counter, RudpPacketType send_type, RudpPacketType recv_type,
	size_t min_data_size, size_t tries)
{
	(void)rudp; (void)message; (void)buf; (void)buf_size; (void)remote_counter;
	(void)send_type; (void)recv_type; (void)min_data_size; (void)tries;
	return CHIAKI_ERR_UNINITIALIZED;
}

ChiakiErrorCode chiaki_rudp_select_recv(ChiakiRudp rudp, size_t buf_size, RudpMessage *message)
{
	(void)rudp; (void)buf_size; (void)message;
	return CHIAKI_ERR_UNINITIALIZED;
}

ChiakiErrorCode chiaki_rudp_recv_only(ChiakiRudp rudp, size_t buf_size, RudpMessage *message)
{
	(void)rudp; (void)buf_size; (void)message;
	return CHIAKI_ERR_UNINITIALIZED;
}

ChiakiErrorCode chiaki_rudp_stop_pipe_select_single(ChiakiRudp rudp, ChiakiStopPipe *stop_pipe, uint64_t timeout)
{
	(void)rudp; (void)stop_pipe; (void)timeout;
	return CHIAKI_ERR_UNINITIALIZED;
}

ChiakiErrorCode chiaki_rudp_ack_packet(ChiakiRudp rudp, uint16_t counter_to_ack)
{
	(void)rudp; (void)counter_to_ack;
	return CHIAKI_ERR_UNINITIALIZED;
}

void chiaki_rudp_print_message(ChiakiRudp rudp, RudpMessage *message)
{
	(void)rudp; (void)message;
}

void chiaki_rudp_message_pointers_free(RudpMessage *message)
{
	(void)message;
}

/* ------------------------------------------------------------------ *
 * holepunch -- PSN NAT traversal, only ever used for internet play.
 * ------------------------------------------------------------------ */

ChiakiErrorCode chiaki_holepunch_session_create(ChiakiHolepunchSession session)
{
	(void)session;
	return CHIAKI_ERR_UNINITIALIZED;
}

ChiakiErrorCode chiaki_holepunch_session_start(ChiakiHolepunchSession session, const uint8_t *console_uid,
	ChiakiHolepunchConsoleType console_type)
{
	(void)session; (void)console_uid; (void)console_type;
	return CHIAKI_ERR_UNINITIALIZED;
}

ChiakiErrorCode chiaki_holepunch_upnp_discover(ChiakiHolepunchSession session)
{
	(void)session;
	return CHIAKI_ERR_UNINITIALIZED;
}

ChiakiErrorCode holepunch_session_create_offer(ChiakiHolepunchSession session)
{
	(void)session;
	return CHIAKI_ERR_UNINITIALIZED;
}

ChiakiErrorCode chiaki_holepunch_session_punch_hole(ChiakiHolepunchSession session, ChiakiHolepunchPortType port_type)
{
	(void)session; (void)port_type;
	return CHIAKI_ERR_UNINITIALIZED;
}

void chiaki_holepunch_session_set_port_guessing_socks(ChiakiHolepunchSession session, int count)
{
	(void)session; (void)count;
}

void chiaki_holepunch_main_thread_cancel(ChiakiHolepunchSession session, bool stop_thread)
{
	(void)session; (void)stop_thread;
}

void chiaki_holepunch_session_fini(ChiakiHolepunchSession session)
{
	(void)session;
}

/* ------------------------------------------------------------------ *
 * Queries session.c and ctrl.c make of a holepunch session while setting
 * up the *local* connection (the remote-feature flags on that handle are
 * simply read). The handle is always NULL on this target, so these
 * branches are never taken at run time; the entry points exist so the
 * call sites link.
 * ------------------------------------------------------------------ */

ChiakiHolepunchRegistInfo chiaki_get_regist_info(ChiakiHolepunchSession session)
{
	ChiakiHolepunchRegistInfo info;
	(void)session;
	memset(&info, 0, sizeof(info));
	return info;
}

uint16_t chiaki_get_ps_ctrl_port(ChiakiHolepunchSession session)
{
	(void)session;
	return 0;
}

chiaki_socket_t *chiaki_get_holepunch_sock(ChiakiHolepunchSession session, ChiakiHolepunchPortType type)
{
	(void)session;
	(void)type;
	return NULL;
}

/* Remainder of the remote-feature surface. Nothing in session.c or ctrl.c on
 * the local path calls these (the handle is NULL, so every branch that leads
 * here is skipped), but they are part of the exported library surface and are
 * supplied so the archive is complete. */

void chiaki_get_ps_selected_addr(ChiakiHolepunchSession session, char *ps_ip)
{
	(void)session;
	if (ps_ip) ps_ip[0] = 0;
}

bool chiaki_holepunch_session_get_stun_allocation(ChiakiHolepunchSession session,
                                                  int32_t *allocation_increment,
                                                  bool *random_allocation)
{
	(void)session;
	(void)allocation_increment;
	(void)random_allocation;
	return false;
}

ChiakiErrorCode chiaki_holepunch_generate_client_device_uid(char *out, size_t *out_size)
{
	(void)out;
	(void)out_size;
	return CHIAKI_ERR_UNINITIALIZED;
}

ChiakiHolepunchSession chiaki_holepunch_session_init(const char *psn_oauth2_token, ChiakiLog *log)
{
	(void)psn_oauth2_token;
	(void)log;
	return NULL;
}

ChiakiErrorCode chiaki_holepunch_list_devices(const char *psn_oauth2_token,
                                              ChiakiHolepunchConsoleType console_type,
                                              ChiakiHolepunchDeviceInfo **devices,
                                              size_t *device_count, ChiakiLog *log)
{
	(void)psn_oauth2_token;
	(void)console_type;
	(void)log;
	if (devices) *devices = NULL;
	if (device_count) *device_count = 0;
	return CHIAKI_ERR_UNINITIALIZED;
}

void chiaki_holepunch_free_device_list(ChiakiHolepunchDeviceInfo **devices)
{
	(void)devices;
}

void chiaki_holepunch_session_force_port_guessing(ChiakiHolepunchSession session, bool enabled)
{
	(void)session;
	(void)enabled;
}

void chiaki_holepunch_session_set_ws_fqdn_override(ChiakiHolepunchSession session, const char *fqdn)
{
	(void)session;
	(void)fqdn;
}

void chiaki_holepunch_session_set_port_guessing_ports(ChiakiHolepunchSession session, int count)
{
	(void)session;
	(void)count;
}
