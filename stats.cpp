//compile g++ stats.cpp -std=c++11 -pthread -DLOCAL_ATOMIC -o fast

// atomic::exchange example
#include <iostream>       // std::cout
#include <atomic>         // std::atomic
#include <thread>         // std::thread
#include <vector>         // std::vector
#include <unordered_set>
#include <mutex>
#include <map>
#include <chrono>


/* define  LOCAL_ATOMIC to 1 if you want to test distributed local stats */
//#define LOCAL_ATOMIC 1

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

/* all the stats counters must implement from BaseCounter */
class BaseCounter: public NCA
{
  protected:
     BaseCounter( uint32_t statId, StatType type, bool addToStats)
      : _value(0), _statId(statId), _type(type)
        ,_addToStatsContainer(addToStats) {}
  public:
     BaseCounter( uint32_t statId, StatType type)
      : _value(0), _statId(statId), _type(type)
        ,_addToStatsContainer(true) {}
     virtual ~BaseCounter(){}; 
     void inc(uint64_t val = 1) { _value+= val ;}
     void dec(uint64_t val = 1) { _value-= val ;}
     uint64_t reset() { return _value.exchange(0); }
     uint64_t getStatValue() { return _value.load(std::memory_order_relaxed); }
     uint32_t getStatId() { return _statId; }
     StatType getStatType() { return _type; }
     //duplicate and reset values, should not use by user
     BaseCounter* duplicate()
     { 
           BaseCounter* tmp = createObj(); 
           copyAndResetStats(*tmp);
           return tmp; 
     }
     bool addStatsObject() { return _addToStatsContainer;}
   protected:
     //all Derived class must call this function 
     virtual BaseCounter* createObj() = 0;
     void copyAndResetStats(BaseCounter& tmp)  
     { 
       tmp.inc(reset());
     }
   protected:
     std::atomic<uint64_t> _value;
     uint32_t _statId;
     StatType _type;
     bool _addToStatsContainer;
};

/* StatsMap is a singleton globals  stats structure used by collector to collect and local variables to register */
class StatsMap : public Singleton
{
   public:
      StatsMap(){_statsValue.reserve(1000);}
      __attribute__((noinline)) void add(BaseCounter* stat) 
      { 
        if(false == stat->addStatsObject()) return;
        //std::cout<<"adding***"<<std::endl;
        std::lock_guard<std::mutex> lck (_mtx); _statsPtr.insert(stat); 
      }
      __attribute__((noinline)) void del(BaseCounter* stat) 
      {
        if(false == stat->addStatsObject()) return;
        //std::cout<<"deleting***"<<std::endl;
        BaseCounter* base = stat->duplicate(); 
        std::lock_guard<std::mutex> lck (_mtx);  
        _statsValue.push_back(base); 
        _statsPtr.erase(stat); 
      }
      __attribute__((noinline)) std::vector<BaseCounter*> fetch()
      {
        std::vector<BaseCounter*>  tmp;
        tmp.reserve(_statsValue.size() + _statsPtr.size() + 10);
        std::lock_guard<std::mutex> lck (_mtx);
        //std::cout<<"size deleted obj: "<< _statsValue.size() << " total stats: "<<_statsPtr.size()<<std::endl;
        for(auto& deleted : _statsValue)
           tmp.push_back(deleted);
           _statsValue.clear(); 
        for(auto val : _statsPtr)
           tmp.push_back(val->duplicate());
        return tmp; 
      }
      void clear()
      {
         //std::cout<<"clearing all stats";
         for(auto it = std::begin(_statsValue); it != std::end(_statsValue); ++it)
         {
           delete *it;
         }
         _statsValue.clear(); 
         //std::cout<<"done clearing all stats";
      }
      static StatsMap& getInstance()
      {
            static StatsMap    instance; 
            return instance;
      }
      ~StatsMap() 
      {
         clear();
      }

    protected:
      std::vector<BaseCounter*> _statsValue;
      std::unordered_set<BaseCounter*> _statsPtr;
      std::mutex _mtx;
};

#define ADD_STATS_OBJ(obj) \
   StatsMap::getInstance().add(obj);

#define DEL_STATS_OBJ(obj) \
   StatsMap::getInstance().del(obj);

#define FETCH_STATS_OBJ() \
   StatsMap::getInstance().fetch();



/* concreate class */
class GlobalStats final: public BaseCounter
{
 
public:
     GlobalStats( uint32_t statId): BaseCounter(statId, StatType::GLOBAL_STATS) 
     {
       ADD_STATS_OBJ(this); //call only from concrete class   
     }
     virtual ~GlobalStats() 
     { 
       DEL_STATS_OBJ(this); //call only from concrete class 
     }
private:     
     GlobalStats( uint32_t statId, bool addToStats): BaseCounter(statId, StatType::GLOBAL_STATS, false) 
     {
     }
     virtual GlobalStats* createObj() 
     { 
           return new GlobalStats(_statId, false); //call only from concrete class 
     }
};

/* concreate class */
class CollectionStats final: public BaseCounter
{
 
public:
     CollectionStats( uint32_t statId, uint32_t collectionId)
      : BaseCounter(statId, StatType::ARRAY_OF_STATS)
      , _collectionId(collectionId)   
     {
       ADD_STATS_OBJ(this); //call only from concrete class   
     }
     virtual ~CollectionStats() 
     { 
       DEL_STATS_OBJ(this); //call only from concrete class 
     } 
private:     
     CollectionStats( uint32_t statId, uint32_t collectionId, bool addToStats)
      : BaseCounter(statId, StatType::ARRAY_OF_STATS, false)
      , _collectionId(collectionId)  
     {} 
     virtual CollectionStats* createObj() 
     { 
           return new CollectionStats(_statId, _collectionId, false); //call only from concrete class 
     }
private:
     uint32_t _collectionId;    
};


//TEST code starts here
//testing
/*
 * all thread waits until it is ready object. stats are incremented once this value is set to true.  
 * collector signals thread to stop incrementing counter by sresetting ready value;
 */   
bool ready = false;
//dumpCollectedStats - collector starts dumping aggregated stats to console 
bool dumpCollectedStats = false;

//following variables are used if LOCAL_ATOMIC is not defined
std::atomic<uint64_t> _global(0);
std::atomic<uint64_t> _globalGroup(0);

//collectstats - start collecting stats and print aggregated values
void collectstats() 
{
   std::vector<BaseCounter*> statsValues;
   statsValues.reserve(300);
   uint32_t maxcount = 15;
   while(!ready) {}
   while(maxcount--)
   {
     /* fetch stats every 1 second */
     std::this_thread::sleep_for(std::chrono::seconds(1));
     std::vector<BaseCounter*> tmp = FETCH_STATS_OBJ();
     statsValues.insert(statsValues.end(), tmp.begin(), tmp.end());
    //std::cout << "size: "<<statsValues.size()<<std::endl;
   }
   //std::cout << "Exiting collector \n";
   ready = false;
   while(!dumpCollectedStats) {} //wait for all threads to finish
   std::vector<BaseCounter*> tmp = FETCH_STATS_OBJ();
   statsValues.insert(statsValues.end(), tmp.begin(), tmp.end());
  
   // std::cout << "size: "<<statsValues.size()<<std::endl;
   std::map<uint32_t, uint64_t> _gStatsData;
   for(auto& it: statsValues)
   {
     uint32_t id = it->getStatId(); 
     uint64_t value = it->getStatValue();
     //std::cout << "id: "<<id <<" value: "<<value<<std::endl;
    //TODO, calculate collection based aggregation for stats type StatType::ARRAY_OF_STATS 
     _gStatsData[id] += value;    
   }
   for(auto& it: _gStatsData) 
     std::cout << "id: "<<it.first <<" value: "<<it.second<<std::endl;
   
     std::cout << "global(0): "<<_global <<" _group(10): "<<_globalGroup<<std::endl;
  
}

//golbalStatFunc - increment global variables
void golbalStatFunc (int threadid, int id) {
#ifdef LOCAL_ATOMIC 
  GlobalStats stats(id); 
#endif
   
  while (!ready) {} // wait for the ready signal
  while(ready) //increment stats until it is ready 
  {    
#ifdef LOCAL_ATOMIC 
     stats.inc();   // go!, and increment
#else
     _global++;
#endif
  }
  //std::cout << "Exiting global stats threadId: "<<threadid<<std::endl;
};
 
//golbalStatFunc - increment group variables
void collectionStatFunc(int threadid,int id) { //collection id
#ifdef LOCAL_ATOMIC 
  CollectionStats stats(10, id); //stats id = 10, collection id is passed 
#endif
  while (!ready) {}
  while(ready)                  // wait for the ready signal
  {    
#ifdef LOCAL_ATOMIC 
     stats.inc();   // go!, and increment
#else
     _globalGroup++;
#endif
  }
  //std::cout << "Exiting collecion stats threadId: "<<threadid<<std::endl;
};

//to test performance set macro LOCAL_ATOMIC to 0 for global stats  and 1  for localized stats
int main ()
{
  std::cout << "spawning global threads \n";
  std::vector<std::thread> globalStatsThreads;
  //for (int i=1; i<=2; ++i) globalStatsThreads.push_back(std::thread(golbalStatFunc,i, i%2));
  for (int i=1; i<=2; ++i) globalStatsThreads.push_back(std::thread(golbalStatFunc,i, 0));//last param is statid

  std::cout << "spawning Collection threads...\n";
  std::vector<std::thread> collectionStatsThreads;
  for (int i=1; i<=2; ++i) collectionStatsThreads.push_back(std::thread(collectionStatFunc,i, i%3)); //last param is collection id for same stat id 10

  //start collector thread
  std::thread collector (collectstats);

  std::this_thread::sleep_for(std::chrono::seconds(1));
  auto start = std::chrono::high_resolution_clock::now();
  ready = true; //lets start the race

  //wait for all gloabl threads
  for (auto& th : globalStatsThreads) { th.join(); } 
  //std::cout << "completed global join  \n";

  //wait for all collection threads
  for (auto& th : collectionStatsThreads){ th.join(); }
  //std::cout << "completed collection join  \n";

  auto end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double, std::milli> elapsed = end-start;
  std::cout << "Waited " << elapsed.count() << " ms\n";
  
  //its the time for display data 
  dumpCollectedStats = true;

  //now wait for collector thread
  collector.join();  
  //std::cout << "completed collector join  \n";
}

