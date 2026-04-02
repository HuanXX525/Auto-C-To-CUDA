#include "fileio/io.h"
#include "logger.h"

std::string getExeDir()
{
    char buffer[1024];
    ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (len != -1)
    {
        buffer[len] = '\0';
        return std::string(dirname(buffer));
    }
    return ".";
}

Config &Config::getInstance()
{
    static Config instance; // 局部静态变量，C++11 起线程安全
    if (instance.data.is_null()){
        instance.load();
    }
    return instance;
}

bool Config::load()
{
    std::string path = getExeDir() + "/config.json";
    std::ifstream f(path);
    if (!f.is_open())
    {
        // std::cerr << "错误: 无法在路径 " << path << " 找到配置文件。" << std::endl;
        log_info("Can't find config file in path %s", path.c_str());
        exit(1);
        return false;
    }

    try
    {
        data = json::parse(f);
        log_info("Config Loaded");
        return true;
    }
    catch (json::parse_error &e)
    {
        // std::cerr << "JSON 解析失败: " << e.what() << std::endl;
        log_info("JSON parse faied");
        return false;
    }
}

std::vector<std::string> Config::getSafeFunctions()
{
    return data.value("safe_functions", std::vector<std::string>());
}

std::vector<std::string> Config::getPureFunctions()
{
    return data.value("pure_functions", std::vector<std::string>());
}
