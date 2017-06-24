This is done using Thread local storage. each thread stroes stats in its local map and
an aggregator thread collect all this stats periodically and collect and reset the thread stats.
This stats framework can be used for dynamic stats
304087825(30 Million) number of stats incremented using 4 threads and 15 seconds of stats increment.
