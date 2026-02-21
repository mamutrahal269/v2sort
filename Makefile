CPP := g++
SRC_DIR := ./src
INC_DIR := ./include

CPPFLAGS := -Isubmodules/tomlplusplus/include/ -Isubmodules/CLI11/include/ -I$(INC_DIR) -DBOOST_LOG_DYN_LINK -flto=auto -std=gnu++20 -pipe -O3
CPPFLAGS_DEBUG := -Isubmodules/tomlplusplus/include/ -Isubmodules/CLI11/include/ -I$(INC_DIR) -std=gnu++20 -O0 -g -DDEBUG -DBOOST_LOG_DYN_LINK -Wall -Wextra -pipe
LIBS := -lmaxminddb -lboost_json -lboost_url -lcurl -lpthread -lboost_thread -lboost_log -lboost_filesystem 
OBJS := geodata.o utils.o mkproto.o configgen.o main.o net.o

.PHONY: all clean debug fmt

all: v2sort

%.o : $(SRC_DIR)/%.cpp
	$(CPP) $(CPPFLAGS) -c $< -o $@
v2sort: $(OBJS)
	$(CPP) $(CPPFLAGS) $^ -o $@ -Wl,--start-group $(LIBS) -Wl,--end-group
debug: CPPFLAGS := $(CPPFLAGS_DEBUG)
debug: clean v2sort
fmt:
	find include src -type f \( -name '*.cpp' -o -name '*.h' -o -name '*.hpp' \) -print0 | xargs -0 clang-format -i
clean:
	rm -f v2sort xray.log *.o
