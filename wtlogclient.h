/**
 * WenTanLogServer Client.
 * Include this file to print log in your server.
 * Author: LiWentan.
 * Date: 2019/7/15.
 */
 
#ifndef _WTLOG_CLIENT_H_
#define _WTLOG_CLIENT_H_

#include <iostream>
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <cstring>
#include <unordered_map>
#include "wtatomqueue.hpp"
#include "netprotocol.h"
#include "wtlogtools.h"

using std::string;

namespace wtlog {

class WTLogClientException : public std::exception {
public:
    WTLogClientException(const string& info) : _info(info) {}
    virtual const char* what() {
        return _info.c_str();
    }
    
private:
    string _info;
};

class WTLogClient {
private:
    struct PrintRequest {
        PrintRequest() {}
        PrintRequest(const PrintRequest& in) : 
            p_time(in.p_time), content(in.content), level(in.level), callback(in.callback) {}
        PrintRequest(const string& c_in, 
            uint32_t t_in, 
            LogLevel l_in, 
            void (*ca_in)(const CallBackInfo&)) : 
            p_time(t_in), content(c_in), 
            level(l_in), callback(ca_in) {}
        PrintRequest& operator=(const PrintRequest& in) {
            content = in.content;
            p_time = in.p_time;
            level = in.level;
            callback = in.callback;
        }
        
        uint32_t p_time; // Time of the log. Prevent time lap of different machine and network delay.
        string content; // Content can be binary data. If it is string, last '\0' won't be sent.
        LogLevel level;
        void (*callback)(const CallBackInfo&);
    };

public:
    friend class wtatom::AtomQueue<PrintRequest>;

    /**
     * Construction function.
     */
    WTLogClient();
    
    /**
     * Initialize. Connect the target router server.
     * @return true: You can start to print log.
     * @return false: Conncet to log server error, try again.
     */
    bool connect(const string& ip, short port);
    
    /**
     * Stop to use this client. 
     * It will wait for all requests in queue, and then disconnect with the log server.
     */
    void disconnect();
    
    /**
     * Send the log to log server.
     * @param content: The string to be printed in log.
     * @param level: The log level.
     * @param callback: If you want to check the return of 
     *      the log server, give a callback function.
     */
    void tolog(const string& content, 
        LogLevel level = LogLevel::info, 
        void (*callback)(const CallBackInfo&) = nullptr);

private:
    bool          _connected; // If true, this class is connected to log server.
    sockaddr_in   _svr_addr; // The address of the log server. Used for reconnecting by unexpected disconnection.
    int           _socket; // Socket with the log server.
    pthread_t     _hpq_t; // Thread number of _handle_print_queue.
    pthread_t     _mr_t; // Thread number of _monitor_return.
    
    wtatom::AtomQueue<PrintRequest> _print_queue; // Infos in this queue are to be sent to log server.
    wtatom::AtomMap<uint32_t, void (*)(const CallBackInfo&)> _callback_fun; // The callback functions waitting to be called.
    
private:
    /**
     * Send controll information to log server.
     */
    void _send_command(Command comm, const char* content = nullptr);

    /**
     * Handle the print queue looply.
     */
    static void* _handle_print_queue(void* args);
    
    /**
     * Monitor the return from the log server.
     */
    static void* _monitor_return(void* args);
    
}; // End class WTLogClient.    
    
    
} // End namespace wtlog.


#endif // End ifdef _WTLOG_CLIENT_H_.
