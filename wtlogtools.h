/**
 * Some tools used by wtlog.
 * Author: LiWentan.
 * Date: 2019/7/16.
 */

#ifndef _WTLOG_TOOLS_H_
#define _WTLOG_TOOLS_H_

#include <unordered_map>
#include <pthread.h>
#include <ctime>
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <cstdlib>
#include <stdio.h>
#include <unistd.h>
#include <vector>
#include <sstream>
#include <fcntl.h>
#include <utility>
#include <map>
#include "wtatomqueue.hpp"

#define toscreen std::cout<<__FILE__<<", "<<__LINE__<<": "

typedef std::string string;
typedef std::stringstream stringstream;

namespace wtatom {

static void lock(pthread_mutex_t& lock) {
    int ret = pthread_mutex_lock(&lock);
    if (ret != 0) {
        toscreen << "Lock mutux_lock failed, try again after 1 sec. "
            << "The error code is " << ret << ".\n";
        sleep(1);
        ret = pthread_mutex_lock(&lock);
    }
    return;
}

static void lockr(pthread_rwlock_t& lock) {
    int ret = pthread_rwlock_rdlock(&lock);
    while (ret != 0) {
        toscreen << "Lock read_lock failed, try again after 1 sec. "
            << "The error code is " << ret << ".\n";
        sleep(1);
        ret = pthread_rwlock_rdlock(&lock);
    }
    return;
}

static void lockw(pthread_rwlock_t& lock) {
    int ret = pthread_rwlock_wrlock(&lock);
    while (ret != 0) {
        toscreen << "Lock write_lock failed, try again after 1 sec. "
            << "The error code is " << ret << ".\n";
        sleep(1);
        ret = pthread_rwlock_wrlock(&lock);
    }
    return;
}

static void unlock(pthread_mutex_t& lock) {
    int ret = pthread_mutex_unlock(&lock);
    while (ret != 0) {
        toscreen << "Unlock mutux_lock failed, try again after 1 sec. "
            << "The error code is " << ret << ".\n";
        sleep(1);
        ret = pthread_mutex_unlock(&lock);
    }
    return;
}

static void unlock(pthread_rwlock_t& lock) {
    int ret = pthread_rwlock_unlock(&lock);
    while (ret != 0) {
        toscreen << "Unlock rw_lock failed, try again after 1 sec. "
            << "The error code is " << ret << ".\n";
        sleep(1);
        ret = pthread_rwlock_unlock(&lock);
    }
    return;
}

template <typename KeyType, typename ValType>
class AtomMap {
public:
    template <typename K, typename V>
    class ElementPointer {
    public:
        ElementPointer(AtomMap<K, V>* map, const K& key) : _map(map), _key(key) {}
        ElementPointer(const ElementPointer<K, V>& content) : _map(content._map), _key(content._key) {}
        ElementPointer<K, V>& operator=(const ElementPointer<K, V>& content) {
            _map = content._map;
            _key = content._key;
        }
        bool operator==(const ElementPointer<K, V>& content) {
            if (_map == content._map && _key == content._key) {
                return true;
            }
            return false;
        }
        V operator*() {
            V res;
            _map->find(_key, &res);
            return res;
        }
        ElementPointer<K, V>& operator=(const V& val) {
            _map->insert(std::make_pair(_key, val));
        }
    
    private:
        AtomMap<K, V>* _map;
        K _key;
    };

public:
    /**
     * Construtive function.
     */
    AtomMap() {
        pthread_rwlock_init(&_lock, nullptr);
    }
    
    /**
     * Distructive function.
     */
    virtual ~AtomMap() {}
    
    /**
     * Insert elements.
     * @return true: Success.
     * @return false: Already existing key. Overwrite the value.
     */
    bool insert(const std::pair<KeyType, ValType>& content) {
        lockw(_lock);
        auto it = _data.find(content.first);
        if (it != _data.cend()) {
            it->second = content.second;
            unlock(_lock);
            return false;
        }
        _data.insert(content);
        unlock(_lock);
        return true;
    }
    
    /**
     * Get the value. Also can be used as find.
     * @return true: Found key, write the parameter val as the correspond one.
     * @return false: Unexisting key.
     */
    bool find(const KeyType& key, ValType* val = nullptr) {
        lockr(_lock);
        auto it = _data.find(key);
        if (it == _data.cend()) {
            unlock(_lock);
            return false;
        }
        if (val != nullptr) {
            *val = it->second;
        }
        unlock(_lock);
        return true;
    }
    
    /**
     * Get the value, then remove the element from the map.
     * @return true: Success.
     * @return false: Unexisting key.
     */
    bool find_and_remove(const KeyType& key, ValType* val = nullptr) {
        lockw(_lock);
        auto it = _data.find(key);
        if (it == _data.cend()) {
            unlock(_lock);
            return false;
        }
        if (val != nullptr) {
            *val = it->second;
        }
        _data.erase(it);
        unlock(_lock);
        return true;
    }
    
    /** 
     * Get all elements.
     */
    void get_all(std::vector<KeyType>* key, std::vector<ValType>* val) {
        lockr(_lock);
        auto it = _data.begin();
        while (it != _data.end()) {
            if (key != nullptr) {
                key->push_back(it->first);
            }
            if (val != nullptr) {
                val->push_back(it->second);
            }
            ++it;
        }
        unlock(_lock);
    }
    
    /**
     * Return the ElementPointer of the key.
     * You can use * to get the value, = to overwrite the value.
     */
    ElementPointer<KeyType, ValType> operator[](const KeyType& key) {
        return ElementPointer<KeyType, ValType>(this, key);
    }
    
    /**
     * Get the size.
     */
    size_t size() {
        lockr(_lock);
        size_t res = _data.size();
        unlock(_lock);
        return res;
    }
    
    /**
     * Clear.
     */
    void clear() {
        lockw(_lock);
        _data.clear();
        unlock(_lock);
    }
    
private:
    std::unordered_map<KeyType, ValType> _data;
    pthread_rwlock_t _lock;
};

} // End namespace wtatom.

namespace wttool {

/**
 * Generate hash number by string and time.
 */    
static uint32_t str2hash(const string& str, bool use_time = false) {
    static uint32_t seed = 19299;
    uint32_t res = 1;
    if (use_time) {
        struct timeval tv;
        gettimeofday(&tv, nullptr);
        res = tv.tv_sec * 1e6 + tv.tv_usec;
    }
    for (size_t i = 0; i < str.size(); ++i) {
        res *= seed;
        res *= str[i];
        res += str[i];
    }
    return res;
}

/**
 * Read data blockingly until it is completely read.
 * @return 0: Read the correct size.
 * @return 1: Timeout.
 */
static int safe_read(int socket, void* buffer, size_t size) {
    size_t fin_size = 0;
    while (fin_size < size) {
        int ret = read(socket, buffer + fin_size, size - fin_size);
        if (ret == size - fin_size) {
            // Finish read.
            return 0;
        }
        if (ret <= 0) {
            // Read nothing.
            continue;
        }
        fin_size += ret;
    }
    return 0;
}

/**
 * Write data to a non-blocking socket.
 */
static int safe_write(int socket, void* buffer, size_t size) {
    size_t fin_size = 0;
    while (fin_size < size) {
        int ret = write(socket, buffer + fin_size, size - fin_size);
        if (ret == size - fin_size) {
            return 0;
        }
        if (ret <= 0) {
            continue;
        }
        fin_size += ret;
    }
    return 0;
}

/**
 * Get current date, yyyymmdd.
 */
static string cur_date() {
    time_t cur_sec;
    std::time(&cur_sec);
    tm local_time;
    localtime_r(&cur_sec, &local_time);
    char res[9];
    sprintf(res, "%04d%02d%02d", local_time.tm_year + 1900,
        local_time.tm_mon + 1, local_time.tm_mday);
    return res;
}

/**
 * Convert a string to number.
 */
static int32_t str2num(const string& str) {
    int32_t num;
    sscanf(str.c_str(), "%d", &num);
    return num;
}

/**
 * Convert a number to string.
 */
static string num2str(int32_t num) {
    char str[33];
    sprintf(str, "%d", num);
    return str;
}

/**
 * Char* to string.
 */
static string cstr2str(const char* str) {
    return string(str);
}

} // End namespace wttool.

#endif // End ifdef _WTLOG_TOOLS_H_.
