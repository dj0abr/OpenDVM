CXX := g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -pedantic -MMD -MP
LDFLAGS :=
LDLIBS := -lmysqlclient -lmosquitto -lpthread

SRC := main.cpp renderConfigFile.cpp helper.cpp handleDVconfig.cpp Database.cpp MqttListener.cpp fmdatabase.cpp
OBJ := $(SRC:.cpp=.o)
DEP := $(OBJ:.o=.d)

BINDIR := /usr/local/bin
TARGET := $(BINDIR)/DVconfig

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CXX) $(OBJ) -o $@ $(LDFLAGS) $(LDLIBS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(DEP)

-include $(DEP)
