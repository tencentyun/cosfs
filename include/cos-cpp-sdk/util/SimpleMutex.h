#ifndef SIMPLE_MUTEX_H
#define SIMPLE_MUTEX_H
#include <pthread.h>

class SimpleMutex {
public:
    SimpleMutex() {
        pthread_mutex_init(&m_mutex, NULL);
    }

    ~SimpleMutex() {
        pthread_mutex_destroy(&m_mutex);
    }

    void Lock() {
        pthread_mutex_lock(&m_mutex);
    }

    void Unlock() {
        pthread_mutex_unlock(&m_mutex);
    }

private:
    pthread_mutex_t m_mutex;
};

// mutex holder
class SimpleMutexLocker {
public:
    SimpleMutexLocker(SimpleMutex* mutex) : m_mutex(mutex) {
        m_mutex->Lock();
    }

    ~SimpleMutexLocker() {
        m_mutex->Unlock();
    }

private:
    SimpleMutex* m_mutex;
};

#endif