#include <rose.h>

int main(int argc, char **argv)
{
    SgProject *project = frontend(argc, argv); // [cite: 119]
    project->unparse();
    return 0;
}