// Debug harness for the Back Track King bot.
//
// It compiles the real engine -- main.cpp is included verbatim, DEBUG_TOOL
// suppressing only its main() -- and watches it through the DBG_* hooks, so
// what the viewer shows is what the bot actually computed, never a
// reimplementation that could drift from it.
//
//   btk-debug live   <outdir>                    play a match, dump every turn
//   btk-debug replay <events.jsonl> <outdir> [n]  replay a logged game
//
// In live mode the tool is a drop-in bot: it reads stdin and writes its move
// to stdout exactly as main.cpp would, and drops <outdir>/turn_NNN.json on the
// side. In replay mode it feeds back the stdin recorded by cg-colosseum, which
// needs no referee and is reproducible.

#define DEBUG_TOOL
#include "../main.cpp"

#include <fstream>
#include <iomanip>
#include <sys/stat.h>

// ====================
// CAPTURE

// One candidate cell as the intra-turn beam scored it: `gap` is the
// resultingGap extendManhattanGap() gave the line ending on this cell.
struct CandidateRecord
{
    int x, y;
    int cost;
    int gap;
    int owner;
    int prefixLen;
};

// Everything one turn of the search revealed about itself. Filled by the DBG_*
// hooks, flushed to JSON once the move is out.
class DebugProbe
{
public:
    int turn = 0;
    bool haveBoard = false;
    Map board;
    vector<pair<int, int>> wishes;
    vector<CandidateRecord> candidates;
    int baselineGap = 0;
    bool haveBaseline = false;
    ActionSet decision;
    int disrupt = -1;
    int myScore = 0, foeScore = 0;
    int myId = 0;
    // Wishes already connected this turn, so the viewer can tell a link that
    // still has to be built from one that is already paying out.
    vector<pair<int, int>> active;

    // Which planning pass the candidates being reported belong to. The outer
    // beam calls generateActionSets() once per node per depth -- on a busy
    // board that is thousands of passes and over a million candidate records,
    // all but the first two describing hypothetical futures rather than this
    // turn. Only the root node's two passes run on the real board: the
    // opponent is planned first, then we are, so passes 0 and 1 are the ones
    // worth capturing and everything after them is discarded.
    int passIndex = -1;
    bool capturing = false;
    static const int ROOT_PASSES = 2;
    // (owner, cell) already recorded this turn.
    unordered_set<long long> seenCandidate;

    void beginTurn(const Map &b, const vector<pair<int, int>> &w)
    {
        board = b;
        wishes = w;
        haveBoard = true;
        candidates.clear();
        seenCandidate.clear();
        haveBaseline = false;
        baselineGap = 0;
        decision = ActionSet();
        disrupt = -1;
        passIndex = -1;
        capturing = false;
    }

    // Called once at the top of every planning pass, which is what delimits
    // them. Both root passes share the same board, hence the same baseline:
    // it is the reference the viewer subtracts candidate gaps from.
    void setBaseline(const vector<int> &baseline)
    {
        passIndex++;
        capturing = (passIndex < ROOT_PASSES);
        if (!capturing || haveBaseline)
            return;
        int total = 0;
        for (int v : baseline)
            total += v;
        baselineGap = total;
        haveBaseline = true;
    }

    // Only the first rail of a turn is kept: one number per cell of the
    // position actually on the board. A deeper candidate scores a cell given
    // rails that have not been placed, so its number belongs to a hypothetical
    // board and would put several conflicting values on the same cell.
    //
    // The same cell is still offered once per owner, and the round that opens
    // a pass re-offers cells the previous one already scored, so the record is
    // keyed by (cell, owner) and written once.
    void addCandidate(const CandidateRecord &rec)
    {
        if (!capturing || rec.prefixLen != 0)
            return;
        const long long key =
            ((long long)rec.owner << 32) | (unsigned)(rec.y * 1024 + rec.x);
        if (!seenCandidate.insert(key).second)
            return;
        candidates.push_back(rec);
    }

    void endTurn(const ActionSet &action, int d)
    {
        decision = action;
        disrupt = d;
    }

    void write(const string &path) const;
};

static DebugProbe *g_probe = nullptr;

// ---- hook implementations, declared in main.cpp ----

void dbgTurnBegin(const Map &board, const vector<pair<int, int>> &wishes)
{
    if (g_probe)
        g_probe->beginTurn(board, wishes);
}

void dbgBaseline(const vector<int> &baseline)
{
    if (g_probe)
        g_probe->setBaseline(baseline);
}

void dbgCandidate(int owner, int prefixLen, const Coord &cand, int cost,
                  int gap)
{
    if (g_probe)
        g_probe->addCandidate(
            CandidateRecord{cand.x, cand.y, cost, gap, owner, prefixLen});
}

void dbgTurnEnd(const ActionSet &action, int disrupt)
{
    if (g_probe)
        g_probe->endTurn(action, disrupt);
}

// ====================
// SERIALISATION

// Hand-rolled so the tool stays a single translation unit with no dependency
// beyond the standard library, the way main.cpp is.

static void writeIntGrid(ostream &os, const char *name, const Map &board,
                         int (*cell)(const Map &, int, int))
{
    os << "  \"" << name << "\": [";
    for (int y = 0; y < board.height(); y++)
    {
        os << (y ? ",\n    [" : "\n    [");
        for (int x = 0; x < board.width(); x++)
        {
            if (x)
                os << ",";
            os << cell(board, x, y);
        }
        os << "]";
    }
    os << "\n  ],\n";
}

static int cellTerrain(const Map &b, int x, int y) { return b.tileType(x, y); }
static int cellRegion(const Map &b, int x, int y) { return b.tileRegion(x, y); }
static int cellRail(const Map &b, int x, int y) { return b.tileOwner(x, y); }
static int cellInked(const Map &b, int x, int y)
{
    return b.isInked(x, y) ? 1 : 0;
}

void DebugProbe::write(const string &path) const
{
    ofstream os(path);
    if (!os)
    {
        fprintf(stderr, "debug_tool: cannot write %s\n", path.c_str());
        return;
    }

    const int W = board.width(), H = board.height();

    os << "{\n";
    os << "  \"turn\": " << turn << ",\n";
    os << "  \"myId\": " << myId << ",\n";
    os << "  \"width\": " << W << ",\n";
    os << "  \"height\": " << H << ",\n";
    os << "  \"scores\": {\"me\": " << myScore << ", \"foe\": " << foeScore
       << "},\n";

    writeIntGrid(os, "terrain", board, cellTerrain);
    writeIntGrid(os, "regions", board, cellRegion);
    writeIntGrid(os, "rails", board, cellRail);
    writeIntGrid(os, "inked", board, cellInked);

    // Towns, with the connections each one wishes for.
    os << "  \"towns\": [";
    for (size_t i = 0; i < board.towns.size(); i++)
    {
        const Town &t = board.towns[i];
        os << (i ? ",\n    " : "\n    ");
        os << "{\"id\": " << t.id << ", \"x\": " << t.coord.x
           << ", \"y\": " << t.coord.y << ", \"wishes\": [";
        for (size_t k = 0; k < t.desiredConnections.size(); k++)
            os << (k ? "," : "") << t.desiredConnections[k];
        os << "]}";
    }
    os << "\n  ],\n";

    // Regions, sorted by id so the viewer can index them directly.
    vector<int> ids;
    ids.reserve(board.regionById.size());
    for (const auto &kv : board.regionById)
        ids.push_back(kv.first);
    sort(ids.begin(), ids.end());

    os << "  \"regionInfo\": [";
    for (size_t i = 0; i < ids.size(); i++)
    {
        const Region &r = board.regionById.at(ids[i]);
        os << (i ? ",\n    " : "\n    ");
        os << "{\"id\": " << r.id << ", \"instability\": " << r.instability
           << ", \"inked\": " << (r.inked ? "true" : "false")
           << ", \"hasTown\": " << (r.hasTown ? "true" : "false")
           << ", \"cells\": " << r.coords.size() << "}";
    }
    os << "\n  ],\n";

    os << "  \"wishes\": [";
    for (size_t i = 0; i < wishes.size(); i++)
        os << (i ? "," : "") << "[" << wishes[i].first << ","
           << wishes[i].second << "]";
    os << "],\n";

    os << "  \"active\": [";
    for (size_t i = 0; i < active.size(); i++)
        os << (i ? "," : "") << "[" << active[i].first << ","
           << active[i].second << "]";
    os << "],\n";

    os << "  \"baselineGap\": " << baselineGap << ",\n";
    os << "  \"inkThreshold\": " << INK_INSTABILITY_THRESHOLD << ",\n";

    os << "  \"candidates\": [";
    for (size_t i = 0; i < candidates.size(); i++)
    {
        const CandidateRecord &c = candidates[i];
        os << (i ? ",\n    " : "\n    ");
        os << "{\"x\": " << c.x << ", \"y\": " << c.y << ", \"gap\": " << c.gap
           << ", \"cost\": " << c.cost << ", \"owner\": " << c.owner
           << ", \"prefixLen\": " << c.prefixLen << "}";
    }
    os << "\n  ],\n";

    os << "  \"decision\": {\"cells\": [";
    for (size_t i = 0; i < decision.cells.size(); i++)
        os << (i ? ", " : "") << "{\"x\": " << decision.cells[i].x
           << ", \"y\": " << decision.cells[i].y << "}";
    os << "], \"disrupt\": " << disrupt << "}\n";
    os << "}\n";
}

// ====================
// DRIVING THE ENGINE

// Created up front rather than failing a turn at a time: a missing output
// directory is a typo or a cleaned tree, not a reason to lose the run.
static void ensureDir(const string &dir)
{
    struct stat st;
    if (stat(dir.c_str(), &st) == 0)
        return;
    if (mkdir(dir.c_str(), 0755) != 0)
        fprintf(stderr, "debug_tool: cannot create %s\n", dir.c_str());
}

static string turnPath(const string &dir, int turn)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "/turn_%03d.json", turn);
    return dir + buf;
}

// One turn of the real bot, with the probe watching. Game::gameTurn() writes
// the move to stdout itself, which is what makes live mode a usable bot.
static void runTurn(Game &game, DebugProbe &probe, const string &outdir,
                    int turn)
{
    probe.turn = turn;
    game.gameTurn();
    probe.myId = game.myId;
    probe.myScore = game.myScore;
    probe.foeScore = game.foeScore;

    // The referee reports a live connection from both of its towns; the pair
    // is canonicalised the way BeamSearch::setup() does so the viewer can
    // match it against a wish.
    probe.active.clear();
    for (const auto &kv : game.activeConnections)
    {
        if (!kv.second)
            continue;
        const int a = min(kv.first.first, kv.first.second);
        const int b = max(kv.first.first, kv.first.second);
        if (find(probe.active.begin(), probe.active.end(), make_pair(a, b)) ==
            probe.active.end())
            probe.active.push_back({a, b});
    }

    if (probe.haveBoard)
        probe.write(turnPath(outdir, turn));
}

static int runLive(const string &outdir)
{
    ensureDir(outdir);

    DebugProbe probe;
    g_probe = &probe;

    Game game;
    game.init();
    for (int turn = 1;; turn++)
    {
        game.parse();
        if (!cin)
            break;
        runTurn(game, probe, outdir, turn);
    }
    return 0;
}

// ---- replay ----

// Minimal reader for the "data" string of a cg-colosseum event line. The file
// is one JSON object per line with just {"type": ..., "data": ...}, so a full
// parser would be overkill -- but the escapes have to be honoured, since the
// payload is newline-separated game input.
static bool parseEventLine(const string &line, string &type, string &data)
{
    auto field = [&](const char *key, string &out) -> bool {
        const string pat = string("\"") + key + "\":";
        size_t p = line.find(pat);
        if (p == string::npos)
            return false;
        p = line.find('"', p + pat.size());
        if (p == string::npos)
            return false;
        out.clear();
        for (size_t i = p + 1; i < line.size(); i++)
        {
            const char c = line[i];
            if (c == '"')
                return true;
            if (c != '\\')
            {
                out.push_back(c);
                continue;
            }
            if (++i >= line.size())
                return false;
            switch (line[i])
            {
            case 'n': out.push_back('\n'); break;
            case 't': out.push_back('\t'); break;
            case 'r': out.push_back('\r'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case 'u':
            {
                // Game input is ASCII; decode the code point and keep the low
                // byte rather than dragging in a UTF-8 encoder.
                if (i + 4 >= line.size())
                    return false;
                out.push_back((char)stoi(line.substr(i + 1, 4), nullptr, 16));
                i += 4;
                break;
            }
            default: out.push_back(line[i]); break;
            }
        }
        return false;
    };

    return field("type", type) && field("data", data);
}

static int runReplay(const string &eventsPath, const string &outdir,
                     int wantTurn)
{
    ensureDir(outdir);

    ifstream f(eventsPath);
    if (!f)
    {
        fprintf(stderr, "debug_tool: cannot open %s\n", eventsPath.c_str());
        return 1;
    }

    // The recorded stdin, in order. The first event carries the init block and
    // the first turn's state together; every later one is a single turn.
    vector<string> inputs;
    vector<string> outputs;
    string line;
    while (getline(f, line))
    {
        if (line.empty())
            continue;
        string type, data;
        if (!parseEventLine(line, type, data))
            continue;
        if (type == "in")
            inputs.push_back(data);
        else if (type == "out")
            outputs.push_back(data);
    }

    if (inputs.empty())
    {
        fprintf(stderr, "debug_tool: no input events in %s\n",
                eventsPath.c_str());
        return 1;
    }

    fprintf(stderr, "debug_tool: search budget %d ms/turn (%d on the first)%s\n",
            TURN_BUDGET_MS, FIRST_TURN_BUDGET_MS,
            TURN_BUDGET_MS == 30 ? ", matching the referee"
                                 : ", NOT the referee's 30 ms");

    // Asking for more turns than the game holds means "all of it", not a
    // mistake: a caller replaying a batch of games cannot know each length.
    const int lastTurn = (int)inputs.size();
    if (wantTurn > lastTurn)
        fprintf(stderr, "debug_tool: game is %d turns, %d asked -- replaying "
                        "all of it\n",
                lastTurn, wantTurn);
    const int stopTurn = wantTurn > 0 ? min(wantTurn, lastTurn) : lastTurn;

    // Game reads cin directly, so the recording is handed to it as cin rather
    // than threading a stream through the engine's signatures.
    string all;
    for (int i = 0; i < stopTurn; i++)
        all += inputs[i];
    istringstream feed(all);
    streambuf *const savedIn = cin.rdbuf(feed.rdbuf());

    // The move the bot prints on replay is noise on stdout; it is compared
    // against the recording below instead.
    ostringstream sink;
    streambuf *const savedOut = cout.rdbuf(sink.rdbuf());

    DebugProbe probe;
    g_probe = &probe;

    Game game;
    game.init();
    for (int turn = 1; turn <= stopTurn; turn++)
    {
        game.parse();
        runTurn(game, probe, outdir, turn);
    }

    cin.rdbuf(savedIn);
    cout.rdbuf(savedOut);

    // What the logged bot played, next to what this build plays on the same
    // input. A difference is information, not a failure: the log may come from
    // an older version, the search is bounded by the wall clock and so follows
    // the machine's speed, and replaying at a larger budget explores further.
    // Only a game logged by this very binary on this very machine would match
    // outright. The comparison is on the set of cells rather than the printed
    // line -- the beam orders a turn's rails by when it chose them, which
    // carries no meaning for the referee, and calling that a difference would
    // cry wolf.
    if (stopTurn <= (int)outputs.size())
    {
        istringstream replayed(sink.str());
        string line, lastLine, logged = outputs[stopTurn - 1];
        while (getline(replayed, line))
            if (!line.empty())
                lastLine = line;
        while (!logged.empty() &&
               (logged.back() == '\n' || logged.back() == '\r'))
            logged.pop_back();

        auto cellSet = [](const string &s) {
            set<pair<int, int>> out;
            size_t p = 0;
            while ((p = s.find("PLACE_TRACKS ", p)) != string::npos)
            {
                int x, y;
                if (sscanf(s.c_str() + p, "PLACE_TRACKS %d %d", &x, &y) == 2)
                    out.insert({x, y});
                p += 13;
            }
            return out;
        };

        const bool sameCells = cellSet(lastLine) == cellSet(logged);
        fprintf(stderr, "turn %d  logged : %s\n", stopTurn, logged.c_str());
        fprintf(stderr, "turn %d  replay : %s\n", stopTurn, lastLine.c_str());

        if (lastLine == logged)
            fprintf(stderr, "turn %d  -> identical\n", stopTurn);
        else if (sameCells)
            fprintf(stderr, "turn %d  -> same cells, different order\n",
                    stopTurn);
        else if (TURN_BUDGET_MS != 30)
            fprintf(stderr,
                    "turn %d  -> differs: this build searches for %d ms, the "
                    "referee allows 30.\n"
                    "            Rebuild with `make debug` to compare against "
                    "a logged game.\n",
                    stopTurn, TURN_BUDGET_MS);
        else
            fprintf(stderr,
                    "turn %d  -> differs. The search stops on the clock, so it "
                    "cuts off at a point\n"
                    "            that follows the machine's load -- a game "
                    "logged alongside others\n"
                    "            does not truncate where a lone replay does. "
                    "Usually the first rail\n"
                    "            still matches and the turn parts ways on the "
                    "second or third.\n"
                    "            Replay a match run with `-t 1` for a "
                    "turn-exact comparison.\n",
                    stopTurn);
    }

    fprintf(stderr, "debug_tool: wrote turns 1..%d to %s\n", stopTurn,
            outdir.c_str());
    return 0;
}

static int usage()
{
    fprintf(stderr,
            "usage:\n"
            "  btk-debug live   <outdir>\n"
            "  btk-debug replay <events.jsonl> <outdir> [turn]\n");
    return 1;
}

int main(int argc, char **argv)
{
    ios::sync_with_stdio(false);

    if (argc < 3)
        return usage();

    const string mode = argv[1];
    if (mode == "live")
        return runLive(argv[2]);
    if (mode == "replay")
    {
        if (argc < 4)
            return usage();
        const int turn = argc > 4 ? atoi(argv[4]) : 0;
        return runReplay(argv[2], argv[3], turn);
    }
    return usage();
}
