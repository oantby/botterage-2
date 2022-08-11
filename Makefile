src = $(wildcard *.cpp)
src := $(filter-out inventory.cpp commandList.cpp botterage-http.cpp, $(src))
obj = $(src:.cpp=.o)
dep = $(obj:.o=.d)

LDFLAGS=-L /usr/lib64 -L /usr/local/lib -L /usr/lib64/mysql -lsqlite3 \
-lcurl -lmysqlclient -lmysqlpp -lssl -lcrypto -pthread -ldl -Wl,--dynamic-list=dynlist

CXX = g++
CXXFLAGS = -Wall -include include/logging.hpp -std=c++17 -I include -I /usr/include/mysql -I /usr/include \
-I /usr/include/mysql++

.PHONY : all
all : botterage

botterage : $(obj)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

inventory : inventory.cpp include/inventory.hpp include/stringextensions.hpp \
stringextensions.o include/settings.hpp
	g++ -std=c++17 $(INCFLAGS) -L /usr/lib64 -L /usr/local/lib \
	-L /usr/lib64/mysql -lmysqlpp -lmysqlclient inventory.cpp \
	stringextensions.o -o inventory

commandList : commandList.cpp include/DBConnPool.hpp DBConnPool.o \
include/commands.hpp include/botterage-db.hpp
	g++ -std=c++17 $(INCFLAGS) $(LDFLAGS) commandList.cpp DBConnPool.o \
	-o commandList

vars.tsv : $(src)
	echo -e 'Variable\tDefault\tSecret?\tDescription' > vars.tsv
	egrep -hIr --exclude-dir '.git' '^@var [^ ]+[* :]' | \
	perl -ne 'if (m/@var ([^ *]+)( \((.*?)\))?(\*?): *(.*)$$/) {print "$$1\t$$3\t$$4\t$$5\n";}' >> vars.tsv

# add fcgi flags for compilation
botterage-http : LDFLAGS += -lfcgi++ -lfcgi -lcrypto
botterage-http : botterage-http.cpp stringextensions.o utils.o include/DBConnPool.hpp DBConnPool.o
	g++ $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

-include $(dep)

%.d : %.cpp
	@$(CXX) $(CXXFLAGS) $< -MM -MT $(@:.d=.o) >$@

.PHONY : install
install : botterage inventory
	mv botterage /usr/local/bin/botterage

.PHONY : clean
clean :
	-rm -f *.o