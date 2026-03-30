#include <rose.h>

int main(int argc, char** argv) {
    SgProject *p = frontend(argc, argv);
    AstJSONGeneration json_gen;
    json_gen.generate(p);
    return 0;
}
