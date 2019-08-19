/**
 * Server connects the client and lander.
 * Server is the control hub.
 * Author: LiWentan.
 * Date: 2019/7/18.
 */

#ifndef _WTLOG_SERVER_H_
#define _WTLOG_SERVER_H_

#include "netprotocol.h"
#include "wtlogtools.h"

using std::string;

namespace wtlog {

struct StatInfo {
    std::vector<string> client_socket;
    std::vector<string> lander_socket;
};

class WTLogServer {
private:
    struct SendInfo {
        SendInfo() {}
        SendInfo(uint16_t h_in, const string& c_in) : head(h_in), content(c_in) {}
        SendInfo(const SendInfo& in) : head(in.head), content(in.content) {}
        SendInfo& operator=(const SendInfo& in) {
            head = in.head;
            content = in.content;
        }
        
        uint16_t head;
        string content;
    };
    
public:
    friend class wtatom::AtomMap<int, wtatom::AtomQueue<SendInfo>*>;

    /** 
     * Constructive function.
     */
    WTLogServer();
    
    /** 
     * Start the server.
     * @param listen_port: Listen new connection from this port.
     */
    bool start(short listen_port = 8089);
    
    /** 
     * Stop the server.
     * By call this function, you must guarantee all clients and landers have stopped.
     * @param soft: If true, won't stop if some clients didn't be closed correctly.
     * @return false(Only soft == true): Some client or lander have not correctly closed. Do not stop.
     */
    bool stop(bool soft = true);
    
    /** 
     * Show the status.
     */
    StatInfo status();

private:
    /**
     * For each Client, we have a listen thread.
     * For each Lander, we have a send thread.
     * We have one thread listening to all Landers.
     * We have one thread sending info to all Clients.
     */
    wtatom::AtomMap<int, string>    _socket_info;
    wtatom::AtomMap<int, pthread_t> _listen_t; // Each client have a listen thread.
    wtatom::AtomMap<int, pthread_t> _send_t; // Each lander have a send thread.
    wtatom::AtomMap<pthread_t, char> _lus_t; // Threads pids of _listen_unknown_socket.
    wtatom::AtomQueue<SendInfo>     _send_to_client;
    wtatom::AtomQueue<SendInfo>     _send_to_lander;
    wtatom::AtomMap<uint32_t, int>  _hash_socket; // The departure of logs which need reply.
    
    sockaddr_in   _svr_addr;   // Listen socket address(For new connection).
    pthread_t     _mon_t;      // Listen thread(For new connection).
    int           _mon_socket; // Listen socket.
    bool          _on_listen;  // If true, continuing listen new connection.
    pthread_t     _lfl_t;      // Listen from lander thread.
    pthread_t     _stc_t;      // Send to client thread.
    
    std::map<int, bool> _on_send; // Key: the socket, Val: Whether continuing sending logs to this lander.

private:
    /**
     * Listen thread for new connection.
     */
    static void* _wait_new_connection(void* args);
    
    /**
     * Listen common message from unknown remote socket.
     * If one socket constructed, open a thread to handle the unknown socket.
     */
    static void* _listen_unknown_socket(void* args);
    
    /**
     * Listen from client. Each client has one thread.
     */
    static void* _listen_client(void* args);
    
    /**
     * Listen from lander.
     */
    static void* _listen_lander(void* args);
    
    /**
     * Send to client.
     */
    static void* _send_client(void* args);
    
    /**
     * Send to lander. Each lander has one thread.
     */
    static void* _send_lander(void* args);
    
}; // End class WTLogServer.
    
} // End namespace wtlog.

#endif // End ifdef _WTLOG_SERVER_H_.
