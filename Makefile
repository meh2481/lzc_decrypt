objects := main.o LZC.o
libs := 
header := -I./ 
CXX=g++
output=lzc_decrypt

ifeq ($(BUILD),release)  
# "Release" build - optimization, and no debug symbols
	CXXFLAGS += -O2 -Os -s -DNDEBUG 
else
# "Debug" build - no optimization, and debugging symbols
	CXXFLAGS += -g -ggdb -DDEBUG -pg
endif

all: lzc_decrypt

lzc_decrypt: $(objects)
	$(CXX) -o $(output) $^ $(libs) $(CXXFLAGS) -m64

%.o: %.cpp
	$(CXX) -c -MMD $(CXXFLAGS) -o  $@ $< $(header) -m64

-include $(objects:.o=.d)

clean:
	rm -f *.o *.d $(output)
