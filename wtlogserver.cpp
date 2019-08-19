/**
 * Server connects the client and lander.
 * Server is the control hub.
 * Author: LiWentan.
 * Date: 2019/7/18.
 */
 
#include "wtlogserver.h"

namespace wtlog {
    
WTLogServer::WTLogServer() {}

bool WTLogServer::start(short listen_port) {
    // Create _mon_socket.
    _mon_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (_mon_socket == 0) {
        toscreen << "Create _mon_socket failed.\n";
        return false;
    }
    
    // Set _mon_socket as non-block.
    // int flg = fcntl(_mon_socket, F_GETFL, 0);
    // fcntl(_mon_socket, F_SETFL, flg | O_NONBLOCK);
    
    // Set _svr_addr.
    memset(&_svr_addr, 0, sizeof(_svr_addr));
    _svr_addr.sin_family = AF_INET;
    _svr_addr.sin_port = htons(listen_port);
    _svr_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    
    // Bind _svr_addr to _mon_socket.
    int ret = bind(_mon_socket, (sockaddr*)&_svr_addr, sizeof(sockaddr));
    if (ret < 0) {
        toscreen << "Bind address to socket failed.\n";
        close(_mon_socket);
        return false;
    }
    
    // Set _mon_socket as listen mode.
    ret = listen(_mon_socket, 2000);
    if (ret < 0) {
        toscreen << "Cannot set _mon_socket as listen mode.\n";
        close(_mon_socket);
        return false;
    }
    
    // Set listen flag, tell the _wait_new_connection thread to accept new connection.
    _on_listen = true;
    
    // Create thread for listening from lander.
    ret = pthread_create(&_lfl_t, nullptr, _listen_lander, this);
    if (ret != 0) {
        toscreen << "Create thread for listening from lander failed.\n";
        close(_mon_socket);
        _on_listen = false;
        return false; 
    }
    
    // Create thread for sending to client.
    ret = pthread_create(&_stc_t, nullptr, _send_client, this);
    if (ret != 0) {
        toscreen << "Create thread for sending to client failed.\n";
        close(_mon_socket);
        _on_listen = false;
        pthread_cancel(_lfl_t);
        return false;
    }
    
    // Create thread to monitor new connection.
    ret = pthread_create(&_mon_t, nullptr, _wait_new_connection, this);
    if (ret != 0) {
        toscreen << "Create thread for monitoring new connection failed.\n";
        close(_mon_socket);
        _on_listen = false;
        pthread_cancel(_lfl_t);
        pthread_cancel(_stc_t);
        return false;
    }
    
    toscreen << "Start completely. Listen at PORT: " << listen_port << ".\n";
    return true;
}

bool WTLogServer::stop(bool soft) {
    // Set flag, stop accepting new connection.
    _on_listen = false;
    
    // Wait monitor thread.
    void* tmp_pointer;
    toscreen << "Waiting for monitor thread to stop...\n";
    pthread_join(_mon_t, &tmp_pointer);
    close(_mon_socket);
    
    // Wait _lus_t.
    toscreen << "Waitting for unknown listener to stop...\n";
    while (_lus_t.size() != 0) {
        usleep(5e5);
    }

    if (_socket_info.size() != 0) {
        // Print uncorrect opposites info.
        std::vector<string> _on_socket_info;
        _socket_info.get_all(nullptr, &_on_socket_info);
        stringstream ss;
        ss << "Following clients or landers have not been correctly closed: \n";
        for (size_t i = 0; i < _on_socket_info.size(); ++i) {
            ss << i << ": " << _on_socket_info[i] << ".\n";
        }
        toscreen << ss.str();
        
        if (soft == true) {
            return false;
        }
        
        // Clean the resources.
        std::vector<int> _on_socket;
        std::vector<pthread_t> listen_thread;
        std::vector<pthread_t> send_thread;
        std::vector<wtatom::AtomQueue<SendInfo>*> _queue;
        _listen_t.get_all(&_on_socket, &listen_thread);
        _send_t.get_all(nullptr, &send_thread);
        for (size_t i = 0; i < _on_socket.size(); ++i) {
            pthread_cancel(listen_thread[i]);
            pthread_cancel(send_thread[i]);
            close(_on_socket[i]);
        }
        _socket_info.clear();
        _listen_t.clear();
        _send_t.clear();
        _send_to_client.clear();
        _send_to_lander.clear();
    }
    
    toscreen << "Server stopped.\n";
    return true;
}

StatInfo WTLogServer::status() {
    std::vector<string> o_info;
    _socket_info.get_all(nullptr, &o_info);
    StatInfo res;
    for (size_t i = 0; i < o_info.size(); ++i) {
        if (o_info[i].find("[Client]") != std::string::npos) {
            // Is a client.
            res.client_socket.push_back(o_info[i]);
        } else {
            res.lander_socket.push_back(o_info[i]);
        }
    }
    return res;
}

void* WTLogServer::_wait_new_connection(void* args) {
    WTLogServer* server = (WTLogServer*)args;
    while (server->_on_listen) {
        // Waitting for new connection.
        int new_socket = accept(server->_mon_socket, nullptr, nullptr);
        if (new_socket < 0) {
            usleep(4e5);
            continue;
        }
        
        // Set socket as Block mode.
        int flg = fcntl(new_socket, F_GETFL, 0);
        fcntl(new_socket, F_SETFL, flg&~O_NONBLOCK);
        
        // Create thread to handle this new socket.
        pthread_t new_thread;
        void* param_t = malloc(sizeof(int) + sizeof(void*));
        memcpy(param_t, &new_socket, sizeof(int));
        memcpy(param_t + sizeof(int), &server, sizeof(void*));
        int ret = pthread_create(&new_thread, nullptr, server->_listen_unknown_socket, param_t);
        if (ret != 0) {
            toscreen << "Create thread for new connection failed.\n";
            continue;
        }
        server->_lus_t[new_thread] = 0;
    }
    pthread_exit(nullptr);
}

void* WTLogServer::_listen_unknown_socket(void* args) {
    int tar_socket = *(int*)args;
    WTLogServer* server = *(WTLogServer**)(args + sizeof(int));
    char buffer[sizeof(uint16_t)];
    pthread_t self_t = pthread_self();
    
    // Get the socket information.
    sockaddr_in sk_info;
    socklen_t sk_info_len = sizeof(sockaddr_in);
    getpeername(tar_socket, (sockaddr*)&sk_info, &sk_info_len);
    string sk_info_str = "[IP: " + wttool::cstr2str(inet_ntoa(sk_info.sin_addr)) + "]"
        "[PORT: " + wttool::num2str(ntohs(sk_info.sin_port)) + "]";
    
    // Read the handshake information from tar_socket.
    wttool::safe_read(tar_socket, buffer, sizeof(uint16_t));
    uint16_t hand_info = ntohs(*(uint16_t*)buffer);
    
    // Check the handshake information.
    if (hand_info == h_authorize_info) { // Is a client.
        // Create thread for listen.
        pthread_t l_t;
        void* param_t = malloc(sizeof(int) + sizeof(void*));
        memcpy(param_t, args, sizeof(int) + sizeof(void*));
        int ret = pthread_create(&l_t, nullptr, _listen_client, param_t);
        if (ret != 0) {
            toscreen << "Creat listen thread for new client failed.\n";
            write(tar_socket, "00", 2); // Send failed info to client.
            close(tar_socket);
            server->_lus_t.find_and_remove(self_t); // Delete this lus thread info from _lus_t.
            pthread_exit(nullptr);
        }
        
        // Add info to socket_info.
        server->_socket_info[tar_socket] = "[Client]" + sk_info_str;
        
        // Add listen thread to thread pool.
        server->_listen_t[tar_socket] = l_t;
        
        // Send OK information to client.
        uint16_t har = htons(h_authorize_ret);
        write(tar_socket, &har, sizeof(uint16_t));
        
        toscreen << "Connected to " << "[Client]" << sk_info_str << ".\n";
        
    } else if (hand_info == h_handshake_info) { // Is a lander.
        // Send OK information to lander.
        uint16_t hhr = htons(h_handshake_ret);
        write(tar_socket, &hhr, sizeof(uint16_t));
        
        // Set lander socket as NONBLOCK.
        int flg = fcntl(tar_socket, F_GETFL, 0);
        fcntl(tar_socket, F_SETFL, flg | O_NONBLOCK);
        
        // Set _on_send tag.
        server->_on_send[tar_socket] = true;
        
        // Create thread for sending.
        pthread_t s_t;
        void* param_t = malloc(sizeof(int) + sizeof(void*));
        memcpy(param_t, args, sizeof(int) + sizeof(void*));
        int ret = pthread_create(&s_t, nullptr, _send_lander, param_t);
        if (ret != 0) {
            toscreen << "Creat send thread for new lander failed.\n";
            close(tar_socket); // Since the lander has been ready, directly close socket.
            server->_lus_t.find_and_remove(self_t); // Delete this lus thread info from _lus_t.
            pthread_exit(nullptr);
        }
        
        // Add info to socket_info.
        server->_socket_info[tar_socket] = "[Lander]" + sk_info_str;
        
        // Add send thread to thread pool.
        server->_send_t[tar_socket] = s_t;
        
        toscreen << "Connected to " << "[Lander]" << sk_info_str << ".\n";
    } else {
        toscreen << "Unknown remote type.\n";
        close(tar_socket);
        free(args);
        pthread_exit(nullptr);
    }
    free(args);
    pthread_exit(nullptr);
}

void* WTLogServer::_listen_client(void* args) {
    pthread_t self_t = pthread_self();
    int l_socket = *(int*)args;
    WTLogServer* server = *(WTLogServer**)(args + sizeof(int));
    free(args);
    char buffer[10240];
    while (server->_on_listen == true) {
        // Read head.
        uint16_t recv_head;
        wttool::safe_read(l_socket, &recv_head, sizeof(uint16_t));
        recv_head = ntohs(recv_head);
        
        if (debug_mode) {
            toscreen << "Found a message from client.\n";
        }
        
        if (recv_head == h_close_head) { // Client won't send any log to this server.
            // Since we do not know whether there is a log need reply,
            // and we do not know when will the log need reply come back,
            // we directly clean the resource, ignore the potential reply.
            
            if (debug_mode) {
                toscreen << "This message from client is a close message.\n";
            }
            
            server->_socket_info.find_and_remove(l_socket);
            server->_listen_t.find_and_remove(l_socket);
            
            // There may be something in progress(_send_client is handling), give them 3 sec.
            sleep(3);
            
            // Send confirmation message to client.
            uint16_t reply_to_client = htons(h_close_ret);
            write(l_socket, &reply_to_client, sizeof(uint16_t));
            
            if (debug_mode) {
                toscreen << "Have sent close comfirmation message to client.\n";
            }
            
            // Clean other resources.
            close(l_socket);
            
            if (debug_mode) {
                toscreen << "Disconneted with the client.\n";
            }
            
            pthread_exit(nullptr);
        } else if (recv_head == h_send_log || recv_head == h_send_log_need_reply) {
            if (debug_mode) {
                toscreen << "This message from client is a log.\n";
            }
            
            // Read log.
            wttool::safe_read(l_socket, buffer, 4 + 2 + 4 + 2);
            uint16_t con_size = ntohs(*(uint16_t*)(buffer + 4 + 2 + 4));
            wttool::safe_read(l_socket, buffer + 12, (size_t)con_size);
            
            if (debug_mode) {
                toscreen << "Log size: " << con_size << ".\n";
            }
            
            // Construct SendInfo and push that to _send_to_lander queue.
            SendInfo info(recv_head, string(buffer, con_size + 12));
            server->_send_to_lander.push(info);
            
            if (debug_mode) {
                toscreen << "Send the log to queue successfully.\n";
            }
            
            // If need reply, establish mappings of hash_id and client_socket.
            if (recv_head == h_send_log_need_reply) {
                uint32_t hash_id = ntohl(*(uint32_t*)(buffer + 6));
                server->_hash_socket[hash_id] = l_socket;
                
                if (debug_mode) {
                    toscreen << "It is a log need reply. Saved hash_id: " << hash_id << ".\n";
                }
            }
            
        } else {
            toscreen << "Unsupported head: " << recv_head << ".\n";
        }
    }
    pthread_exit(nullptr);
}

void* WTLogServer::_send_client(void* args) {
    char buffer[10240];
    SendInfo s_info;
    WTLogServer* server = (WTLogServer*)args;
    while (server->_on_listen == true || server->_send_to_client.size() != 0) {
        if (server->_send_to_client.get(&s_info) == false) {
            // Nothing to be sent.
            usleep(1e5);
            continue;
        }
        
        if (s_info.head == h_log_receive_success) { // Is a log reply.
            if (debug_mode) {
                toscreen << "Found a log reply from the _send_to_client queue.\n";
            }
        
            // Construct message.
            uint16_t h_sent = htons(s_info.head);
            memcpy(buffer, &h_sent, sizeof(uint16_t));
            memcpy(buffer + sizeof(uint16_t), s_info.content.c_str(), 4 + 2);
            uint16_t rly_len = ntohs(*(uint16_t*)(s_info.content.c_str() + 4));
            memcpy(buffer + 64, s_info.content.c_str() + 6, (size_t)rly_len);
            
            if (debug_mode) {
                toscreen << "Reply message length: " << rly_len << ".\n";
            }
            
            // Get the reply target client.
            uint32_t hash_id = ntohl(*(uint32_t*)s_info.content.c_str());
            int c_socket;
            if (server->_hash_socket.find_and_remove(hash_id, &c_socket) == false) {
                // The link from this target client has been disconnected.
                toscreen << "Cannot find the corresponding client, discard the reply.\n";
                continue;
            }
            
            if (debug_mode) {
                toscreen << "The reply hash_id: " << hash_id << ".\n";
            }
            
            // Send to client.
            write(c_socket, buffer, 2 + 4 + 2 + rly_len);
            
            if (debug_mode) {
                toscreen << "Successuflly sent the reply to client.\n";
            }
            
        } else { 
            toscreen << "Send to client find unknown head: " << s_info.head << ".\n";
        }
    }
    pthread_exit(nullptr);
}

void* WTLogServer::_listen_lander(void* args) {
    char buffer[10240];
    WTLogServer* server = (WTLogServer*)args;
    while (server->_on_listen == true || server->_send_t.size() != 0) {
        // Get all sockets to lander.
        std::vector<int> alive_lander_socket;
        server->_send_t.get_all(&alive_lander_socket, nullptr);
        
        // Looply check each lander.
        uint16_t recv_head;
        for (size_t i = 0; i < alive_lander_socket.size(); ++i) {
            // Try to read a message head.
            int cur_s = alive_lander_socket[i];
            int ret = read(cur_s, &recv_head, sizeof(uint16_t));
            if (ret <= 0) {
                // No message. Check next lander.
                continue;
            }
            // Have message, read and handle.
            recv_head = ntohs(recv_head);
            if (recv_head == h_log_receive_success) {
                if (debug_mode) {
                    toscreen << "From TCP, received a reply message.\n";
                }
                
                // Is a success reply to client.
                // Read the reply message. Construct the SendInfo.
                SendInfo s_inf;
                s_inf.head = recv_head;
                wttool::safe_read(cur_s, buffer, 4 + 2);
                uint16_t rly_len = ntohs(*(uint16_t*)(buffer + 4));
                wttool::safe_read(cur_s, buffer + 6, rly_len);
                s_inf.content = string(buffer, 6 + rly_len);
                
                if (debug_mode) {
                    uint32_t rly_hash_id = ntohl(*(uint32_t*)s_inf.content.c_str());
                    toscreen << "The reply hash_id is: " << rly_hash_id << ".\n";
                }
                
                // Push SendInfo to _send_to_client queue.
                server->_send_to_client.push(s_inf);
                
                if (debug_mode) {
                    toscreen << "Pushed this message to _send_to_client queue.\n";
                }
                
                continue;
            } else if (recv_head == h_stop_send_log) {
                // Lander told the server not to send log to it.
                if (debug_mode) {
                    toscreen << "Received the lander's not sending log request.\n";
                }
                server->_on_send[cur_s] = false;
                
                // Waitting the _send_lander thread to close.
                pthread_t st;
                if (server->_send_t.find(cur_s, &st) == false) {
                    toscreen << "[ERROR]Cannot find the thread id of a lander.\n";
                    continue;
                }
                pthread_join(st, nullptr);
                if (debug_mode) {
                    toscreen << "The _send_lander thread is closed.\n";
                }
                
                // Send feedback.
                uint16_t s_head = htons(h_stop_send_log_reply);
                write(cur_s, &s_head, sizeof(uint16_t));
                
                if (debug_mode) {
                    toscreen << "Sent h_stop_send_log_reply.\n";
                }
            } else if (recv_head == h_close_with_lander) {
                // This lander won't send any reply message to this server.
                if (debug_mode) {
                    toscreen << "Received h_close_with_lander from lander.\n";
                }
                
                // Clean the resources for this lander.
                server->_send_t.find_and_remove(cur_s, nullptr);
                server->_on_send.erase(cur_s);
                server->_socket_info.find_and_remove(cur_s, nullptr);
                
                // Send reply.
                uint16_t s_head = htons(h_close_with_lander_reply);
                write(cur_s, &s_head, sizeof(uint16_t));
                
                // Close.
                close(cur_s);
                if (debug_mode) {
                    toscreen << "Closed the socket, finish all connection with the lander.\n";
                }
            } else {
                toscreen << "Listen from lander find unknown head: " << recv_head << ".\n";
            }
        }
    }
    pthread_exit(nullptr);
}

void* WTLogServer::_send_lander(void* args) {
    int l_socket = *(int*)args;
    WTLogServer* server = *(WTLogServer**)(args + sizeof(int));
    free(args);
    char buffer[10240];
    SendInfo s_info;
    while (server->_on_listen == true || server->_send_to_lander.size() != 0) {
        // Check the local tag.
        if (server->_on_send[l_socket] == false) {
            // The lander has told this server not send log to it.
            server->_on_send.erase(l_socket);
            pthread_exit(nullptr);
        }
        
        // Get a new message to lander.
        if (server->_send_to_lander.get(&s_info) == false) {
            // No message.
            usleep(2e5);
            continue;
        }
        
        // Handle the message.
        if (s_info.head == h_send_log || s_info.head == h_send_log_need_reply) {
            // Is a log message.
            uint16_t head_sent = htons(s_info.head);
            memcpy(buffer, &head_sent, sizeof(uint16_t)); // Head.
            memcpy(buffer + 2, s_info.content.c_str(), 4 + 2 + 4 + 2); // Time, Level, hash_id, content_size.
            uint16_t log_len = ntohs(*(uint16_t*)(buffer + 12));
            memcpy(buffer + 14, s_info.content.c_str() + 12, (size_t)log_len); // Log content.
            
            if (debug_mode) {
                uint32_t sent_hash_id = ntohl(*(uint32_t*)(s_info.content.c_str() + 6));
                toscreen << "Start to send a log to lander, hash_id: " << sent_hash_id 
                    << ", content length: " << log_len << ".\n";
            }
            
            // Send to the lander.
            wttool::safe_write(l_socket, buffer, 14 + log_len);
            
            if (debug_mode) {
                toscreen << "Sent totally: " << 14 + log_len << " bytes.\n";
            }
        } else {
            toscreen << "Send to lander find unknown head: " << s_info.head << ".\n";
        }
    }
    pthread_exit(nullptr);
}

} // End namespace wtlog.
