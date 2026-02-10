CPP := g++
SRC_DIR := ./src
INC_DIR := ./include

CPPFLAGS := -I$(INC_DIR) -DBOOST_LOG_DYN_LINK -march=native -mtune=native -flto=auto -std=gnu++20 -O3
CPPFLAGS_DEBUG := -I$(INC_DIR) -std=gnu++20 -O0 -g -DDEBUG -DBOOST_LOG_DYN_LINK
LIBS := -lboost_locale -lboost_json -lboost_url -lcurl -lboost_system -lboost_filesystem -lpthread -lboost_date_time -lboost_thread -lboost_regex -lboost_log_setup -lboost_log -lboost_locale
OBJS := utils.o mkproto.o configgen.o main.o net.o

.PHONY: all clean denug

all: v2sort

%.o : $(SRC_DIR)/%.cpp
	$(CPP) $(CPPFLAGS) -c $< -o $@

v2sort: $(OBJS)
	$(CPP) $(CPPFLAGS) $^ -o $@ -Wl,--start-group $(LIBS) -Wl,--end-group
debug: CPPFLAGS := $(CPPFLAGS_DEBUG)
debug: clean v2sort
clean:
	rm -f v2sort *.o
