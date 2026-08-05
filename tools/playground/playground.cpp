#include <rose.h>
#include "pass/PassManager.hpp"
#include "analysis/AnalysisTest.hpp"
using namespace c2cuda;

int main(int argc, char **argv)
{
    PassManager pm;
    SgProject *project = frontend(argc, argv); // [cite: 119]
    pm.add<analysis::AnalysisTestPass>();
    pm.run(project);

    // project->unparse();
    return 0;
}