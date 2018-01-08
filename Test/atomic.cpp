#include <thread> 
#include <vector>
#include <time.h>
#include <chrono>

using namespace std;
std::atomic<bool> ready (false);
std::atomic<int> counter(0);
//int counter(0);
//int counter = 0;

//increment local

constexpr int max_value = 100000000;

void increment_l (int id) {
        unsigned int lcounter = 0;
        //std::cout << "In Thread Id #" << id << std::endl;
        while (!ready) { 
                std::this_thread::yield(); 
        }// wait for the ready signal
        
        // Increment the atomic variable and go to a nano sleep
        for(volatile int i=0; i<max_value; ++i){
            ++lcounter;
        }
        counter += lcounter;
}

//increment atomic
void increment_a (int id) {

        //std::cout << "In Thread Id #" << id << std::endl;
        while (!ready) { 
                std::this_thread::yield(); 
        }// wait for the ready signal
        
        // Increment the atomic variable and go to a nano sleep
        for(volatile int i=0; i<max_value; ++i){
            ++counter;
        }
}

//increment thread local
void increment_tl (int id) {

        static thread_local unsigned int tlcounter;
        //std::cout << "In Thread Id #" << id << std::endl;
        while (!ready) { 
                std::this_thread::yield(); 
        }// wait for the ready signal
        
        // Increment the atomic variable and go to a nano sleep
        for(volatile int i=0; i<max_value; ++i){
            ++tlcounter;
            //nanosleep(&timeOut, &remains);
        }
        counter += tlcounter;
}

int func (void fptr(int))
{
        ready = false;
        counter = 0;
        std::vector<std::thread> threads;
        //std::cout << "spawning 10 threads...\n";
        auto start = std::chrono::high_resolution_clock::now();
        for (int i=1; i<=10; ++i) threads.push_back(std::thread((*fptr),i));
        ready = true;
        for (auto& th : threads) th.join();

        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> elapsed = end-start;
        std::cout << "Waited " << elapsed.count() << " ms\n";

        std::cout << "The value of counter is " << counter << std::endl;
        return 0;
}

int main ()
{
   std::cout << "Local =========== " ;
   func(increment_l);
   std::cout << "Thread Local =========== " ;
   func(increment_tl);
   std::cout << "Atomic =========== " ;
   func(increment_a);
  
   return 0;
}
