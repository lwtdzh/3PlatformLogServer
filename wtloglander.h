/**
 * Machine that running lander storaging the log content.
 * Lander will connect with the server.
 * Author: LiWentan.
 * Date: 2019/7/16.
 */
 
#ifndef _WTLOG_LANDER_H_
#define _WTLOG_LANDER_H_

#include <pthread.h>
#include "wtlogtools.h"
#include "netprotocol.h"
#include "wtatomqueue.hpp"

using std::string;

namespace wtlog {
    
class WTLogLander {
private:
    /**
     * Use this information to storage a log.
     */
    struct LogInfo {
        LogInfo() {}
        LogInfo(const string& c_in, uint32_t t_in, LogLevel l_in, uint32_t h_in) : 
            content(c_in), p_time(t_in), level(l_in), hash_id(h_in) {}
        LogInfo(const LogInfo& in) : 
            content(in.content), p_time(in.p_time), level(in.level), hash_id(in.hash_id) {}
        LogInfo& operator=(const LogInfo& in) {
            content = in.content;
            p_time = in.p_time;
            level = in.level;
            hash_id = in.hash_id;
            return *this;
        }
        bool operator==(const LogInfo& rhs) {
            if (content == rhs.content && 
                p_time == rhs.p_time &&
                level == rhs.level && hash_id == rhs.hash_id) {
                return true;
            }
            return false;
        }
        
        string content;
        uint32_t p_time;
        LogLevel level;
        uint32_t hash_id;
    };
    
    /**
     * Use this information to start a search task.
     */
    struct SearchInfo {
        SearchInfo() {}
        SearchInfo(const string& c_in, LogLevel l_in, uint32_t h_in, 
            uint32_t s_in, uint32_t e_in) : content(c_in), level(l_in),
            hash_id(h_in), start_time(s_in), end_time(e_in) {}
        SearchInfo(const SearchInfo& in) : content(in.content), 
            level(in.level), hash_id(in.hash_id), 
            start_time(in.start_time), end_time(in.end_time) {}
        SearchInfo& operator=(const SearchInfo& in) {
            content = in.content;
            level = in.level;
            hash_id = in.hash_id;
            start_time = in.start_time;
            end_time = in.end_time;
            return *this;
        }
        
        string content;
        LogLevel level;
        uint32_t hash_id;
        uint32_t start_time;
        uint32_t end_time;
    };
    
    /**
     * Use this information to send a package to server.
     */
    struct SendInfo {
        SendInfo() {}
        SendInfo(uint16_t h_in, const string& c_in = "") : head(h_in), content(c_in) {}
        SendInfo(const SendInfo& in) : head(in.head), content(in.content) {}
        SendInfo& operator=(const SendInfo& in) {
            head = in.head;
            content = in.content;
            return *this;
        }
        
        uint16_t head;
        string content;
    };
    
public:
    friend class wtatom::AtomQueue<LogInfo>;
    friend class wtatom::AtomQueue<SearchInfo>;
    friend class wtatom::AtomQueue<SendInfo>;

    /**
     * Constructive function.
     * @param path: The folder that storaging the log file.
     */
    WTLogLander(const string& path);
    
    /**
     * Connect to the server.
     * If success, it will automatically receive and save log.
     */
    bool connect(const string& ip, short port);
    
    /**
     * Disconnect. Stop to work.
     * Tell the server that do not send log to this server.
     */
    void disconnect();
    
private:
    string  _path;   // Log file folder path.
    FILE*   _write;  // File pointer used to write data.
    FILE*   _read;   // File pointer used to read data.
    string  _cur_log_date;    // Now, log will be written to file reflecting this date.
    bool          _send_queue_on_append; // False means send queue won't have more elements, just need handle existing elements.
    bool          _on_recv;   // True means this lander is monitoring the request from server.
    int           _socket;    // Socket to the log server.
    sockaddr_in   _svr_addr;  // Server addr.
    pthread_t     _hpq_t;     // Thread number of _handle_print_queue.
    pthread_t     _hsq_t;     // Thread number of _handle_search_queue.
    pthread_t     _sq_t;      // Thread number of _send_queue.
    pthread_t     _mon_t;     // Thread number of monitoring request from server.
    pthread_rwlock_t                _file_lock;    // Any outer modifications to log file need write lock.
    wtatom::AtomQueue<LogInfo>      _print_queue;  // Logs to be printed.
    wtatom::AtomQueue<SearchInfo>   _search_queue; // Search requests.
    wtatom::AtomQueue<SendInfo>     _send_queue; // Search requests.
    wtatom::AtomMap<uint32_t, char> _reply_map;    // Request to be replied. Key is hash_id.

private:
    /**
     * Monitor the request from log server.
     */
    static void* _monitor(void* args);

    /**
     * Send information to log server.
     * @param comm:
     *     stop_send_log: Tell server do not send any log to this lander. Wait until the server reply, then return (Blocking). 
     *     close_with_lander: Tell server this lander won't send any package to server, e.g., search result, log reply. Blocking.
     *     write_log_ret: Tell the server this is a reply of a log request. Nonblocking.
     *
     * @param content: Extend info. Must be allocate in heap by malloc. This function will call free(content).
     *     If comm == write_log_ret: The hash_id (uint32_t).
     *
     */
    void _send_command(Command comm, void* content = nullptr);

    /**
     * Handle the print queue.
     */
    static void* _handle_print_queue(void* args);
    
    /**
     * Handle the search queue.
     */
    static void* _handle_search_queue(void* args);
    
    /**
     * Handle the send queue.
     */
    static void* _handle_send_queue(void* args);
    
}; // End class WTLogLander.

    
} // End namespace wtlog.

#endif // End ifndef _WTLOG_LANDER_H_.