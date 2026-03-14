#include <rose.h>

namespace TestRoseFunction{
    void getFuncDefFromCall(SgProject *project); // 从调用获取函数定义
    // void getFuncConstAttribute(SgProject *project);
    void readJson(string str);
    void drawDot(SgNode *project);
}