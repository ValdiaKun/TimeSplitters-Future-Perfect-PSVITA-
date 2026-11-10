// SPDX-License-Identifier: GPL-3.0-only
// Offline authored sources only; no firmware or runtime compiler dependency.
#include "openshacccg/compiler.h"
#include <cstdio>
#include <fstream>
#include <string>
int main(int argc,char **argv) {
    if (argc!=4 || (std::string(argv[1])!="vertex" && std::string(argv[1])!="fragment")) return 2;
    std::ifstream input(argv[2],std::ios::binary);
    if (!input) return 2;
    std::string source(65537,'\0');input.read(source.data(),static_cast<std::streamsize>(source.size()));
    source.resize(static_cast<size_t>(input.gcount()));
    if (source.empty() || source.size()>65536 || source.find('\0')!=std::string::npos) return 2;
    VscCompileRequest request{};
    request.source_name="tsfp-authored.cg";request.source=source.data();request.source_size=source.size();
    request.entrypoint="main";request.stage=std::string(argv[1])=="vertex" ? VSC_STAGE_VERTEX : VSC_STAGE_FRAGMENT;
    request.optimization_level=0;request.fast_math=0;request.fast_precision=0;request.fast_int=0;
    VscCompileResult result{};
    int status=vsc_compile(&request,&result);
    for (size_t i=0;i<result.diagnostic_count;++i) {
        const VscDiagnostic &d=result.diagnostics[i];
        std::fprintf(stderr,"shader diagnostic severity=%u code=%x line=%u column=%u %s\n",
            static_cast<unsigned>(d.severity),d.code,d.line,d.column,d.message ? d.message : "");
        if (d.severity>=VSC_DIAG_WARNING) status=1; // no fallback/unsupported-warning success
    }
    if (!status && result.gxp_data && result.gxp_size && result.gxp_size<=1024*1024) {
        std::ofstream output(argv[3],std::ios::binary);
        output.write(reinterpret_cast<const char *>(result.gxp_data),static_cast<std::streamsize>(result.gxp_size));
        if (!output) status=2;
    } else if (!status) status=1;
    vsc_destroy_result(&request.allocator,&result);
    return status;
}
