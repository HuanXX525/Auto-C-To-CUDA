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
    if (instance.data.is_null())
    {
        instance.load();
    }
    return instance;
}

bool Config::load()
{
    std::vector<std::string> candidate_paths = {
        getExeDir() + "/config.json",
        getExeDir() + "/../config.json",
        getExeDir() + "/../../config.json",
        "config.json"};

    std::ifstream f;
    std::string path;
    for (const auto &candidate : candidate_paths)
    {
        f.open(candidate);
        if (f.is_open())
        {
            path = candidate;
            break;
        }
        f.clear();
    }

    if (!f.is_open())
    {
        // std::cerr << "错误: 无法在路径 " << path << " 找到配置文件。" << std::endl;
        log_info("Can't find config file");
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

void renameToCU(SgSourceFile *sourceFile)
{
    std::string currentOutput = sourceFile->get_unparse_output_filename();
    if(!currentOutput.empty())
        return;
    std::string fullName = sourceFile->get_sourceFileNameWithPath();
    log_info(">>>> Translating File: %s <<<<\n\n", fullName.c_str());

    // 2. 查找最后一个点来替换后缀
    size_t lastDot = fullName.find_last_of(".");
    std::string newName;
    if (lastDot != std::string::npos)
    {
        newName = fullName.substr(0, lastDot) + ".cu";
    }
    else
    {
        newName = fullName + ".cu";
    }

    // 3. 设置输出文件名，此时包含了原始路径
    sourceFile->set_unparse_output_filename(newName);
}
