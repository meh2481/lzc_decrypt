objects := main.o LZC.o
libs := -L./lib/Win32 ./lib/Win32/FreeImage.lib -static-libgcc -static-libstdc++
header := -I./ -I./include 
CXX=g++
output=lzc_decrypt

CXXFLAGS += -g -ggdb -DDEBUG

all: lzc_decrypt

lzc_decrypt: $(objects)
	$(CXX) -o $(output) $^ $(libs) $(CXXFLAGS)

%.o: %.cpp
	$(CXX) -c -MMD $(CXXFLAGS) -o  $@ $< $(header)

-include $(objects:.o=.d)

clean:
	rm -f *.o *.d $(output)
