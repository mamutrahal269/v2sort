CPP := g++
SRC_DIR := ./src
INC_DIR := ./include

CPPFLAGS := -I$(INC_DIR) -march=native -mtune=native -flto=auto -std=gnu++20 -O3
LIBS := -lboost_locale -lboost_json -lboost_url -lcurl -lboost_system
OBJS := mkproto.o configgen.o main.o net.o

.PHONY: all clean

all: v2sort

%.o : $(SRC_DIR)/%.cpp
	$(CPP) $(CPPFLAGS) -c $< -o $@

v2sort: $(OBJS)
	$(CPP) $(CPPFLAGS) $^ -o $@ $(LIBS)

clean:
	rm -f v2sort *.o
