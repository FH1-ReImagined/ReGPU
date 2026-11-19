#include "gpu/accepted_retirement_window.h"
#include <cassert>
#include <fstream>
#include <iterator>
#include <memory>
using namespace regpu::accepted_retirement;
std::string Read(const std::string& path) {
    std::ifstream f(path);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}
void Populate(State& s) {
    s.protocol.limit=2;
    for(uint32_t i=1;i<=2;++i) {
        assert(s.protocol.Begin(kHeader,6,0,i,10+i,false,false));
        s.protocol.Close(20+i,30+i,true,0,0,0,0,false);
        s.protocol.current.decode_ns=100*i;
        s.protocol.current.close_ns=100*i+1;
        s.protocol.current.return_ns=100*i+2;
        const auto row=s.protocol.current;
        assert(s.protocol.End());s.rows[i-1]=row;
    }
}
int main(int argc,char** argv) {
    assert(argc==2);
    const std::string path=argv[1];
    assert(!std::filesystem::exists(path) && !std::filesystem::exists(path+".partial"));
    auto owner_s=std::make_unique<State>();auto& s=*owner_s;s.path=path;Populate(s);s.Seal();
    assert(std::filesystem::exists(path) && !std::filesystem::exists(path+".partial"));
    const auto bytes=Read(path);
    assert(bytes.find("sealed=1 failed=0 reason=accepted-limit limit=2 rows=2")!=std::string::npos);
    assert(bytes.find("1,11,21,31,0,0,0,0,100,101,102,0")!=std::string::npos);
    assert(bytes.find("2,12,22,32,0,0,0,0,200,201,202,0")!=std::string::npos);
    s.Seal();assert(Read(path)==bytes);
    auto owner_existing=std::make_unique<State>();auto& existing=*owner_existing;existing.path=path;Populate(existing);existing.Seal();assert(Read(path)==bytes);
    const auto failed=path+".failed";
    assert(!std::filesystem::exists(failed));
    auto owner_missing=std::make_unique<State>();auto& missing=*owner_missing;missing.path=failed;
    assert(missing.protocol.Begin(kHeader,6,0,1,1,false,false));
    assert(!missing.protocol.End());missing.Seal();
    assert(Read(failed).find("failed=1 reason=no-exact-close")!=std::string::npos);
    const auto early=path+".early";assert(!std::filesystem::exists(early));
    { auto owner_no_destructor_publish=std::make_unique<State>();auto& no_destructor_publish=*owner_no_destructor_publish;no_destructor_publish.path=early;Populate(no_destructor_publish); }
    assert(!std::filesystem::exists(early)); // Runner must not infer success from process exit.
    const auto pending=path+".pending";
    assert(!std::filesystem::exists(pending) && !std::filesystem::exists(pending+".partial"));
    { std::ofstream f(pending+".partial");f<<"retained-partial"; }
    auto owner_partial=std::make_unique<State>();auto& partial=*owner_partial;partial.path=pending;Populate(partial);partial.Seal();
    assert(!std::filesystem::exists(pending) && Read(pending+".partial")=="retained-partial");
}
