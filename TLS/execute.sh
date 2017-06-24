g++ stats.cpp --std=c++11 -lpthread -g
if [ $? -eq 0 ]; then
./a.out
fi
