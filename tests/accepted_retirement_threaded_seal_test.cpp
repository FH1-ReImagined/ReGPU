#include "gpu/accepted_retirement_threaded.h"
#include <cassert>
#include <fstream>
#include <memory>
using namespace regpu::accepted_threaded;
int main(int argc, char** argv) {
    assert(argc == 2);
    const std::filesystem::path path(argv[1]);
    assert(!std::filesystem::exists(path) && !std::filesystem::exists(path.string() + ".partial"));
    {
        auto open = std::make_unique<State>(); open->path = path.string();
        open->protocol.limit = 1;
    }
    assert(!std::filesystem::exists(path)); // no destructor publication
    {
        auto state = std::make_unique<State>(); state->path = path.string();
        auto& p = state->protocol; p.limit = 1;
        auto ticket = p.Begin(regpu::accepted_retirement::kHeader,6,0,1,101,true,false,100);
        p.Bind(ticket,64,64,101);
        assert(p.Start(ticket,64,101,6,102,false));
        p.Close(ticket,201,301,true,0,0,0,0,0,false,103); p.End(ticket,104);
        state->Seal(); assert(!std::filesystem::exists(path)); // producer post-Pace required too
        p.Paced(ticket,105); state->Seal(); assert(std::filesystem::exists(path));
        std::ifstream input(path); std::string line; std::getline(input,line);
        assert(line == "#protocol=regpu_accepted_threaded_v1 sealed=1 failed=0 reason=accepted-limit limit=1 rows=1 completed=1");
        state->Seal();
    }
    std::ifstream before(path); const std::string bytes((std::istreambuf_iterator<char>(before)), {}); before.close();
    {
        auto refuse = std::make_unique<State>(); refuse->path = path.string();
        refuse->protocol.Fail("fixture-failure"); refuse->Seal();
    }
    std::ifstream after(path); const std::string unchanged((std::istreambuf_iterator<char>(after)), {});
    assert(unchanged == bytes && !std::filesystem::exists(path.string() + ".partial"));
}
