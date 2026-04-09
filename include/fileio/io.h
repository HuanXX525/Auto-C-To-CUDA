#ifndef __IO__H__
#define __IO__H__
#include <filesystem> // C++17 引入
#include "nlohmann/json.hpp"
#include <unistd.h>   // readlink 需要
using json = nlohmann::json;
namespace fs = std::filesystem;

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <unistd.h>
#include <libgen.h>

using json = nlohmann::json;

// 内部方法：获取可执行文件所在目录
std::string getExeDir();


class Config
{
public:
    // 获取单例实例的静态方法
    static Config &getInstance();

    // 禁用拷贝构造和赋值操作
    Config(const Config &) = delete;
    Config &operator=(const Config &) = delete;

    // 初始化：加载配置文件
    bool load();


    // 示例：获取配置项的方法
    std::vector<std::string> getSafeFunctions();
    std::vector<std::string> getPureFunctions();


private:
    // 构造函数私有化
    Config() {}

    json data;
};

#include "rose.h"

void renameToCU(SgSourceFile *sourceFile);

#endif