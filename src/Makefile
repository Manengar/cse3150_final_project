# BGP Simulator - Standalone C++ Makefile
# Builds: ./bgp_simulator

CXX = g++
CXXFLAGS = -std=c++17 -O3 -g0 -Wall -Wextra -pthread
TARGET = bgp_simulator
SOURCES = main.cpp bgp_simulator.cpp
OBJECTS = $(SOURCES:.cpp=.o)

# Default target
all: $(TARGET)

# Build executable
$(TARGET): $(OBJECTS)
	@echo "Linking $(TARGET)..."
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(OBJECTS)
	@echo "Build complete: ./$(TARGET)"

# Compile source files
%.o: %.cpp bgp_simulator.h
	@echo "Compiling $<..."
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Clean build artifacts
clean:
	@echo "Cleaning..."
	rm -f $(TARGET) $(OBJECTS) ribs.csv
	@echo "Clean complete"

# Rebuild from scratch
rebuild: clean all

# Help
help:
	@echo "BGP Simulator Makefile"
	@echo ""
	@echo "Targets:"
	@echo "  make          - Build bgp_simulator"
	@echo "  make clean    - Remove build artifacts"
	@echo "  make rebuild  - Clean and rebuild"
	@echo "  make help     - Show this help"
	@echo ""
	@echo "Usage:"
	@echo "  ./bgp_simulator --relationships topology.txt \\"
	@echo "                  --announcements anns.csv \\"
	@echo "                  --rov-asns rov_asns.csv"

.PHONY: all clean rebuild help
