/**
 * Network protocol in WenTanLogServer.
 * Author: LiWentan.
 * Date: 2019/7/16.
 *
 * From Client to Server:
 *     Send log: [head(16)][time(32)][level(16)][hash_id(32)][content_size(16)][content(variable_length)].
 *     Initialize conncetion shake hand: [head(16)].
 *     Disconnect: [head(16)].
 *
 * From Server to Client:
 *     Reply log: Directly transmit the package from Lander.
 *     Initialize conncetion shake hand reply: [head(16)].
 *     Disconnect reply: [head(16)].
 *
 * From Server to Lander:
 *     Send log: Directly transmit the package from Client.
 *     Send search request: [head(16)][level(16)][hash_id(32)][start_time(32)]
 *         [end_time(32)][content_size(16)][content(variable_length)].
 *
 * From Lander to Server:
 *     Reply log: [head(16)][hash_id(32)][reply_message_size(16)][reply_message(variable_length)].
 *     Reply search request: [head(16)][hash_id(32)][message_number(16)][msg_1_size(16)][msg_1_content][msg_2_size(16)]...
 */

#ifndef _WTLOG_CLIENT_PROTOCOL_
#define _WTLOG_CLIENT_PROTOCOL_
 
#include <iostream>
#include <stdio.h>

static const bool debug_mode = true; // Set true to get more log.

using std::string;

namespace wtlog {
   
namespace {

// Head from client to server.
const uint16_t h_authorize_info = 2560; // Tell server this client is ready.
const uint16_t h_close_head = 2561; // Tell server this client won't send more logs.
const uint16_t h_send_log = 2562; // Tell server this is a log.
const uint16_t h_send_log_need_reply = 2563; // Tell server this is a log and need reply.

// Head from server to client.
const uint16_t h_authorize_ret = 9766; // Tell client this server is ready to receive log.
const uint16_t h_close_ret = 9767; // Tell client this server have known the client is closed.
const uint16_t h_log_receive_success = 9768; // Tell client this is a reply to a log.

// Head from lander to server.
const uint16_t h_handshake_info = 1101; // Tell server this lander is ready to receive logs.
const uint16_t h_stop_send_log = 1102; // Tell server do not send log and search request to this lander.
const uint16_t h_close_with_lander = 1103; // Tell server this lander won't send anything(e.g., search result) to server.
const uint16_t h_search_fin = 1104; // Tell server this is a package containing the search result.
extern const uint16_t h_log_receive_success; // Tell client this is a reply to a log.

// Head from server to lander.
const uint16_t h_handshake_ret = 8455; // Tell lander this server know the existing of the lander. Start to send log.
extern const uint16_t h_send_log; // Tell lander this is a log.
extern const uint16_t h_send_log_need_reply; // Tell lander this is a log. Need reply to server after finish printing.
const uint16_t h_search_request = 8457; // Tell lander this is a search request.
const uint16_t h_stop_send_log_reply = 8458; // Tell lander this server won't send any log to the lander.
const uint16_t h_close_with_lander_reply = 8459; // Tell lander this server won't receive any message from the lander.

// Other.
const char log_disk_tail_tag = -1; // This byte indicate this may be the tail of one log in disk file (Not guarntee since log may be binary).
const char log_disk_head_tag = 1; // This byte indicate this may be the head of one log in disk file (Not guarntee since log may be binary).

} // End anonoymous namespace.

enum LogLevel {
    info = 0,
    debug = 1,
    warning = 2,
    error = 3
};

enum CallBackStat {
    success = 0,
    failed = 1,
    timeout = 2
};

enum Command {
    disconnect = 0, // Tell server the client is close, server will close the tcp connection.
    stop_send_log = 1, // Stop send log, close the tcp connection with the lander.
    close_with_lander = 2, // Tell the receiver that it can safely close the tcp connection.
    write_log_ret = 3, // Tell the server that lander writed log successfully.
    search_ret = 4, // Tell the server this is the reply of a search request.
    stop_immediately = 5 // Tell the server this lander(client) has some problem, server can clean the resources.
};

struct CallBackInfo {
    CallBackStat status;
    string message;
};

} // End namespace wtlog.

#endif // End ifdef _WTLOG_CLIENT_PROTOCOL_.
