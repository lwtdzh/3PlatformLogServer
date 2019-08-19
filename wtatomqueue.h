/**
 * A multi-thread firendly queue.
 * If you want to use the queue, include hpp file.
 * Do not include this file directly in your project.
 * Author: LiWentan.
 * Date: 2019/7/15.
 */
 
#ifndef _ATOM_QUEUE_H_
#define _ATOM_QUEUE_H_

#include <pthread.h>
#include <iostream>

#define toscreen std::cout<<__FILE__<<", "<<__LINE__<<": "

using std::string;

namespace wtatom {

class AtomQueueException : public std::exception {
public:
    AtomQueueException(const string& info) : _info(info) {}
    virtual const char* what() {
        return _info.c_str();
    }
private:
    string _info;
};

template <typename T>
class AtomQueue {
private:
    template <typename TE>
    class DataElement {
    public:
        DataElement(const TE& in) : _data(in), _valid(false) {
            if (pthread_mutex_init(&_lock, nullptr) != 0) {
                throw AtomQueueException("Initialize local lock Failed.");
            }
        }
        DataElement() : _valid(false) {
            if (pthread_mutex_init(&_lock, nullptr) != 0) {
                throw AtomQueueException("Initialize local lock Failed.");
            }
        }
        DataElement<TE>& operator=(const DataElement<TE>& rhs) {
            _data = rhs._data;
            _valid = rhs._valid;
        }
        TE _data;
        pthread_mutex_t _lock;
        bool _valid; // If false, this element is waitting for being pushed.
    };
    
public:
    /**
     * Construction and distruction function.
     * You need avoid queue expansion as much as possible.
     * The overhead of queue expansion is large.
     * Pre-allocate a maximum space based on usage context.
     */
    AtomQueue(size_t reserve_capacity = 2048);
    virtual ~AtomQueue();
    
    /**
     * Push an element to the tail.
     */
    void push(const T& in);
    
    /**
     * Get the element at the top. Then remove the top element from the queue.
     * If you want to delet the top element, you can set out as NULL. 
     * @return true: Success.
     * @return false: Failed. Variable "out" would not be modified. 
     *      The reason may be that the queue is empty. Try again.
     *      If not empty, the top element won't lose, but it may be moved to the tail.
     */
    bool get(T* out); // Recommended.
    bool get(T& out); // Not recommended.
    
    /**
     * Get the size of the queue.
     */
    size_t size();
    
    /**
     * Clear.
     * It should only be called when all push and get operations are finished.
     * If some operations haven't finished, it may cause data incorrect.
     */
    void clear();
    
private:
    DataElement<T>* _data;
    size_t _head;
    size_t _tail;
    size_t _capacity;
    pthread_mutex_t _find_space_lock; // Used when finding space.
    pthread_rwlock_t _global_lock; // Used when expanding.
    
private:
    /**
     * Allocate a space for new element. This space will be ready for push.
     * This space will be locked.
     * You must use this space, and unlock this space.
     * If space is full, it will expand the space.
     * Global_lock will be read_locked before this function return.
     * You must unlock the glock_lock manumally.
     * Return the absolute position.
     */
    size_t _occupy_next_empty_space_and_global_lock();
    
    /**
     * Set the top absolute position to &pos. This space will be ready for get.
     * This space will be locked.
     * You must use this space, and unlock this space.
     * Global_lock will be read_locked before this function return.
     * You must unlock the glock_lock manumally.
     * @return true: Top space is occupied. Global_lock is locked.
     * @return false: Empty queue. Global_lock won't be locked.
     */
    bool _pop_top_space_and_global_lock(size_t& pos);
  
    /**
     * Convert the logic position to absolute position.
     */
    size_t _logic2abso(size_t logic_pos);
    
    /**
     * Convert the absolute position to logic position.
     */
    size_t _abso2logic(size_t abso_pos);
    
    /**
     * Expansion the space twice.
     * Used when the capacity is not enough.
     * High cost. Return true means success.
     */
    bool _expansion();
    
    /**
     * Lock or unlock safely.
     */
    static void _lock(pthread_mutex_t* lock);
    static void _lockr(pthread_rwlock_t* lock);
    static void _lockw(pthread_rwlock_t* lock);
    static void _unlock(pthread_mutex_t* lock);
    static void _unlock(pthread_rwlock_t* lock);
};
    
} // End namespace wtatom.

#endif // End ifdef _ATOM_QUEUE_H_.

