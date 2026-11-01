// The validation sequence and predicates are unchanged. Diagnostics observe it;
// reports are never consumed as authorization by native mutation operations.

template<class Action> bool inspection_observe(InstallationDiagnostic& diagnostic,Action&& action){
    InstallationDiagnosticScope scope(diagnostic);
    try {action();return true;}
    catch(const SigningError& error){diagnostic.reason=error.what();}
    catch(const fs::filesystem_error& error){
        diagnostic.reason="filesystem_error";
        diagnostic.exception=inspection_wide(error.what());
        if(!error.path1().empty())diagnostic.path=error.path1();
        if(error.code().category()==std::system_category())inspection_error("std::filesystem",static_cast<DWORD>(error.code().value()));
    }
    catch(const std::exception& error){diagnostic.reason="inspection_exception";diagnostic.exception=inspection_wide(error.what());}
    catch(...){diagnostic.reason="inspection_unknown_failure";}
    return false;
}
// Read-only development-bundle security inspection. Reuses the installation
// location/ACL and independent-file checks; it grants no installation identity.
void inspect_development_bundle(const fs::path& input){
    const auto root=installation_path(input);
    require_installation_location(root);
    if(!fs::exists(root))return; // Validate the protected creation parent first.
    require_admin_protected(root);
    std::vector<fs::path> pending{root};
    while(!pending.empty()){
        const auto directory=pending.back();pending.pop_back();
        for(const auto& entry:fs::directory_iterator(directory)){
            const auto path=entry.path();require_admin_protected(path);
            if(entry.is_directory())pending.push_back(path);
            else{Handle file;pin(path,file);}
        }
    }
}
int run_installation_inspection(int argc,wchar_t** argv){
    InstallationDiagnostic validation;
    const bool valid=inspection_observe(validation,[&]{
        signing_require(argc==5 && std::wstring(argv[4])==L"--development-bundle","legacy_installer_inspection_retired");
        inspect_development_bundle(argv[2]);});
    const auto report=L"[results]\nschema=1\nlocation="+std::wstring(valid?L"secure_candidate":L"rejected")+L"\n"+inspection_fields(validation);
    if(!valid)inspection_stderr(report);
    // A secondary report failure must not erase the original security refusal.
    InstallationDiagnostic reporting;
    const bool written=inspection_observe(reporting,[&]{
        inspection_stage("report_write",argc>=4?fs::path(argv[3]):fs::path{});
        signing_require(argc==5,"invalid_arguments");write_new(argv[3],report);});
    if(!written){reporting.category="reporting";
        inspection_stderr(inspection_fields(reporting,valid?L"inspection_":L"report_failure_"));}
    return valid && written?0:2;
}
