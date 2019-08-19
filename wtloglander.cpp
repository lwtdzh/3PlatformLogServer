/**
 * Machine that running lander storaging the log content.
 * Lander will connect with the server.
 * Author: LiWentan.
 * Date: 2019/7/17.
 */
 
#include "wtloglander.h"

namespace wtlog {
    
WTLogLander::WTLogLander(const string& path) : _path(path) {
    _write = _read = nullptr;
    _on_recv = false;
    _send_queue_on_append = false;
}

bool WTLogLander::connect(const string& ip, short port) {
    // Open the file.
    _cur_log_date = wttool::cur_date();
    string log_file = _path + _cur_log_date;
    _write = fopen(log_file.c_str(), "ab");
    _read = fopen(log_file.c_str(), "rb");
    if (_write == nullptr || _read == nullptr) {
        toscreen << "Cannot open the log file: " << log_file << ".\n";
        if (_write != nullptr) {
            fclose(_write);
        }
        if (_read != nullptr) {
            fclose(_read);
        }
        return false;
    }
    
    // Initialize thread lock.
    pthread_rwlock_init(&_file_lock, nullptr);
    
    // Construct Addr.
    memset(&_svr_addr, 0, sizeof(sockaddr_in));
    _svr_addr.sin_family = PF_INET;
    _svr_addr.sin_port = htons(port);
    _svr_addr.sin_addr.s_addr = inet_addr(ip.c_str());
    
    // Construct the connection.
    _socket = socket(PF_INET, SOCK_STREAM, 0);
    if (_socket < 0) {
        toscreen << "Initialize the socket failed. Connect failed.\n";
        fclose(_write);
        fclose(_read);
        pthread_rwlock_destroy(&_file_lock);
        return false;
    }
    int ret = ::connect(_socket, (sockaddr*)&_svr_addr, sizeof(sockaddr));
    if (ret < 0) {
        toscreen << "Cannot connect to IP: " << ip << ", Port: " << port << ".\n";
        close(_socket);
        fclose(_write);
        fclose(_read);
        pthread_rwlock_destroy(&_file_lock);
        return false;
    }
    
    // Handshake with the server, check whether remote server is correct type.
    uint16_t handshake_info_buffer = htons(h_handshake_info);
    ret = write(_socket, &handshake_info_buffer, sizeof(uint16_t));
    if (ret != sizeof(uint16_t)) {
        toscreen << "Write handshake_info to server error. Try to connect again.\n";
        close(_socket);
        fclose(_write);
        fclose(_read);
        pthread_rwlock_destroy(&_file_lock);
        return false;
    }
    uint16_t handshake_ret_buffer;
    ret = read(_socket, &handshake_ret_buffer, sizeof(uint16_t));
    handshake_ret_buffer = ntohs(handshake_ret_buffer);
    if (ret != sizeof(uint16_t) || handshake_ret_buffer != h_handshake_ret) {
        toscreen << "Remote server may not a correct wtlogserver. Try again.\n";
        close(_socket);
        fclose(_write);
        fclose(_read);
        pthread_rwlock_destroy(&_file_lock);
        return false;
    }
    
    // Set flag. Tell other thread that they can receive and send message with log server.
    _send_queue_on_append = true;
    _on_recv = true;
    
    // Create thread to handle the _print_queue.
    ret = pthread_create(&_hpq_t, nullptr, _handle_print_queue, this);
    if (ret != 0) {
        toscreen << "Create thread for handling _print_queue failed. Code: " << ret << ".\n";
        _on_recv = false;
        _send_queue_on_append = false;
        _send_command(Command::stop_immediately);
        close(_socket);
        fclose(_write);
        fclose(_read);
        pthread_rwlock_destroy(&_file_lock);
        return false;
    }
    
    // Create thread to handle the _search_queue.
    ret = pthread_create(&_hsq_t, nullptr, _handle_search_queue, this);
    if (ret != 0) {
        toscreen << "Create thread for handling _search_queue failed. Code: " << ret << ".\n";
        _on_recv = false;
        _send_queue_on_append = false;
        pthread_cancel(_hpq_t);
        _send_command(Command::stop_immediately);
        close(_socket);
        fclose(_write);
        fclose(_read);
        pthread_rwlock_destroy(&_file_lock);
        return false;
    }
    
    // Create thread to handle the _send_queue.
    ret = pthread_create(&_sq_t, nullptr, _handle_send_queue, this);
    if (ret != 0) {
        toscreen << "Create thread for handling _send_queue failed. Code: " << ret << ".\n";
        _on_recv = false;
        _send_queue_on_append = false;
        pthread_cancel(_hpq_t);
        pthread_cancel(_hsq_t);
        _send_command(Command::stop_immediately);
        close(_socket);
        fclose(_write);
        fclose(_read);
        pthread_rwlock_destroy(&_file_lock);
        return false;
    }
    
    // Create thread to monitor request from server.
    ret = pthread_create(&_mon_t, nullptr, _monitor, this);
    if (ret != 0) {
        toscreen << "Create thread for handling _monitor failed. Code: " << ret << ".\n";
        _on_recv = false;
        _send_queue_on_append = false;
        pthread_cancel(_hpq_t);
        pthread_cancel(_hsq_t);
        pthread_cancel(_sq_t);
        _send_command(Command::stop_immediately);
        close(_socket);
        fclose(_write);
        fclose(_read);
        pthread_rwlock_destroy(&_file_lock);
        return false;
    }
    
    // Connect successfully.
    toscreen << "Connected to IP: " << ip << ", Port: " << port << ".\n";
    return true;
}
       
void WTLogLander::disconnect() {
    if (_send_queue_on_append == false) {
        return;
    }
    
    // Tell the server do not send request to this lander. 
    _send_command(Command::stop_send_log);
    toscreen << "Told the server do not send any log to this lander.\n";
    
    void* tmp_pointer;
    
    // Wait until all elements in print queue have been done.
    toscreen << "Waitting the handle_print_queue thread...\n";
    pthread_join(_hpq_t, &tmp_pointer);
    
    // Wait until all elements in search queue have been done.
    toscreen << "Waitting the handle_search_queue thread...\n";
    pthread_join(_hsq_t, &tmp_pointer);
    
    // Set flag to tell _handle_send_queue, it's time to exit.
    _send_queue_on_append = false;
    
    // Wait until all elements in send queue have been done.
    toscreen << "Waitting the handle_send_queue thread...\n";
    pthread_join(_sq_t, &tmp_pointer);
    
    // Tell the server this lander won't send anything to the server.
    _send_command(Command::close_with_lander);
   
    // Close local connection.
    close(_socket);

    // Destroy the thread lock.
    pthread_rwlock_destroy(&_file_lock);
    return;
}

void* WTLogLander::_monitor(void* args) {
    WTLogLander* lander = (WTLogLander*)args;
    char buffer[10240];
    while (lander->_on_recv == true) {
        // Receive head.
        uint16_t head_recv;
        bool reply = false;
        wttool::safe_read(lander->_socket, &head_recv, sizeof(uint16_t));
        head_recv = ntohs(head_recv);
        
        // Unify log print request.
        if (head_recv == h_send_log_need_reply) {
            head_recv = h_send_log;
            reply = true;
        }
      
        // Handle according to head_type.
        switch(head_recv) {
            case (h_send_log) : {
                // Read log package, construct LogInfo.
                LogLevel level;
                uint32_t p_time;
                uint32_t hash_id;
                uint16_t content_size;
                wttool::safe_read(lander->_socket, buffer, sizeof(uint32_t));
                p_time = (uint32_t)ntohs(*(uint32_t*)buffer);
                wttool::safe_read(lander->_socket, buffer, sizeof(uint16_t));
                level = (LogLevel)ntohs(*(uint16_t*)buffer);
                wttool::safe_read(lander->_socket, &hash_id, sizeof(uint32_t));
                hash_id = ntohl(hash_id);
                wttool::safe_read(lander->_socket, &content_size, sizeof(uint16_t));
                content_size = ntohs(content_size);
                wttool::safe_read(lander->_socket, buffer, content_size);
                LogInfo info = LogInfo(string(buffer, content_size), p_time, level, hash_id);
                
                // Push LogInfo to queue.
                if (lander->_on_recv != false) {
                    lander->_print_queue.push(info);
                    // If this log need reply, push it to reply map.
                    if (reply == true) {
                        lander->_reply_map[hash_id] = 0;
                    }
                }

                break;
            }
            case (h_search_request) : {
                // Read search package, construct SearchInfo.
                LogLevel level;
                uint32_t hash_id;
                uint32_t start_time;
                uint32_t end_time;
                uint16_t content_size;
                wttool::safe_read(lander->_socket, buffer, sizeof(uint16_t));
                level = (LogLevel)ntohs(*(uint16_t*)buffer);
                wttool::safe_read(lander->_socket, &hash_id, sizeof(uint32_t));
                hash_id = ntohl(hash_id);
                wttool::safe_read(lander->_socket, &start_time, sizeof(uint32_t));
                start_time = ntohl(start_time);
                wttool::safe_read(lander->_socket, &end_time, sizeof(uint32_t));
                end_time = ntohl(end_time);
                wttool::safe_read(lander->_socket, &content_size, sizeof(uint16_t));
                content_size = ntohs(content_size);
                wttool::safe_read(lander->_socket, buffer, content_size);
                SearchInfo info(string(buffer, content_size), level, hash_id, start_time, end_time);
                
                // Push SearchInfo to queue.
                if (lander->_on_recv != false) {
                    lander->_search_queue.push(info);
                }

                break;
            }
            case (h_stop_send_log_reply) : {
                if (debug_mode) {
                    toscreen << "Received the h_stop_send_log_reply.\n";
                }
                
                lander->_on_recv = false;
                break;
            }
            case (h_close_with_lander_reply) : {
                toscreen << "[ERROR]_monitor received h_close_with_lander_reply.\n";
                lander->_send_queue_on_append = false;
                lander->_on_recv = false;
                break;
            }
            default : {
                toscreen << "[ERROR]Unsupported head type: " << head_recv << ".\n";
            }
        }
    }
    pthread_exit(nullptr);
}

void* WTLogLander::_handle_print_queue(void* args) {
    WTLogLander* lander = (WTLogLander*)args;
    int empty_times = 0;
    LogInfo loginfo;
    char buffer[10240];
    while (lander->_on_recv == true || lander->_print_queue.size() != 0) {
        if (empty_times >= 20) {
            usleep(2e5);
        }
        
        if (lander->_print_queue.get(loginfo) == false) {
            // No log is waitting to be sent.
            ++empty_times;
            continue;
        }
        
        size_t next_addr = 0;
        
        // Write log head tag.
        memcpy(buffer + next_addr, &log_disk_head_tag, sizeof(char));
        next_addr += sizeof(char);
        
        // Write time.
        uint32_t cur_time = loginfo.p_time;
        memcpy(buffer + next_addr, &cur_time, sizeof(uint32_t));
        next_addr += sizeof(uint32_t);
        
        // Write level.
        uint16_t level = (uint16_t)loginfo.level;
        memcpy(buffer + next_addr, &level, sizeof(uint16_t));
        next_addr += sizeof(uint16_t);
        
        // Write content size.
        uint16_t content_size = (uint16_t)loginfo.content.size();
        memcpy(buffer + next_addr, &content_size, sizeof(uint16_t));
        next_addr += sizeof(uint16_t);
        
        // Write content.
        memcpy(buffer + next_addr, loginfo.content.c_str(), content_size);
        next_addr += content_size;
        
        // Add log tail tag.
        memcpy(buffer + next_addr, &log_disk_tail_tag, sizeof(char));
        next_addr += sizeof(char);
        
        // Write to disk.
        wtatom::lockr(lander->_file_lock);
        int ret = fwrite(buffer, next_addr, 1, lander->_write);
        if (ret != 1) {
            // Write failed. Manully write a log_disk_tail_tag to avoid pollution.
            toscreen << "[ERROR]Write log to disk failed. Log size: " << next_addr << ".\n";
            ret = fwrite(&log_disk_tail_tag, 1, 1, lander->_write);
            int try_times = 0;
            while (ret != 1 && try_times < 5) {
                // Wait until write success.
                usleep(2e4);
                ret = fwrite(&log_disk_tail_tag, 1, 1, lander->_write);
                ++try_times;
            }
        }
        fflush(lander->_write);
        wtatom::unlock(lander->_file_lock);
        
        // Send success info to server (if doing not need reply, it won't send network package).
        void* ret_hash_id = malloc(sizeof(uint32_t));
        memcpy(ret_hash_id, &loginfo.hash_id, sizeof(uint32_t));
        lander->_send_command(Command::write_log_ret, ret_hash_id);
    }
    
    pthread_exit(nullptr);
}

void* WTLogLander::_handle_search_queue(void* args) {
    WTLogLander* lander = (WTLogLander*)args;
    int empty_times = 0;
    SearchInfo sinfo;
    while (lander->_on_recv == true || lander->_search_queue.size() != 0) {
        if (empty_times >= 20) {
            usleep(2e5);
        }
        
        if (lander->_search_queue.get(sinfo) == false) {
            // No log is waitting to be sent.
            ++empty_times;
            continue;
        }
        
        // TODO.
        
    }
    pthread_exit(nullptr);
}

void WTLogLander::_send_command(Command comm, void* content) {
    if (comm == Command::stop_send_log) {
        SendInfo info(h_stop_send_log);
        
        if (debug_mode) {
            toscreen << "Start to send h_stop_send_log to server.\n";
        }
        
        _send_queue.push(info);
        
        if (debug_mode) {
            toscreen << "Waitting for _on_recv to close.\n";
        }
        
        while (_on_recv != false) {
            // Wait until the reveive switch is closed.
            usleep(4e5);
        }
        // Wait until the _monitor thread is finished.
        void* tmp_pointer;
        toscreen << "Waitting that the _monitor thread quit.\n";
        pthread_join(_mon_t, &tmp_pointer);
        toscreen << "Monitor thread is closed.\n";
    } else if (comm == Command::close_with_lander) {
        // Tell the send thread finish its work.
        _send_queue_on_append = false;
        
        // Guarantee the send queue has stopped.
        void* tmp_pointer;
        pthread_join(_sq_t, &tmp_pointer);
        
        // Tell server this lander won't send any message. Wait the reply.
        uint16_t no_send_head = htons(h_close_with_lander);
        write(_socket, &no_send_head, sizeof(uint16_t));
        uint16_t no_send_reply;
        toscreen << "Send h_close_with_lander, waitting for reply...\n";
        wttool::safe_read(_socket, &no_send_reply, sizeof(uint16_t));
        no_send_reply = ntohs(no_send_reply);
        if (no_send_reply != h_close_with_lander_reply) {
            toscreen << "Send h_close_with_lander but got wrong reply.\n";
        } else {
            toscreen << "Send h_close_with_lander and got right reply.\n";
        }
    } else if (comm == Command::write_log_ret) {
        if (content == nullptr) {
            toscreen << "[ERROR]Try to write log ret but no content.\n";
            return;
        }
        uint32_t& hash_id = *(uint32_t*)content;
        if (_reply_map.find_and_remove(hash_id) == true) {
            // This hash_id reflects a log which need reply.
            SendInfo info(h_log_receive_success, string((char*)&hash_id));
            _send_queue.push(info);
        }
    } else if (comm == Command::stop_immediately) {
        uint16_t no_send_head = htons(h_close_with_lander);
        write(_socket, &no_send_head, sizeof(uint16_t));
    }
    if (content != nullptr) {
        free(content);
    }
}

void* WTLogLander::_handle_send_queue(void* args) {
    WTLogLander* lander = (WTLogLander*)args;
    int empty_times = 0;
    SendInfo sinfo;
    char buffer[10240];
    while (lander->_send_queue_on_append == true || lander->_send_queue.size() != 0) {
        if (empty_times >= 20) {
            usleep(2e5);
        }
        
        if (lander->_send_queue.get(sinfo) == false) {
            // No message is waitting to be sent.
            ++empty_times;
            continue;
        }

        if (sinfo.head == h_log_receive_success) {
            size_t next_addr = 0;
            
            // Write head.
            uint16_t head_sent = htons(h_log_receive_success);
            memcpy(buffer + next_addr, &head_sent, sizeof(uint16_t));
            next_addr += sizeof(uint16_t);
            
            // Write hash_id.
            uint32_t hash_id;
            memcpy(&hash_id, sinfo.content.c_str(), sizeof(uint32_t));
            hash_id = htonl(hash_id);
            memcpy(buffer + next_addr, &hash_id, sizeof(uint32_t));
            next_addr += sizeof(uint32_t);
            
            // Write content size.
            uint16_t con_size = htons((uint16_t)(sinfo.content.size() - sizeof(uint32_t)));
            memcpy(buffer + next_addr, &con_size, sizeof(uint16_t));
            next_addr += sizeof(uint16_t);
            
            // Write content.
            memcpy(buffer + next_addr, sinfo.content.c_str() + sizeof(uint32_t), sinfo.content.size() - sizeof(uint32_t));
            next_addr += (sinfo.content.size() - sizeof(uint32_t));
            
            // Send.
            write(lander->_socket, buffer, next_addr);
        } else if (sinfo.head == h_search_fin) {
            
        } else if (sinfo.head == h_stop_send_log) {
            // Send this message to server.
            if (debug_mode) {
                toscreen << "Start to send h_stop_send_log to server.\n";
            }
            uint16_t s_head = htons(h_stop_send_log);
            write(lander->_socket, &s_head, sizeof(uint16_t));
            if (debug_mode) {
                toscreen << "Writing send h_stop_send_log to server finished.\n";
            }
        } else {
            toscreen << "Found unsupported head in send queue: " << sinfo.head << ".\n";
        }
    }
    pthread_exit(nullptr);
}

} // End namespace wtlog.