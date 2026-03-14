#include "test.h"
#include <iostream>
#include "../include/nlohmann/json.hpp"
#include "../include/logger.h"

void TestRoseFunction::getFuncDefFromCall(SgProject *project)
{ // 查找所有的函数调用
    Rose_STL_Container<SgNode *> functions = NodeQuery::querySubTree(project, V_SgFunctionCallExp);

    std::cout << "\n========== 跨文件函数分析报告 ==========\n"
              << std::endl;

    for (auto it = functions.begin(); it != functions.end(); ++it)
    {
        SgFunctionCallExp *call = isSgFunctionCallExp(*it);

        // 1. 获取关联的声明（可能是头文件里的原型）
        SgFunctionDeclaration *decl = call->getAssociatedFunctionDeclaration();
        if (!decl)
            continue;

        std::string funcName = decl->get_name().getString();
        // 获取调用发生的位置（文件名）
        std::string callLoc = call->get_file_info()->get_filename();

        std::cout << "发现调用: " << funcName << " (位于: " << callLoc << ")" << std::endl;

        // 2. 关键：尝试跳转到定义（Defining Declaration）
        SgFunctionDeclaration *defDecl = isSgFunctionDeclaration(decl->get_definingDeclaration());

        if (defDecl && defDecl->get_definition())
        {
            // 获取定义所在的位置
            std::string defLoc = defDecl->get_file_info()->get_filename();

            if (defLoc == callLoc)
            {
                std::cout << "  [√] 状态: 内部调用 (定义在同一文件中)" << std::endl;
            }
            else
            {
                std::cout << "  [★] 状态: 跨文件链接成功！" << std::endl;
                std::cout << "      -> 定义文件: " << defLoc << std::endl;
                std::cout << "      -> 定义行号: " << defDecl->get_file_info()->get_line() << std::endl;
            }
        }
        else
        {
            std::cout << "  [×] 状态: 找不到定义 (可能是外部库函数或 Merge 未生效)" << std::endl;
        }
        std::cout << "------------------------------------------" << std::endl;
    }
}
using json = nlohmann::json;
void TestRoseFunction::readJson(std::string s){
    std::ifstream f(s);
    if (!f.is_open())
    {
        log_error("Read File %s Error", s);
        return;
    }

    try
    {
        // 2. 解析 JSON
        json data = json::parse(f);

        // 4. 读取数组（白名单）
        std::vector<std::string> whiteList = data["safe_math_functions"];

        std::cout << "已加载 " << whiteList.size() << " 个安全函数。" << std::endl;
        for (const auto &func : whiteList)
        {
            // 这里你可以把它们存入 std::set 方便后续查询
            std::cout << " - " << func << std::endl;
        }
    }
    catch (json::parse_error &e)
    {
        std::cerr << "JSON 解析错误: " << e.what() << std::endl;
    }
}


void TestRoseFunction::drawDot(SgNode *project){
    generateDOT(project, "rose.dot");
}