//compile g++ stats.cpp -std=c++11 -pthread -DLOCAL_ATOMIC -o fast

// atomic::exchange example
#include <iostream>       // std::cout
#include <atomic>         // std::atomic
#include <thread>         // std::thread
#include <vector>         // std::vector
#include <unordered_set>
#include <unordered_map>
#include <mutex>
#include <map>
#include <chrono>
#include "ThreadStorage.h" 
#include <stdio.h>      /* printf */
#include <stdarg.h>   

/* define  LOCAL_ATOMIC to 1 if you want to test distributed local stats */
//#define LOCAL_ATOMIC 1

void lprint(const char* format, ...)
{

   static  std::mutex _mtx;
    va_list argptr;
    va_start(argptr, format);
    { 
      std::lock_guard<std::mutex> lck (_mtx);
      vfprintf(stdout, format, argptr);
    }
    va_end(argptr);
   

}
class NCA /* no Copy and assignment */
{
      public:
         NCA() {}
      private:
         NCA(NCA const&) = delete;
         NCA& operator=(NCA const&) = delete;
};

class Singleton: public NCA
{
  protected:
    Singleton() {};
  private:

};

enum class StatType
{
  GLOBAL_STATS = 1, /* global stat id, similar to uint32_t globalStat */
  ARRAY_OF_STATS  = 2 /* collection of same stat id with different identifier, like std::vector< {id, counter} > */
};

struct State {
  enum Type {
    NONE = 0,     //< The stat hasn't been changed
    UPDATED = 1,  //< The stat was either incremented or decremented
    SET = 2       //< The stat was set
  };
};

/* all the stats counters must implement from StatCounter */
class StatCounter
{
  public:
     StatCounter()
      : _value(0), _state(State::Type::NONE)
     {} 
     StatCounter( const StatCounter& counter)
      : _value(counter.getStatValue()),
        _state(counter.getState())
     {
     }
     StatCounter& operator=(const StatCounter& other)
     {
       if (this == &other) return *this; 
       _value = other.getStatValue();
        _state = other.getState();
       return *this;
     }
     StatCounter& operator+=(StatCounter& other)
     {
       if (this != &other)
       {
          // lprint("%ld BEFORE += total statId: %ld, new: %ld\n",pthread_self(), getStatValue(), other.getStatValue());
         _state = other.getState();
         _value.fetch_add(other._value);
         //lprint("%ld AFTRE += total statId: %ld\n",pthread_self(), getStatValue());
       }
       return *this;
     }
     void copyAndReset(StatCounter& other)
     {
       if (this != &other)
       {
         _state = other.getState();
         _value =+ other.reset();
       }
     }
     void inc(uint64_t val) { _value+= val; _state =State::UPDATED; }
     void dec(uint64_t val) { _value-= val ;_state =State::UPDATED; }
     void set(uint64_t val) { _value = val ; _state =State::SET; }
     //TODO, ASH we need to reset instead of just get value
     uint64_t reset() { _state = State::NONE; return _value.exchange(0); }
     //uint64_t reset() { _state = State::NONE; return getStatValue(); }
     uint64_t getStatValue() const  { return _value.load(std::memory_order_relaxed); }
     State::Type getState() const { return _state; }
   protected:
     std::atomic<uint64_t> _value;
     State::Type _state;
};

/* StatsMap is a singleton globals  stats structure used by collector to collect and local variables to register */
class StatsMap
{
     
    public:
     StatsMap() : _usable(true) 
     {
        //lprint("%ld:%p Ctr called\n", pthread_self(), this);
     }
     ~StatsMap() 
     {
        //lprint("%ld:%p Dtr called\n", pthread_self(), this);
     }
     StatsMap(const StatsMap& other) 
      : _usable(other.isUsable())
      , _statsIds(other.getStatsMap())
     {  
        //lprint("%ld:%p CopyCtr called\n", pthread_self(), this);
     }
     StatsMap(StatsMap&& other) 
      : _usable(other.isUsable())
      , _statsIds(std::move(other.getStatsMap()))
     {  
        //lprint("%ld:%p MoveCtr called\n", pthread_self(), this);
     }
     StatsMap& operator=(StatsMap&& other) noexcept 
     {  
        //lprint("%ld:%p MoveOpreator called\n", pthread_self(), this);
        if (this != &other)
        {
          this->_statsIds = std::move(other.getStatsMap());
          this->_usable = other.isUsable();
        }
        return *this;
     }
     StatsMap& operator+=(StatsMap& stats)
     {
        uint64_t totalStats = 0;
        for(auto& stat: stats.getStatsMap())
        {
           StatCounter& current = getStats(stat.first);
           //lprint("%ld BEFORE += total statId: %ld, new: %ld\n",pthread_self(), current.getStatValue(), stat.second.getStatValue());
           current += stat.second;
           //lprint("%ld AFTER += total statId: %ld, \n",pthread_self(), current.getStatValue());
           totalStats += current.getStatValue();
        }
        //lprint("%ld OP += total statId: %d, Total value: %ld \n",pthread_self(), _statsIds.size(), totalStats;
        return *this;
     } 
     void copyAndResetStats(StatsMap& stats)
     {
        uint64_t totalStats = 0;
        for(auto& stat: stats.getStatsMap())
        {
           StatCounter& current = getStats(stat.first);
           current.copyAndReset(stat.second);
           //current += stat.second;
           totalStats += current.getStatValue();
        }
        //lprint("%ld OP += total statId: %d, Total value: %ld \n",pthread_self(), _statsIds.size(), totalStats);
     }
     void print()
     {
        uint64_t totalStats = 0;
        for(auto& stat: _statsIds)
        {
           uint64_t value = stat.second.getStatValue();
           totalStats += value;
           //std::cout<<"statId: "<<stat.first<< "value: "<< value<<std::endl;
           
        }
        lprint("%ld STATS MAP: total statId: %d, Total value: %ld \n",pthread_self(), _statsIds.size(), totalStats);
     }
     void set(uint64_t key, uint64_t val = 0) { StatCounter& stat = getStats(key); stat.set(val);} 
     void inc(uint64_t key, uint64_t val = 1) { StatCounter& stat = getStats(key); stat.inc(val );}
     void dec(uint64_t key, uint64_t val = -1) { StatCounter& stat = getStats(key); stat.dec(val );}
     StatCounter& getStats(uint64_t key) { return _statsIds[key];};
     bool isUsable() const { return _usable; }
     void setUnusable() { _usable = false; }
     std::unordered_map<uint64_t, StatCounter>& getStatsMap() { return _statsIds;}
     const std::unordered_map<uint64_t, StatCounter>& getStatsMap() const { return _statsIds;}
    protected:
      bool _usable;
      std::unordered_map<uint64_t, StatCounter> _statsIds;
      
};


/* ThreadStatsMapContainer is a singleton globals  stats structure used by collector to collect and local variables to register */
class ThreadStatsMapContainer : public Singleton
{
   public:
      __attribute__((noinline)) StatsMap aggregate()
      {
        //std::cout<<pthread_self()<<" enetring aggregatep"<<std::endl;
        StatsMap statsAggr;
        //lprint("%ld:%p Created Aggr Stats Map\n",pthread_self(), &statsAggr);
        std::lock_guard<std::mutex> lck (_mtx);
        auto it = _statsMap.begin();
        auto end = _statsMap.end();
        while(it != end)  
        {
           statsAggr.copyAndResetStats(it->second);
           if(!it->second.isUsable())
           {
             lprint("%ld:%p Aggr deleting stats map\n",pthread_self(), &(it->second));
              _statsMap.erase(it++);
           }
           else
           {
             ++it;
           }   
            
        }
        //lprint("%ld returning from aggr \n",pthread_self());
        return std::move(statsAggr);
     }
      
      __attribute__((noinline)) StatsMap* createStats()
      {
        lprint("%ld in create statsmap\n" ,pthread_self());
        std::lock_guard<std::mutex> lck (_mtx);
        StatsMap& stat = _statsMap[pthread_self()];
        //lprint("%ld:%p Created statsMap\n",pthread_self(), &stat);
        //std::cout<<pthread_self()<<": Created new StatsMap size: "<<_statsMap.size()<<std::endl;
        return &stat; 
      }
      static ThreadStatsMapContainer& getInstance()
      {
            static ThreadStatsMapContainer instance;
            return instance;
      }
    static StatsMap& getStatsCtxt()
    {
        StatsMap* statsCntr  = _statsTLS.data();
        if(nullptr == statsCntr)
        {
            lprint("%ld createing a new TLS \n" ,pthread_self());
            statsCntr =  getInstance().createStats();
            _statsTLS.data(statsCntr);
        }
        return *statsCntr;
    }
    static void i_set(uint64_t key, uint64_t val) {getStatsCtxt().set(key,val) ;};
    static void i_increment(uint64_t key, uint64_t val){getStatsCtxt().inc(key,val); };
    static void i_decrement(uint64_t key, uint64_t val) {getStatsCtxt().dec(key,val); };



      struct ThreadDestructor
      {
      public:
          void operator ()(StatsMap* value) {
              lprint("%ld IN ThreadDestructor \n" ,pthread_self());
              value->setUnusable();
          }
      };
    protected:
      std::unordered_map<pthread_t, StatsMap> _statsMap;
      std::mutex _mtx;
      static ThreadStorage<StatsMap*, ThreadDestructor>  _statsTLS;
};

ThreadStorage<StatsMap*, ThreadStatsMapContainer::ThreadDestructor> ThreadStatsMapContainer::_statsTLS;


//TEST code starts here
//testing
/*
 * all thread waits until it is ready object. stats are incremented once this value is set to true.  
 * collector signals thread to stop incrementing counter by sresetting ready value;
 */   
bool ready = false;
//dumpCollectedStats - collector starts dumping aggregated stats to console 
bool dumpCollectedStats = false;

//collectstats - start collecting stats and print aggregated values
void collectstats() 
{
   StatsMap aggr;
   lprint("%ld:%p Collect Stats\n",pthread_self(), &aggr);
   uint32_t maxcount = 15;
   while(!ready) {}
   while(maxcount--)
   {
     /* fetch stats every 1 second */
     std::this_thread::sleep_for(std::chrono::seconds(1));
     StatsMap stats = ThreadStatsMapContainer::getInstance().aggregate();
     aggr+= stats;
     //aggr.print(); 
   }
   ready = false;
   //std::cout << "Enering Final collection \n";
   while(!dumpCollectedStats) {} //wait for all threads to finish
   StatsMap stats = ThreadStatsMapContainer::getInstance().aggregate();
   aggr+= stats;
  
   aggr.print(); 
   //std::cout << "Exiting collector \n";
  
}

uint64_t globalCount = 0;
//golbalStatFunc - increment global variables
void golbalStatFunc (int threadid, int id) {
  
  int64_t count = 0; 
  while (!ready) {} // wait for the ready signal
  //lprint("%d Thread: %d started Race \n" ,pthread_self(), threadid);
  while(ready) //increment stats until it is ready 
  {
    //std::cout <<threadid<<" ready : "<<ready<<std::endl;
    int64_t start = id*100;
    int64_t end = start+100;
    for(;start<end;++start)
    {
          ThreadStatsMapContainer::i_increment(start, 1);
          ++count;
    }
    //std::this_thread::sleep_for(std::chrono::seconds(1));
  }
  lprint("%d Thread: %d executed INC : %d  times \n" ,pthread_self(), threadid, count);
   static  std::mutex _gmtx;
   std::lock_guard<std::mutex> lck (_gmtx);
   globalCount += count;
};
 
//to test performance set macro LOCAL_ATOMIC to 0 for global stats  and 1  for localized stats
int main ()
{
  //lprint("pawning global threads \n" ,);
  std::vector<std::thread> globalStatsThreads;
  //for (int i=1; i<=2; ++i) globalStatsThreads.push_back(std::thread(golbalStatFunc,i, i%2));
  for (int i=0; i<4; ++i) globalStatsThreads.push_back(std::thread(golbalStatFunc,i, i));//last param is statid

  //start collector thread
  std::thread collector (collectstats);

  std::this_thread::sleep_for(std::chrono::seconds(1));
  auto start = std::chrono::high_resolution_clock::now();
  ready = true; //lets start the race

  //wait for all gloabl threads
  for (auto& th : globalStatsThreads) { th.join(); } 
  //std::cout << "completed global join  \n";

  //wait for all collection threads

  auto end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double, std::milli> elapsed = end-start;
  std::cout << "Waited " << elapsed.count() << " ms\n";
  
  //its the time for display data 
  dumpCollectedStats = true;

  //now wait for collector thread
  collector.join(); 
  std::cout<<globalCount<<" number of stats incremented in "<<elapsed.count() << " ms\n";
  //std::cout << "completed collector join  \n";
}

