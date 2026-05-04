/**
 * @file cmdline.h
 * @brief 自定义命令行参数解析(含过滤)与 compile_commands.json 解析工具
 *
 * === CmdLineParser 用法示例 ===
 *
 * @code
 * #include "utils/cmdline.h"
 *
 * int main(int argc, char** argv) {
 *     // 1. 定义自定义参数
 *     c2cuda::CmdLineParser parser;
 *     parser.addFlag("help", "h", "显示帮助信息")
 *           .addFlag("verbose", "v", "启用详细日志")
 *           .addOption("compile-db", "p", "compile_commands.json 路径", "compile_commands.json")
 *           .addOption("output", "o", "输出文件路径", "", true);  // required=true
 *
 *     // 2. 解析: 自定义参数被消费, 其余参数保留在 remaining 中
 *     auto args = parser.parse(argc, argv);
 *
 *     // 3. 使用解析结果
 *     if (args.hasFlag("help")) {
 *         parser.printHelp(argv[0]);
 *         return 0;
 *     }
 *     bool verbose = args.hasFlag("verbose");
 *     std::string dbPath = args.getOption("compile-db");  // 有默认值
 *     std::string output = args.getOption("output");      // required, 一定存在
 *
 *     // 4. 将剩余参数传递给 ROSE frontend
 *     auto rose_argv = c2cuda::CmdLineParser::toArgv(args.remaining);
 *     int rose_argc = static_cast<int>(rose_argv.size());
 *     SgProject* project = frontend(rose_argc, rose_argv.data());
 *
 *     return 0;
 * }
 * @endcode
 *
 * 命令行调用示例:
 *   ./translate.out --verbose --compile-db=build/cc.json --output result.cu \
 *       input.c -rose:o output.cu
 *   其中 --verbose, --compile-db, --output 被 CmdLineParser 消费,
 *   input.c 和 -rose:o output.cu 保留在 remaining 传递给 ROSE.
 *
 * === CompileCommandsDB 用法示例 ===
 *
 * @code
 * #include "utils/cmdline.h"
 *
 * // 加载数据库
 * c2cuda::CompileCommandsDB db;
 * if (!db.load("compile_commands.json")) {
 *     std::cerr << "Failed to load compile_commands.json\n";
 *     return 1;
 * }
 *
 * // 按文件名精确查找
 * const auto* cmd = db.findByFile("src/kernel/kernel.cpp");
 * if (cmd) {
 *     // 获取该文件的 include 路径
 *     auto includes = cmd->getIncludePaths();  // ["/usr/rose/include/rose", ...]
 *     // 获取该文件的宏定义
 *     auto defines = cmd->getDefines();        // ["C2CUDEBUG", ...]
 *     // 获取绝对路径
 *     std::string abs = cmd->getAbsoluteFile();
 * }
 *
 * // 按后缀匹配 (只提供文件名)
 * const auto* cmd2 = db.findByFileSuffix("kernel.cpp");
 *
 * // 获取所有源文件列表
 * auto allFiles = db.getAllFiles();
 *
 * // 获取所有去重的 include 路径
 * auto allIncludes = db.getAllIncludePaths();
 * @endcode
 */

#ifndef __UTILS_CMDLINE_H__
#define __UTILS_CMDLINE_H__

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <stdexcept>
#include <iostream>
#include <fstream>
#include "nlohmann/json.hpp"

namespace c2cuda {

// ============================================================
// 命令行参数定义与解析
// ============================================================

struct ArgDef {
    std::string long_name;        // --name
    std::string short_name;       // -n (可选, 为空则无短选项)
    std::string description;
    bool has_value = false;       // 是否接受一个值 (--name=value 或 --name value)
    std::string default_value;    // 默认值 (仅 has_value=true 时有意义)
    bool required = false;
};

struct ParsedArgs {
    std::unordered_map<std::string, std::string> options;  // long_name -> value
    std::unordered_set<std::string> flags;                 // 无值选项被设置的集合
    std::vector<std::string> positional;                   // 位置参数 (非选项)
    std::vector<std::string> remaining;                    // 过滤后剩余参数 (传递给 ROSE 等)

    bool hasFlag(const std::string& name) const {
        return flags.count(name) > 0;
    }

    std::string getOption(const std::string& name, const std::string& fallback = "") const {
        auto it = options.find(name);
        return it != options.end() ? it->second : fallback;
    }

    bool hasOption(const std::string& name) const {
        return options.count(name) > 0;
    }
};

class CmdLineParser {
public:
    CmdLineParser& add(const ArgDef& def) {
        defs_.push_back(def);
        long_map_[def.long_name] = defs_.size() - 1;
        if (!def.short_name.empty()) {
            short_map_[def.short_name] = defs_.size() - 1;
        }
        return *this;
    }

    // 便捷方法: 添加无值 flag
    CmdLineParser& addFlag(const std::string& long_name,
                           const std::string& short_name,
                           const std::string& desc) {
        return add({long_name, short_name, desc, false, "", false});
    }

    // 便捷方法: 添加带值选项
    CmdLineParser& addOption(const std::string& long_name,
                             const std::string& short_name,
                             const std::string& desc,
                             const std::string& default_val = "",
                             bool required = false) {
        return add({long_name, short_name, desc, true, default_val, required});
    }

    /**
     * 解析命令行参数
     * 自定义参数会被消费, 未识别的参数保留在 remaining 中 (供 ROSE frontend 使用)
     */
    ParsedArgs parse(int argc, char** argv) const {
        ParsedArgs result;

        // argv[0] 是程序名, 总是保留给 remaining
        if (argc > 0) {
            result.remaining.push_back(argv[0]);
        }

        for (int i = 1; i < argc; ++i) {
            std::string arg(argv[i]);
            std::string key;
            std::string value;
            bool has_eq = false;

            // 处理 --key=value 形式
            if (arg.rfind("--", 0) == 0) {
                auto eq_pos = arg.find('=');
                if (eq_pos != std::string::npos) {
                    key = arg.substr(2, eq_pos - 2);
                    value = arg.substr(eq_pos + 1);
                    has_eq = true;
                } else {
                    key = arg.substr(2);
                }
            }
            // 处理 -k 或 -k value 形式
            else if (arg.size() > 1 && arg[0] == '-' && arg[1] != '-') {
                key = arg.substr(1);
                // 检查是否是短选项
                auto sit = short_map_.find(key);
                if (sit != short_map_.end()) {
                    key = defs_[sit->second].long_name;
                } else {
                    // 不认识的短选项, 保留给 remaining
                    result.remaining.push_back(arg);
                    continue;
                }
            } else {
                // 位置参数或不认识的参数, 保留
                result.remaining.push_back(arg);
                continue;
            }

            // 查找定义
            auto lit = long_map_.find(key);
            if (lit == long_map_.end()) {
                // 不认识的长选项, 保留给 remaining
                result.remaining.push_back(arg);
                continue;
            }

            const ArgDef& def = defs_[lit->second];

            if (def.has_value) {
                if (!has_eq) {
                    // 取下一个参数作为值
                    if (i + 1 < argc) {
                        value = argv[++i];
                    } else {
                        throw std::runtime_error(
                            "Option --" + def.long_name + " requires a value");
                    }
                }
                result.options[def.long_name] = value;
            } else {
                result.flags.insert(def.long_name);
            }
        }

        // 填充默认值
        for (const auto& def : defs_) {
            if (def.has_value && !result.hasOption(def.long_name) && !def.default_value.empty()) {
                result.options[def.long_name] = def.default_value;
            }
        }

        // 检查必填项
        for (const auto& def : defs_) {
            if (def.required && def.has_value && !result.hasOption(def.long_name)) {
                throw std::runtime_error(
                    "Required option --" + def.long_name + " is missing");
            }
        }

        return result;
    }

    /**
     * 从 remaining 构造新的 argc/argv (用于传递给 ROSE frontend)
     * 注意: 返回的 argv 指针指向 remaining 中的 c_str(), 调用者需确保 remaining 生命周期
     */
    static std::vector<char*> toArgv(std::vector<std::string>& remaining) {
        std::vector<char*> argv;
        argv.reserve(remaining.size());
        for (auto& s : remaining) {
            argv.push_back(s.data());
        }
        return argv;
    }

    void printHelp(const std::string& program_name = "") const {
        if (!program_name.empty()) {
            std::cerr << "Usage: " << program_name << " [options] [ROSE options] <input.c>\n\n";
        }
        std::cerr << "Options:\n";
        for (const auto& def : defs_) {
            std::string opt = "  --" + def.long_name;
            if (!def.short_name.empty()) {
                opt += ", -" + def.short_name;
            }
            if (def.has_value) {
                opt += " <value>";
            }
            // 对齐
            while (opt.size() < 30) opt += ' ';
            std::cerr << opt << def.description;
            if (def.has_value && !def.default_value.empty()) {
                std::cerr << " (default: " << def.default_value << ")";
            }
            if (def.required) {
                std::cerr << " [required]";
            }
            std::cerr << "\n";
        }
    }

private:
    std::vector<ArgDef> defs_;
    std::unordered_map<std::string, size_t> long_map_;   // long_name -> index
    std::unordered_map<std::string, size_t> short_map_;  // short_name -> index
};

// ============================================================
// compile_commands.json 解析
// ============================================================

struct CompileCommand {
    std::string file;                      // 源文件路径
    std::string directory;                 // 工作目录
    std::vector<std::string> arguments;    // 编译参数列表
    std::string command;                   // 编译命令字符串 (与 arguments 二选一)

    // 从 arguments 中提取 include 路径 (-I...)
    std::vector<std::string> getIncludePaths() const {
        std::vector<std::string> paths;
        for (size_t i = 0; i < arguments.size(); ++i) {
            const auto& arg = arguments[i];
            if (arg.rfind("-I", 0) == 0) {
                if (arg.size() > 2) {
                    paths.push_back(arg.substr(2));
                } else if (i + 1 < arguments.size()) {
                    paths.push_back(arguments[i + 1]);
                }
            }
        }
        return paths;
    }

    // 从 arguments 中提取宏定义 (-D...)
    std::vector<std::string> getDefines() const {
        std::vector<std::string> defs;
        for (const auto& arg : arguments) {
            if (arg.rfind("-D", 0) == 0) {
                defs.push_back(arg.substr(2));
            }
        }
        return defs;
    }

    // 获取源文件的绝对路径
    std::string getAbsoluteFile() const {
        if (!file.empty() && file[0] == '/') return file;
        if (directory.empty()) return file;
        std::string path = directory;
        if (path.back() != '/') path += '/';
        path += file;
        return path;
    }
};

class CompileCommandsDB {
public:
    /**
     * 从文件路径加载 compile_commands.json
     * @return true 加载成功, false 失败
     */
    bool load(const std::string& path) {
        std::ifstream ifs(path);
        if (!ifs.is_open()) return false;

        nlohmann::json j;
        try {
            ifs >> j;
        } catch (const nlohmann::json::parse_error&) {
            return false;
        }

        if (!j.is_array()) return false;

        commands_.clear();
        file_index_.clear();

        for (const auto& entry : j) {
            CompileCommand cmd;

            if (entry.contains("file")) {
                cmd.file = entry["file"].get<std::string>();
            }
            if (entry.contains("directory")) {
                cmd.directory = entry["directory"].get<std::string>();
            }
            if (entry.contains("arguments") && entry["arguments"].is_array()) {
                for (const auto& arg : entry["arguments"]) {
                    cmd.arguments.push_back(arg.get<std::string>());
                }
            }
            if (entry.contains("command")) {
                cmd.command = entry["command"].get<std::string>();
                // 如果没有 arguments 但有 command, 做简单分词
                if (cmd.arguments.empty()) {
                    cmd.arguments = splitCommand(cmd.command);
                }
            }

            file_index_[cmd.file] = commands_.size();
            commands_.push_back(std::move(cmd));
        }

        return true;
    }

    // 获取所有编译命令
    const std::vector<CompileCommand>& getCommands() const {
        return commands_;
    }

    // 根据文件名查找 (精确匹配)
    const CompileCommand* findByFile(const std::string& file) const {
        auto it = file_index_.find(file);
        if (it != file_index_.end()) {
            return &commands_[it->second];
        }
        return nullptr;
    }

    // 根据文件名后缀匹配 (如只提供文件名而非完整路径)
    const CompileCommand* findByFileSuffix(const std::string& suffix) const {
        for (const auto& cmd : commands_) {
            if (cmd.file.size() >= suffix.size() &&
                cmd.file.compare(cmd.file.size() - suffix.size(), suffix.size(), suffix) == 0) {
                return &cmd;
            }
        }
        return nullptr;
    }

    // 获取所有源文件列表
    std::vector<std::string> getAllFiles() const {
        std::vector<std::string> files;
        files.reserve(commands_.size());
        for (const auto& cmd : commands_) {
            files.push_back(cmd.file);
        }
        return files;
    }

    // 获取所有唯一的 include 路径
    std::vector<std::string> getAllIncludePaths() const {
        std::unordered_set<std::string> seen;
        std::vector<std::string> paths;
        for (const auto& cmd : commands_) {
            for (const auto& p : cmd.getIncludePaths()) {
                if (seen.insert(p).second) {
                    paths.push_back(p);
                }
            }
        }
        return paths;
    }

    size_t size() const { return commands_.size(); }
    bool empty() const { return commands_.empty(); }

private:
    std::vector<CompileCommand> commands_;
    std::unordered_map<std::string, size_t> file_index_;

    // 简单的命令行分词 (处理引号)
    static std::vector<std::string> splitCommand(const std::string& cmd) {
        std::vector<std::string> tokens;
        std::string current;
        bool in_single_quote = false;
        bool in_double_quote = false;
        bool escaped = false;

        for (char c : cmd) {
            if (escaped) {
                current += c;
                escaped = false;
                continue;
            }
            if (c == '\\' && !in_single_quote) {
                escaped = true;
                continue;
            }
            if (c == '\'' && !in_double_quote) {
                in_single_quote = !in_single_quote;
                continue;
            }
            if (c == '"' && !in_single_quote) {
                in_double_quote = !in_double_quote;
                continue;
            }
            if (std::isspace(static_cast<unsigned char>(c)) && !in_single_quote && !in_double_quote) {
                if (!current.empty()) {
                    tokens.push_back(std::move(current));
                    current.clear();
                }
                continue;
            }
            current += c;
        }
        if (!current.empty()) {
            tokens.push_back(std::move(current));
        }
        return tokens;
    }
};

} // namespace c2cuda

inline void plot_args(std::vector<char*>& argv) {
    std::cerr << "the args for rose frontend: \n";
    for(char* s : argv) {
        std::cerr << s << ", ";
    }
    std::cerr << "\n";
}

#endif // __UTILS_CMDLINE_H__
