CXX      := g++
STD      := -std=c++17
WARN     := -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor \
            -Woverloaded-virtual -Wnull-dereference -Wdouble-promotion
OPT      := -O3 -march=native -funroll-loops
DBG      := -O0 -g3 -fno-omit-frame-pointer
ASAN     := -fsanitize=address,undefined -fno-sanitize-recover=all

SRCDIR   := src
TESTDIR  := tests
BUILD    := build

SRCS     := $(SRCDIR)/OrderBook.cpp $(SRCDIR)/Logger.cpp
MAIN_SRC := $(SRCDIR)/main.cpp
TEST_SRCS:= $(TESTDIR)/test_main.cpp \
            $(TESTDIR)/test_spsc.cpp  \
            $(TESTDIR)/test_slab.cpp  \
            $(TESTDIR)/test_matching.cpp \
            $(TESTDIR)/test_integration.cpp

INCLUDES := -I$(SRCDIR) -I$(TESTDIR)

ENGINE   := $(BUILD)/engine
TESTS    := $(BUILD)/tests
TESTS_ASAN := $(BUILD)/tests_asan

.PHONY: all release debug asan test test_asan clean run run_verbose

all: release test

release: $(ENGINE)
debug:   $(BUILD)/engine_dbg
asan:    $(TESTS_ASAN)

$(BUILD):
	mkdir -p $(BUILD)

$(ENGINE): $(SRCS) $(MAIN_SRC) | $(BUILD)
	$(CXX) $(STD) $(WARN) $(OPT) -pthread $(INCLUDES) $^ -o $@

$(BUILD)/engine_dbg: $(SRCS) $(MAIN_SRC) | $(BUILD)
	$(CXX) $(STD) $(WARN) $(DBG) -pthread $(INCLUDES) $^ -o $@

$(TESTS): $(SRCS) $(TEST_SRCS) | $(BUILD)
	$(CXX) $(STD) $(WARN) $(OPT) -pthread $(INCLUDES) $^ -o $@

$(TESTS_ASAN): $(SRCS) $(TEST_SRCS) | $(BUILD)
	$(CXX) $(STD) $(WARN) $(DBG) $(ASAN) -pthread $(INCLUDES) $^ -o $@

test: $(TESTS)
	./$(TESTS)

test_asan: $(TESTS_ASAN)
	./$(TESTS_ASAN)

run: $(ENGINE)
	./$(ENGINE) --depth --trades --max-trades=20

run_verbose: $(ENGINE)
	./$(ENGINE) --log --depth --trades --max-trades=50

clean:
	rm -rf $(BUILD) trading_engine.log
