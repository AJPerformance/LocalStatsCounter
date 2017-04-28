# LocalStatsCounter
High performance  multithreaded local stats counter(counter in stack not in global scope) that is 1000% (10 times) faster than normal gloabal atomic couters and same speed as a local variable.

Total threads: 5 Threads
   1) one collector thread
   2) 2 threads running on global atomic variable
   3) 2 threads running on group atomic variable

Stats Objects: 2
   global atomic variable
   group atomic variable

Performance Test:
   System Details: 4 prcessor/dual core Intel(R) Core(TM) i7-4600U CPU @ 2.10GHz
   Compiler      : g++ (Ubuntu 4.8.4-2ubuntu1~14.04.3) 4.8.4

2 stats counter with global atomic (slow approch)
==================================
global stats count = 186889093
group stats count = 205467733

Total Stats = 392356826 = 392 million;

2 stats counter with local atomic and stats collector running every 1 second (fast approch)
======================================================================

global stats count = 1733506083
group stats count = 1787117273
total stats = 3520623356 = 3.5 billion

Total diff = 3128266530  //10 times faster  than global atomic

id: 0 value: 1733506083 ( == global)
id: 10 value: 1787117273 ( == group)

To Compile:

 g++ stats.cpp -std=c++11 -pthread -DLOCAL_ATOMIC -o fast
     This will produce fast stats with local atomic variable with a collector
 g++ stats.cpp -std=c++11 -pthread  -o slow
     This will produce slow stats with global atomic
     
     
Improvement: check similar idea is implemeted 
   http://stackoverflow.com/questions/11365351/how-to-implement-efficient-c-runtime-statistics
struct Counter {
    unsigned long c_;
    unsigned long operator++ () { return ++c_; }
    operator unsigned long () const { return c_; }
    void reset () { unsigned long c = c_; ATOMIC_DECREMENT(c_, c); }
    Counter (std::string name);
};

struct CounterAtomic : public Counter {
    unsigned long operator++ () { return ATOMIC_INCREMENT(c_, 1); }
    CounterAtomic (std::string name) : Counter(name) {}
};

struct CounterRepository {
    typedef std::map<std::string, Counter *> MapType;
    mutable Mutex lock_;
    MapType map_;
    void add (std::string n, Counter &c) {
        ScopedLock<Mutex> sl(lock_);
        if (map_.find(n) != map_.end()) throw n;
        map_[n] = &c;
    }
    Counter & get (std::string n) const {
        ScopedLock<Mutex> sl(lock_);
        MapType::const_iterator i = map_.find(n);
        if (i == map_.end()) throw n;
        return *(i->second);
    }
};

CounterRepository counterRepository;

Counter::Counter (std::string name) {
    counterRepository.add(name, *this);
}
