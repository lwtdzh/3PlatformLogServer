/**
 * DEMO of AtomQueue.
 */

#include <iostream>
#include <pthread.h>
#include <map>
#include <set>
#include <unistd.h> 
#include "atomqueue.hpp"

using std::cout;

wtatom::AtomQueue<int> q(2);

void* push_to(void* args) {
    int num = *(int*)args;
    q.push(num);
    return nullptr;
}

void* get_from(void* args) {
    int* num = (int*)args;
    int tmp;
    if (q.get(tmp) == true) {
        *num = tmp;
        return nullptr;
    }
    return nullptr;
}

pthread_t get_t[3000];
pthread_t push_t[1000];

int s[3000];

int main() {
    
    for (size_t i = 0; i < 1000; ++i) {
        s[i] = -1;
    }
    
    std::set<int> sst;
    
    for (size_t i = 0; i < 3000; ++i) {
        bool* res;
        int ret = 0;
        if (i < 1000)
        int ret = pthread_create(&push_t[i], NULL, push_to, new int(i));
        if (ret != 0) {
            cout << "dasdas\n";
        }
        
        ret = pthread_create(&get_t[i], NULL, get_from, &s[i]);
        if(ret !=0) {
            cout << "dasdas\n";
        }
    }
    
    cout <<"wait\n";
    for (size_t i = 0; i < 3000; ++i) {
        int* k;
        void** a = (void**)&k;
        if (i < 1000) {
            cout << "Wait P:" << i << "\n";
            pthread_join(push_t[i], a);
        }
        cout << "Wait G:" << i << "\n";
        pthread_join(get_t[i], a);
       
    }
    
    for (size_t i = 0; i < 3000; ++i) {
        if (s[i] == -1) continue;
        
        sst.insert(s[i]);
    }
    
    for (size_t i = 0; i < 1000; ++i) {
        if (sst.find(i) == sst.end()) {
            cout << "NO!!"<<i<<"\n";
        }
    }
    
    return 0;
}
