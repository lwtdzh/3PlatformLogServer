/**
 * WenTanLogServer Client.
 * Lib this file to print log in your server.
 * Author: LiWentan.
 * Date: 2019/7/16.
 */
 
#include "wtlogclient.h"

namespace wtlog {

WTLogClient::WTLogClient() : _connected(false) {}

bool WTLogClient::connect(const string& ip, short port) {
    // Initialize the _svr_addr.
    memset(&_svr_addr, 0, sizeof(sockaddr_in));
    _svr_addr.sin_family = PF_INET;
    _svr_addr.sin_port = htons(port);
    _svr_addr.sin_addr.s_addr = inet_addr(ip.c_str());
    
    // Construct the connection.
    _socket = socket(PF_INET, SOCK_STREAM, 0);
    if (_socket < 0) {
        toscreen << "Initialize the socket failed. Connect failed.\n";
        return false;
    }
    int ret = ::connect(_socket, (sockaddr*)&_svr_addr, sizeof(sockaddr));
    if (ret < 0) {
        toscreen << "Cannot connect to IP: " << ip << ", Port: " << port << ".\n";
        return false;
    }
    
    // Handshake with the server, check whether remote server is correct type.
    uint16_t authorize_info_buffer = htons(h_authorize_info);
    ret = write(_socket, &authorize_info_buffer, sizeof(uint16_t));
    if (ret != sizeof(uint16_t)) {
        toscreen << "Write authorize_info to server error. Try to connect again.\n";
        _send_command(Command::disconnect);
        close(_socket);
        return false;
    }
    uint16_t authorize_ret_buffer;
    ret = read(_socket, &authorize_ret_buffer, sizeof(uint16_t));
    authorize_ret_buffer = ntohs(authorize_ret_buffer);
    if (ret != sizeof(uint16_t) || authorize_ret_buffer != h_authorize_ret) {
        toscreen << "Remote server may not a correct wtlogserver. Try again.\n";
        _send_command(Command::disconnect);
        close(_socket);
        return false;
    }
    _connected = true;
    
    // Create thread to handle the _print_queue.
    ret = pthread_create(&_hpq_t, nullptr, _handle_print_queue, this);
    if (ret != 0) {
        toscreen << "Create thread for handling _print_queue failed. Code: " << ret << ".\n";
        _send_command(Command::disconnect);
        _connected = false;
        close(_socket);
        return false;
    }
    
    // Create thread to monitor return.
    ret = pthread_create(&_mr_t, nullptr, _monitor_return, this);
    if (ret != 0) {
        toscreen << "Create thread for monitoring return failed. Code: " << ret << ".\n";
        _send_command(Command::disconnect);
        _connected = false;
        close(_socket);
        return false;
    }
    
    // Conncet successfully.
    toscreen << "Connected to IP: " << ip << ", Port: " << port << ".\n";
    return true;
}

void WTLogClient::disconnect() {
    if (_connected == false) {
        return;
    }
    
    // Wait until all works have been done.
    int loop_time = 0;
    while (_print_queue.size() != 0) {
        if (loop_time > 30) {
            toscreen << "Still waitting for print_queue.\n";
            loop_time = 0;
        }
        ++loop_time;
        usleep(4e5);
    }
    loop_time = 0;
    while (_callback_fun.size() != 0) {
        toscreen << "Still waitting for callback_fun.\n";
        ++loop_time;
        if (loop_time > 15) {
            toscreen << "Some request is still waitting for callback, mandatory close.\n";
            break;
        }
        usleep(4e5);
    }
    
    // Tell the log server that this client is going to close.
    _send_command(Command::disconnect);
    
    // Close local connection.
    close(_socket);
    _connected = false;
}

void WTLogClient::tolog(const string& content, 
    LogLevel level, 
    void (*callback)(const CallBackInfo&)) {
    if (_connected == false) {
        // Discard the log.
        return;
    }
    uint32_t utc_time = time(nullptr);
    PrintRequest req = PrintRequest(content, utc_time, level, callback);
    _print_queue.push(req);
}

void WTLogClient::_send_command(Command comm, const char* content) {
    if (comm == Command::disconnect) {
        uint16_t close_head_buffer = htons(h_close_head);
        int ret = write(_socket, &close_head_buffer, sizeof(uint16_t));
        if (ret != sizeof(uint16_t)) {
            toscreen << "Fatal error. Write function call failed.\n";
            _print_queue.clear();
            _callback_fun.clear();
            _connected = false;
        }
    } else {
        toscreen << "Unsupported command: " << (int)comm << ".\n";
    }
}

void* WTLogClient::_handle_print_queue(void* args) {
    WTLogClient* client = (WTLogClient*)args;
    int empty_times = 0;
    PrintRequest pr;
    char buffer[10240];
    while (client->_connected == true || client->_print_queue.size() != 0) {
        if (empty_times >= 20) {
            usleep(2e5);
        }
        
        if (client->_print_queue.get(pr) == false) {
            // No log is waitting to be sent.
            ++empty_times;
            continue;
        }
        
        if (debug_mode) {
            toscreen << "Found a log, start to handle.\n";
        }
        
        if (pr.content.size() > 10000) {
            // Unsupported length.
            toscreen << "A log is too long. Ignore this log.\n";
            continue;
        }
        
        // Send the pr.
        size_t next_addr = 0; // Offset address of empty space in buffer.
        
        // Set the head as "send_log".
        uint16_t h_send_l = (pr.callback == nullptr) ? h_send_log : h_send_log_need_reply;
        h_send_l = htons(h_send_l);
        memcpy(buffer + next_addr, &h_send_l, sizeof(uint16_t));
        next_addr += sizeof(uint16_t);
        
        // Set log time.
        uint32_t p_time = htonl(pr.p_time);
        memcpy(buffer + next_addr, &p_time, sizeof(uint32_t));
        next_addr += sizeof(uint32_t);
        
        // Set log level.
        uint16_t level = htons(static_cast<uint16_t>(pr.level));
        memcpy(buffer + next_addr, &level, sizeof(uint16_t));
        next_addr += sizeof(uint16_t);
        
        // Set hash id.
        uint32_t hash_id = wttool::str2hash(pr.content, true);
        uint32_t hash_id_sent = htonl(hash_id);
        memcpy(buffer + next_addr, &hash_id_sent, sizeof(uint32_t));
        next_addr += sizeof(uint32_t);
        if (pr.callback != nullptr) {
            client->_callback_fun[hash_id] = pr.callback;
        }
        
        if (debug_mode) {
            toscreen << "The hash_id: " << hash_id << ".\n";
        }
        
        // Set the string length.
        uint16_t str_len = static_cast<uint16_t>(pr.content.size());
        str_len = htons(str_len);
        memcpy(buffer + next_addr, &str_len, sizeof(uint16_t));
        next_addr += sizeof(uint16_t);
        
        if (debug_mode) {
            toscreen << "The log length: " << ntohs(str_len) << ".\n";
        }
        
        // Set the string content.
        memcpy(buffer + next_addr, pr.content.c_str(), pr.content.size());
        next_addr += pr.content.size();
        
        if(debug_mode) {
            toscreen << "Start to write log to TCP buffer.\n";
        }
        
        // Send message to log server.
        int ret = write(client->_socket, buffer, next_addr);
        if (ret != next_addr) {
            toscreen << "Fatal error. Write function call failed.\n";
            client->_print_queue.clear();
            client->_callback_fun.clear();
            client->disconnect();
            pthread_exit(nullptr);
        }
        
        if(debug_mode) {
            toscreen << "Write log to TCP buffer completely. Totally sent: " << next_addr << " bytes.\n";
        }
    }
    pthread_exit(nullptr);
}

void* WTLogClient::_monitor_return(void* args) {
    WTLogClient* client = (WTLogClient*)args;
    char buffer[10240];
    while (client->_connected == true || client->_callback_fun.size() != 0) {
        // Read the head.
        uint16_t head;
        wttool::safe_read(client->_socket, &head, sizeof(uint16_t));
        head = ntohs(head);
        switch(head) {
            case h_close_ret : {
                toscreen << "Successfully receive the close reply from log server.\n";
                break;
            }
            case h_log_receive_success : {
                if (debug_mode) {
                    toscreen << "Found a log reply.\n";
                }
                
                // Read hash_id, find the callback function.
                uint32_t hash_id;
                wttool::safe_read(client->_socket, &hash_id, sizeof(uint32_t));
                hash_id = ntohl(hash_id);
                void (*back_fun)(const CallBackInfo&) = nullptr;
                if (client->_callback_fun.find_and_remove(hash_id, &back_fun) == false) {
                    // No such hash_id waitting for callback.
                }
                
                if (debug_mode) {
                    toscreen << "The hash_id of this reply is " << hash_id << ".\n";
                }
                
                // Construct the CallBackInfo.
                CallBackInfo cbinfo;
                cbinfo.status = CallBackStat::success;
                
                // Read the message and save the message to CallBackInfo.
                uint16_t message_length;
                wttool::safe_read(client->_socket, &message_length, sizeof(uint16_t));
                message_length = ntohs(message_length);
                if (message_length != 0) {
                    wttool::safe_read(client->_socket, buffer, message_length);
                    cbinfo.message = string(buffer, message_length);
                }
                
                if (debug_mode) {
                    toscreen << "The reply message length: " << message_length << ".\n";
                }
                
                // Call the callback function.
                if (back_fun != nullptr) {
                    back_fun(cbinfo);
                }

                break;
            }
            default : {
                toscreen << "Undefined reply head from server: " << head << ".\n";
                break;
            }
        }
    }
    pthread_exit(nullptr);
}

} // End namespace wtlog.
