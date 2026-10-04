CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic -Iinclude -pthread
BIN      := build
HDRS     := $(wildcard include/smeter/*.hpp)

all: $(BIN)/smeterd $(BIN)/smeter-cli $(BIN)/pulse-injector

$(BIN)/%: src/%.cpp $(HDRS) | $(BIN)
	$(CXX) $(CXXFLAGS) $< -o $@
$(BIN)/smeter-cli: src/smeter_cli.cpp $(HDRS) | $(BIN)
	$(CXX) $(CXXFLAGS) $< -o $@
$(BIN)/pulse-injector: src/pulse_injector.cpp | $(BIN)
	$(CXX) $(CXXFLAGS) $< -o $@
$(BIN)/smeterd: src/smeterd.cpp $(HDRS) | $(BIN)
	$(CXX) $(CXXFLAGS) $< -o $@
$(BIN)/tests: tests/test_main.cpp $(HDRS) | $(BIN)
	$(CXX) $(CXXFLAGS) -fsanitize=address,undefined -g $< -o $@
$(BIN):
	mkdir -p $(BIN)

$(BIN)/tests_agent: tests/test_agent.cpp $(HDRS) | $(BIN)
	$(CXX) $(CXXFLAGS) -fsanitize=address,undefined -g $< -o $@

test: $(BIN)/tests $(BIN)/tests_agent
	$(BIN)/tests
	$(BIN)/tests_agent
system-test: all
	bash scripts/system_test.sh
tsan: tests/test_main.cpp $(HDRS) | $(BIN)
	$(CXX) $(CXXFLAGS) -fsanitize=thread -g $< -o $(BIN)/tests_tsan && $(BIN)/tests_tsan
clean:
	rm -rf $(BIN) data
.PHONY: all test system-test tsan clean
