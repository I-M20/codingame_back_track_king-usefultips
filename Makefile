CXX  ?= g++
STD  := -std=c++17
OPT  := -O2

DUMPS ?= tools/dumps
PORT  ?= 8000

TOOL   := tools/btk-debug
TURNS  ?= 100

.PHONY: all play debug replay check viewer clean

all: play

# The competition binary: main.cpp on its own, exactly as it goes to CodinGame.
play:
	$(CXX) $(STD) $(OPT) main.cpp -o a.out

debug: $(TOOL)

# The debug harness. It includes main.cpp, so the engine it drives is the real
# one, and it keeps main.cpp's own time budget, so the numbers it reports are
# the numbers the bot will actually play on. Raising the budget would show a
# search that CodinGame never runs.
#
# A file rule, not a phony one, so it rebuilds only when a source it is built
# from actually changed -- `make replay` can then depend on it for free.
$(TOOL): tools/debug_tool.cpp main.cpp
	@mkdir -p $(DUMPS)
	$(CXX) $(STD) $(OPT) tools/debug_tool.cpp -o $@

# Replay a logged game into tools/dumps, building the tool first if needed:
#
#   make replay LOG=.colosseum/logs/.../game_..._p0.events.jsonl
#   make replay LOG=<path> TURNS=30
#
# TURNS is capped by the game's length, so the default plays out a whole game.
replay: $(TOOL)
ifndef LOG
	$(error pass the log to replay: make replay LOG=path/to/game_..._p0.events.jsonl)
endif
	@test -f "$(LOG)" || { echo "no such file: $(LOG)"; exit 1; }
	@# Old turns are cleared first: a shorter game would otherwise leave the
	@# tail of the previous one behind, and the viewer would list those turns
	@# as if they belonged to this game.
	@rm -f $(DUMPS)/turn_*.json
	./$(TOOL) replay "$(LOG)" $(DUMPS) $(TURNS)

# The guard on the one hard constraint: main.cpp must always compile alone.
check:
	$(CXX) $(STD) $(OPT) -fsyntax-only main.cpp
	@echo "main.cpp compiles standalone"

viewer:
	python3 tools/serve.py --dumps $(DUMPS) --port $(PORT)

clean:
	rm -f a.out tools/btk-debug
