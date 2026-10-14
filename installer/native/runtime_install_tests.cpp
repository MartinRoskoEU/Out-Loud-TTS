#include "runtime_install.hpp"
#include <iostream>
#include <vector>
#include <algorithm>
using namespace outloud::installer;
struct Double {
    std::vector<std::wstring> statuses{L"missing",L"missing",L"compatible"};
    PackageFacts facts{true,true,true,true,true,true};
    bool consented=true,downloaded=true,execution_fails=false;
    std::uint32_t exit=0;
    unsigned detects{},downloads{},verifications{},executions{},consents{};
    std::wstring detect() { return statuses[std::min<size_t>(detects++,statuses.size()-1)]; }
    bool consent() { ++consents; return consented; }
    bool acquire() { ++downloads; return downloaded; }
    PackageFacts verify() { ++verifications; return facts; }
    std::uint32_t execute() { ++executions; return execution_fails?0xffffffff:exit; }
};
int main() {
    unsigned checks=0;
    const auto check=[&](bool value) { if(!value) throw std::runtime_error("Runtime acquisition assertion failed."); ++checks; };
    for(const auto component:{L"vc",L"dotnet"}) {
        Double installed; installed.statuses={L"compatible"};
        check(ensure_runtime(component,installed)==L"verified" && installed.executions==0 && installed.downloads==0 && installed.consents==0);
        Double missing; check(ensure_runtime(component,missing)==L"verified" && missing.detects==3 && missing.executions==1);
        Double cancelled; cancelled.consented=false; check(ensure_runtime(component,cancelled)==L"cancelled" && cancelled.downloads==0 && cancelled.executions==0);
        Double interrupted; interrupted.downloaded=false; check(ensure_runtime(component,interrupted)==L"acquisition_failed" && interrupted.verifications==0 && interrupted.executions==0);
        for(unsigned field=0;field<6;++field) {
            Double bad;
            switch(field) { case 0:bad.facts.size=false;break;case 1:bad.facts.hash=false;break;case 2:bad.facts.signature=false;break;
                case 3:bad.facts.publisher=false;break;case 4:bad.facts.architecture=false;break;default:bad.facts.version=false; }
            check(ensure_runtime(component,bad)==L"verification_failed" && bad.executions==0);
        }
        Double failed; failed.exit=1603; check(ensure_runtime(component,failed)==L"failed" && failed.detects==3);
        Double cannot_execute; cannot_execute.execution_fails=true; check(ensure_runtime(component,cannot_execute)==L"failed" && cannot_execute.detects==3);
        for(const auto code:{3010u,1641u}) { Double restart; restart.exit=code; check(ensure_runtime(component,restart)==L"restart_required" && restart.detects==3); }
        Double still_missing; still_missing.statuses={L"missing"}; check(ensure_runtime(component,still_missing)==L"failed" && still_missing.executions==1);
        Double raced; raced.statuses={L"missing",L"compatible"}; check(ensure_runtime(component,raced)==L"verified" && raced.executions==0);
        Double uncertain; uncertain.statuses={L"detection_failed"}; check(ensure_runtime(component,uncertain)==L"failed" && uncertain.downloads==0);
        Double user_cancel; user_cancel.exit=1602; check(ensure_runtime(component,user_cancel)==L"cancelled" && user_cancel.detects==3);
        Double new_race; new_race.exit=1638; check(ensure_runtime(component,new_race)==L"verified");
        new_race=Double{}; new_race.exit=1638; new_race.statuses={L"missing"}; check(ensure_runtime(component,new_race)==L"failed");
    }
    for(const auto vc:{L"compatible",L"missing"}) for(const auto net:{L"compatible",L"missing"}) {
        Double v,n; v.statuses={vc,vc,L"compatible"}; n.statuses={net,net,L"compatible"};
        check(ensure_runtime(L"vc",v)==L"verified" && ensure_runtime(L"dotnet",n)==L"verified" &&
            v.executions==(std::wstring(vc)==L"compatible"?0u:1u) && n.executions==(std::wstring(net)==L"compatible"?0u:1u));
    }
    check(acquisition_action(L"vc",L"incompatible_version")==L"block");
    check(acquisition_action(L"dotnet",L"incompatible_version")==L"install");
    check(acquisition_action(L"vc",L"unknown")==L"block");
    std::cout<<checks<<" controlled runtime acquisition assertions PASS; no system operations.\n";
}
