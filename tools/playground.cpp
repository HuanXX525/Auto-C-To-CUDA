#include "test.h"

int main(int argc, char **argv)
{
    ROSE_INITIALIZE;
    SgProject *project = frontend(argc, argv);
    // TestRoseFunction::getFuncDefFromCall(project);
    // TestRoseFunction::getFuncConstAttribute(project);
    // TestRoseFunction::readJson()
    // TestRoseFunction::drawDot(project);

    return 0;
}