#include <rose.h>
#include <string>

int main(int argc, char** argv) {
    SgProject *p = frontend(argc, argv);
    AstJSONGeneration json_gen;
    std::string output = "tmp/ast.json";
    json_gen.generate( output, p);
    return 0;
}
