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

bool renameToCU(SgSourceFile *sourceFile, const std::string &requestedOutput)
{
    std::string currentOutput = sourceFile->get_unparse_output_filename();
    if(!currentOutput.empty())
        return true;
    std::string fullName = sourceFile->get_sourceFileNameWithPath();
    log_info(">>>> Translating File: %s <<<<\n\n", fullName.c_str());

    fs::path outputPath;
    if (!requestedOutput.empty())
    {
        outputPath = requestedOutput;
        std::error_code ec;
        fs::path parent = outputPath.parent_path();
        if (!parent.empty())
        {
            fs::create_directories(parent, ec);
        }
        if (ec)
        {
            log_error("Failed to create output directory %s: %s",
                      parent.string().c_str(), ec.message().c_str());
            return false;
        }
    }
    else
    {
        outputPath = fullName;
        outputPath.replace_extension(".cu");
    }

    sourceFile->set_unparse_output_filename(outputPath.string());
    return true;
}
