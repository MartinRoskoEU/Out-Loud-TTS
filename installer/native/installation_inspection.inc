// The validation sequence and predicates are unchanged. Diagnostics observe it;
// reports are never consumed as authorization by native mutation operations.
void inspect_installation_root(const fs::path& input){
    inspection_stage("path_syntax",input);
    const auto root=installation_path(input);
    require_installation_location(root);
    inspection_stage("path_budget",root);
    maintenance_path_budget(root,{std::begin(registration_payload_paths),std::end(registration_payload_paths)});
    inspection_stage("existing_contents",root);
    if(fs::exists(root) && !fs::is_empty(root) && !fs::exists(root/L"configuration/signing.ini"))
        static_cast<void>(maintenance_failed_fresh_identity(root,{std::begin(registration_payload_paths),std::end(registration_payload_paths)}));
    const auto state=root/L"configuration/signing.ini";
    inspection_stage("ownership_state",state);
    if(fs::exists(state)){
        require_admin_protected(state);Handle file;pin(state,file);
        signing_require(ini(state,L"ownership",L"schema")==L"1" && ini(state,L"ownership",L"product")==L"OutLoud" &&
            ini(state,L"ownership",L"scope")==L"LocalMachine" && hexadecimal(ini(state,L"ownership",L"installation_id"),32) &&
            hexadecimal(ini(state,L"ownership",L"certificate_sha256"),64),"ownership_mismatch");
    }
    inspection_stage("complete",root);
}
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
int run_installation_inspection(int argc,wchar_t** argv){
    InstallationDiagnostic validation;
    const bool valid=inspection_observe(validation,[&]{
        signing_require(argc==4,"invalid_arguments");inspect_installation_root(argv[2]);});
    const auto report=L"[results]\nschema=1\nlocation="+std::wstring(valid?L"secure_candidate":L"rejected")+L"\n"+inspection_fields(validation);
    if(!valid)inspection_stderr(report);
    // A secondary report failure must not erase the original security refusal.
    InstallationDiagnostic reporting;
    const bool written=inspection_observe(reporting,[&]{
        inspection_stage("report_write",argc==4?fs::path(argv[3]):fs::path{});
        signing_require(argc==4,"invalid_arguments");write_new(argv[3],report);});
    if(!written){reporting.category="reporting";
        inspection_stderr(inspection_fields(reporting,valid?L"inspection_":L"report_failure_"));}
    return valid && written?0:2;
}
