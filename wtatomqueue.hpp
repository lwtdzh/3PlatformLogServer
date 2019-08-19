/**
 * A multi-thread firendly queue.
 * If you want to use the queue, include this file.
 * Do not include atomqueue.h directly in your project.
 * Author: LiWentan.
 * Date: 2019/7/15.
 */

#ifndef _ATOM_QUEUE_HPP_
#define _ATOM_QUEUE_HPP_

#include <unistd.h>
#include "wtatomqueue.h"

namespace wtatom {

template <typename T>  
AtomQueue<T>::AtomQueue(size_t c_in) : 
    _capacity(c_in), _head(0), _tail(0) {
    _data = new(std::nothrow) DataElement<T>[_capacity];
    if (_data == nullptr) {
        throw AtomQueueException("Malloc memory for queue failed.");
    }
    if (pthread_mutex_init(&_find_space_lock, nullptr) != 0) {
        throw AtomQueueException("Initialize space lock Failed.");
    }
    if (pthread_rwlock_init(&_global_lock, nullptr) != 0) {
        throw AtomQueueException("Initialize global lock failed.");
    }
}

template <typename T>  
AtomQueue<T>::~AtomQueue() {
    if (_data != nullptr) {
        delete[] _data;
    }
}

template <typename T>  
void AtomQueue<T>::push(const T& in) {
    // Get the logic position of next empty space.
    size_t next_pos = _occupy_next_empty_space_and_global_lock();

    // Wait for the former get operation at this position.
    while (true) {
        // Wait if this position is under reading progress.
        _lock(&_data[next_pos]._lock);
        
        // Check if the former get has finished.
        if (_data[next_pos]._valid == false) {
            // This space is safe. Start to push data.
            break;
        } else {
            // This space is still invalid until the former get finished.
            _unlock(&_data[next_pos]._lock);
            continue;
        }
    }
    
    // Nobody is using this position, start to push data here.
    _data[next_pos]._data = in;
    _data[next_pos]._valid = true;
    
    // Release the space for other operation.
    _unlock(&_data[next_pos]._lock);
    
    // Release the read_lock of global_lock.
    _unlock(&_global_lock);
    
    return;
}

template <typename T>
bool AtomQueue<T>::get(T& out) {
    return get(&out);
}

template <typename T>
bool AtomQueue<T>::get(T* out) {
    // Get the top position.
    size_t top_pos = 0;
    if (_pop_top_space_and_global_lock(top_pos) == false) {
        // Empty queue.
        return false;
    }
    
    // Wait for the former push operation at this position.
    while (true) {
        _lock(&_data[top_pos]._lock);
        
        // Check if the former push has finished.
        if (_data[top_pos]._valid == true) {
            // Nobody is using this position, start to push data here._valid == true) {
            // This space is safe. Start to push data.
            break;
        } else {
            // This space is still invalid until the former push finished.
            _unlock(&_data[top_pos]._lock);
            continue;
        }
    }
    
    // Nobody is using this position, start to get data here.
    if (out != nullptr) {
        *out = _data[top_pos]._data;
    }
    _data[top_pos]._valid = false;
    
    // Release the space for other operation.
    _unlock(&_data[top_pos]._lock);
    
    // Release the read_lock of global_lock.
    _unlock(&_global_lock);
    
    return true;
}

template <typename T>
void AtomQueue<T>::clear() {
    _lock(&_find_space_lock);
    
    _head = 0;
    _tail = 0;
    
    _unlock(&_find_space_lock);
}

template <typename T>  
size_t AtomQueue<T>::_occupy_next_empty_space_and_global_lock() {
    _lock(&_find_space_lock);
    
    // Check the capacity.
    if ((_tail + 1) % _capacity == _head) {
        // Space is full.
        bool ret = _expansion();
        while (ret != true) {
            toscreen << "Expansion failed, wait 1 sec.\n";
            sleep(1);
            ret = _expansion();
        }
    }
    
    // Get the next_space position.
    size_t next_space = _tail;
    
    // Modify the _tail to next position.
    _tail = (_tail + 1) % _capacity;
    
    // LockR the global_lock.
    _lockr(&_global_lock);
    
    // Finish.
    _unlock(&_find_space_lock);
    return next_space;
}

template <typename T>  
bool AtomQueue<T>::_pop_top_space_and_global_lock(size_t& pos) {
    _lock(&_find_space_lock);
    
    // Check if the queue is empty.
    if (_tail  == _head) {
        _unlock(&_find_space_lock);
        return false;
    }
    
    // Set the pos.
    pos = _head;
    _head = (_head + 1) % _capacity;
    
    // LockR the global_lock.
    _lockr(&_global_lock);
    
    // Finish.
    _unlock(&_find_space_lock);
    return true;
}

template <typename T>  
size_t AtomQueue<T>::_abso2logic(size_t abso_pos) {
    if (abso_pos >= _head) {
        return abso_pos - _head;
    }
    return _capacity - _head + abso_pos;
}

template <typename T>  
size_t AtomQueue<T>::_logic2abso(size_t logic_pos) {
    return (_head + logic_pos) % _capacity;
}

template <typename T>  
bool AtomQueue<T>::_expansion() {
    _lockw(&_global_lock);
    
    // Allocate new space.
    DataElement<T>* new_data = new(std::nothrow) DataElement<T>[_capacity * 2];
    if (new_data == nullptr) {
        _unlock(&_global_lock);
        return false;
    }
    
    // Copy data to the new space.
    for (size_t i = 0; i < _capacity; ++i) {
        new_data[i] = _data[_logic2abso(i)];
    }
    
    // Set the index variables.
    _head = 0;
    _tail = _capacity - 1;
    _capacity = _capacity * 2;
    
    // Move the _data pointer.
    delete[] _data;
    _data = new_data;
    
    _unlock(&_global_lock);
    return true;
}

template <typename T>  
void AtomQueue<T>::_lock(pthread_mutex_t* lock) {
    int ret = pthread_mutex_lock(lock);
    if (ret != 0) {
        toscreen << "Lock mutux_lock failed, try again after 1 sec. "
            << "The error code is " << ret << ".\n";
        sleep(1);
        ret = pthread_mutex_lock(lock);
    }
    return;
}

template <typename T>  
void AtomQueue<T>::_lockr(pthread_rwlock_t* lock) {
    int ret = pthread_rwlock_rdlock(lock);
    while (ret != 0) {
        toscreen << "Lock read_lock failed, try again after 1 sec. "
            << "The error code is " << ret << ".\n";
        sleep(1);
        ret = pthread_rwlock_rdlock(lock);
    }
    return;
}

template <typename T>  
void AtomQueue<T>::_lockw(pthread_rwlock_t* lock) {
    int ret = pthread_rwlock_wrlock(lock);
    while (ret != 0) {
        toscreen << "Lock write_lock failed, try again after 1 sec. "
            << "The error code is " << ret << ".\n";
        sleep(1);
        ret = pthread_rwlock_wrlock(lock);
    }
    return;
}

template <typename T>  
void AtomQueue<T>::_unlock(pthread_mutex_t* lock) {
    int ret = pthread_mutex_unlock(lock);
    while (ret != 0) {
        toscreen << "Unlock mutux_lock failed, try again after 1 sec. "
            << "The error code is " << ret << ".\n";
        sleep(1);
        ret = pthread_mutex_unlock(lock);
    }
    return;
}

template <typename T>  
void AtomQueue<T>::_unlock(pthread_rwlock_t* lock) {
    int ret = pthread_rwlock_unlock(lock);
    while (ret != 0) {
        toscreen << "Unlock rw_lock failed, try again after 1 sec. "
            << "The error code is " << ret << ".\n";
        sleep(1);
        ret = pthread_rwlock_unlock(lock);
    }
    return;
}

template <typename T>  
size_t AtomQueue<T>::size() {
    _lock(&_find_space_lock);
    if (_head <= _tail) {
        _unlock(&_find_space_lock);
        return _tail - _head;
    }
    _unlock(&_find_space_lock);
    return _tail + _capacity - _head;
}

} // End namespace wtatom.

#endif // End ifdef _ATOM_QUEUE_HPP_.
