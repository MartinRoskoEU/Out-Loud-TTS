#include "prerequisites.hpp"
#include <iostream>
#include <stdexcept>
using namespace outloud::installer;
int main() {
    unsigned count=0;
    const auto check=[&](bool ok) { if (!ok) throw std::runtime_error("Prerequisite assertion failed."); ++count; };
    const auto v=[](const wchar_t* s) { return version(s).value(); };
    WindowsFacts win{false,10,0,22000,1,0x8664};
    check(evaluate(win).status==L"compatible");
    win.build=19045; check(evaluate(win).status==L"unsupported"); win.build=26100;
    win.machine=0xaa64; check(evaluate(win).status==L"unsupported");
    win.machine=0x14c; check(evaluate(win).status==L"unsupported"); win.machine=0x8664;
    win.product=3; check(evaluate(win).status==L"unsupported");
    win.product=2; check(evaluate(win).status==L"unsupported");
    win.product=0; check(evaluate(win).status==L"unsupported");
    win.failed=true; check(evaluate(win).status==L"detection_failed");
    const auto minimum=v(L"14.44.35211.0"); VCFacts vc;
    check(evaluate(vc,minimum).status==L"missing");
    vc.other_arch=true; check(evaluate(vc,minimum).status==L"incorrect_architecture");
    vc.installed=minimum; check(evaluate(vc,minimum).status==L"compatible");
    vc.installed=v(L"14.43.35000.0"); check(evaluate(vc,minimum).status==L"version_too_old");
    vc.installed=v(L"14.51.36247.0"); check(evaluate(vc,minimum).status==L"compatible");
    vc.files_valid=false; check(evaluate(vc,minimum).status==L"detection_failed");
    vc.failed=true; check(evaluate(vc,minimum).status==L"detection_failed");
    vc.failed=false; vc.installed=v(L"15.0.10000.0"); check(evaluate(vc,minimum).status==L"incompatible_version");
    const auto netmin=v(L"10.0.0"); DotNetFacts net;
    check(evaluate(net,netmin,netmin).status==L"missing"); // Missing Desktop / SDK alone.
    net.core={netmin}; check(evaluate(net,netmin,netmin).status==L"missing"); // Core alone.
    net.wrong_arch=true; check(evaluate(net,netmin,netmin).status==L"incorrect_architecture"); net.wrong_arch=false;
    net.desktop={v(L"9.0.12")}; check(evaluate(net,netmin,netmin).status==L"version_too_old");
    net.desktop={v(L"11.0.0")}; check(evaluate(net,netmin,netmin).status==L"incompatible_version");
    net.desktop={netmin}; net.host_ok=true; net.resolved_desktop=netmin; net.resolved_core=netmin;
    check(evaluate(net,netmin,netmin).status==L"compatible");
    net.desktop={v(L"10.0.12")}; net.resolved_desktop=v(L"10.0.12"); net.resolved_core=v(L"10.0.12");
    check(evaluate(net,netmin,netmin).status==L"compatible");
    net.files_valid=false; check(evaluate(net,netmin,netmin).status==L"detection_failed"); net.files_valid=true;
    net.host_ok=false; check(evaluate(net,netmin,netmin).status==L"detection_failed");
    net.resolution_seen=true; check(evaluate(net,netmin,netmin).status==L"incompatible_version");
    net.core={v(L"9.0.12")}; check(evaluate(net,netmin,netmin).status==L"version_too_old");
    net.core={v(L"11.0.0")}; check(evaluate(net,netmin,netmin).status==L"incompatible_version");
    net.core.clear(); check(evaluate(net,netmin,netmin).status==L"missing");
    net.failed=true; check(evaluate(net,netmin,netmin).status==L"detection_failed");
    check(!version(L"10.0.0-preview.1")); check(!version(L"14.x.35112")); check(!version(L"65536.0.0"));
    check(v(L"14.51.1.0")>v(L"14.44.9999.0"));
    std::cout<<count<<" controlled prerequisite assertions PASS\n";
}
