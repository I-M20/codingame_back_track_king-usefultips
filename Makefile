CXX  ?= g++
STD  := -std=c++17
OPT  := -O2

DUMPS ?= tools/dumps
PORT  ?= 8000

.PHONY: all play debug check viewer clean

all: play

# The competition binary: main.cpp on its own, exactly as it goes to CodinGame.
play:
	$(CXX) $(STD) $(OPT) main.cpp -o a.out

# The debug harness. It includes main.cpp, so the engine it drives is the real
# one, and it keeps main.cpp's own time budget, so the numbers it reports are
# the numbers the bot will actually play on. Raising the budget would show a
# search that CodinGame never runs.
debug:
	@mkdir -p $(DUMPS)
	$(CXX) $(STD) $(OPT) tools/debug_tool.cpp -o tools/btk-debug

# The guard on the one hard constraint: main.cpp must always compile alone.
check:
	$(CXX) $(STD) $(OPT) -fsyntax-only main.cpp
	@echo "main.cpp compiles standalone"

viewer:
	python3 tools/serve.py --dumps $(DUMPS) --port $(PORT)

clean:
	rm -f a.out tools/btk-debug
