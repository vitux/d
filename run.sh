g++ --std=c++11 HelloFastCGI.cpp -I/usr/local/include/mongocxx/v_noabi  -I/usr/local/include/bsoncxx/v_noabi  -L/usr/local/lib -lmongocxx -lbsoncxx -O2 -fPIC -lfastcgi-daemon2 -shared -o libHelloFastCGI.so -pthread


fastcgi-daemon2 --config=HelloFastCGI.conf
