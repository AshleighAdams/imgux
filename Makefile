include config

CFLAGS += -g
CFLAGS += -Werror --std=c++11 # -O3 
#LFLAGS += -shared
#LIBS   += -lutil

PKG_CONFIG += opencv

CFLAGS += `pkg-config $(PKG_CONFIG) --cflags`
LIBS   += `pkg-config $(PKG_CONFIG) --libs`

SOURCES = src/*.cpp src/*.hpp
OBJECTS = $(SOURCES:.cpp=.o)

all: libimgux.so videosource showframe opticalflow

libimgux.o: src/imgux.hpp src/imgux.cpp
	$(CXX) $(CFLAGS) -o $@ -c -fPIC src/imgux.cpp
libimgux.so: libimgux.o
	$(CXX) $(CFLAGS) -o $@ -shared $<

videosource: libimgux.so src/videosource.cpp
	$(CXX) $(CFLAGS) -o $@ src/$@.cpp $(LIBS) -limgux -I./src/ -L./

showframe: libimgux.so src/showframe.cpp
	$(CXX) $(CFLAGS) -o $@ src/$@.cpp $(LIBS) -limgux -I./src/ -L./

opticalflow: libimgux.so src/opticalflow.cpp
	$(CXX) $(CFLAGS) -o $@ src/$@.cpp $(LIBS) -limgux -I./src/ -L./

clean:
	$(RM) *.o

