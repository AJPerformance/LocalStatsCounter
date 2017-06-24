#include <pthread.h>
#include <system_error>
#ifndef _THREAD_STORAGE_H
#define _THREAD_STORAGE_H

  template <typename T>
  class DefaultThreadStorageDestructor {
  public:
    typedef T   ValueType;

    void operator ()(ValueType) {
    }
  };

  template <typename T, typename U = DefaultThreadStorageDestructor<T> >
  class ThreadStorage;

template <typename U>
class ThreadStorage<void*, U> {
public:
  typedef void* ValueType;
  typedef U     DestructorType; 
    
  ThreadStorage() {
    int32_t returnCode = ::pthread_key_create(&this->tlsKey_,
        ThreadStorage::destructor);
    if (returnCode != 0)
      throw std::system_error(returnCode, std::generic_category());
  }

  virtual ~ThreadStorage() {
    int32_t returnCode = ::pthread_key_delete(this->tlsKey_);
    if (returnCode != 0)
      throw std::system_error(returnCode, std::generic_category());
  } 
  
  ValueType data() const {
    return ::pthread_getspecific(this->tlsKey_);
  } 
    
  void data(ValueType value) {
    int32_t returnCode = ::pthread_setspecific(this->tlsKey_, value);
    if (returnCode != 0)
      throw std::system_error(returnCode, std::generic_category());
  } 
  
private: 
  static void destructor(ValueType value) {
    DestructorType dtor;
    dtor(value);
  }

  pthread_key_t tlsKey_;
};

  template <typename T, typename U>
  class ThreadStorage {
  public:
    typedef T   ValueType;
    typedef U   DestructorType;

    ThreadStorage() {
    }

    virtual ~ThreadStorage() {
    }

    ValueType data() const {
      return reinterpret_cast<T>(this->data_.data());
    }

    void data(ValueType value) {
      this->data_.data(reinterpret_cast<void*>(value));
    }

  private:
    class VoidDestructor {
    public:
      void operator ()(void* value) {
        DestructorType dtor;
        dtor(reinterpret_cast<ValueType>(value));
      }
    };

    ThreadStorage(const ThreadStorage&);
    ThreadStorage& operator =(const ThreadStorage&);

    ThreadStorage<void*, VoidDestructor> data_;
  };


#endif /* _THREAD_STORAGE_H */
