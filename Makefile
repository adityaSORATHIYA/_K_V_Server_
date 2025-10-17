CXX = g++
CXXFLAGS = -I./libs/crow/include -O2
TARGET = server
SRC = app.cpp

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRC) -lpthread

clean:
	rm -f $(TARGET)
