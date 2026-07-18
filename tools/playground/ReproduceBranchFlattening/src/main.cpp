#include <rose.h>
#include "branch_flatten.hpp"

int main(int argc, char **argv)
{
    SgProject *project = frontend(argc, argv);

    Rose_STL_Container<SgNode *> funcDefs =
        NodeQuery::querySubTree(project, V_SgFunctionDefinition);

    for (auto *node : funcDefs)
    {
        SgFunctionDefinition *funcDef = isSgFunctionDefinition(node);
        if (funcDef)
        {
            branch_flatten::flattenBranches(funcDef);
        }
    }

    AstPostProcessing(project);
    project->unparse();
    return 0;
}
