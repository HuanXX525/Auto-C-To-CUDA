#ifndef __UTILS_TRANSLATE_JOB_H__
#define __UTILS_TRANSLATE_JOB_H__

#include "utils/cmdline.h"
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace c2cuda {

// ============================================================
// TranslateJob: 翻译任务的统一描述
// ============================================================

enum class TranslateMode {
    SingleFile,      // 单文件, 直接走 ROSE 原有逻辑
    CompileDB,       // 多文件, 从 compile_commands.json 构建
    ScanDir          // 多文件, 递归扫描目录
};

struct TranslateJob {
    TranslateMode mode = TranslateMode::SingleFile;
    std::vector<std::string> source_files;     // 待翻译的 .c 文件 (绝对路径)
    std::vector<std::string> include_paths;    // -I 路径
    std::vector<std::string> defines;          // -D 宏定义
    std::vector<std::string> extra_rose_args;  // 直接传给 ROSE 的额外参数
    std::string output_dir;                    // 输出目录 (多文件模式)
    std::string base_dir;                      // 源文件公共前缀 (保持目录结构)
    bool verbose = false;

    // 构造传给 ROSE frontend 的 argv (多文件模式使用)
    std::vector<std::string> buildRoseArgv(const std::string& program) const {
        std::vector<std::string> argv;
        argv.push_back(program);
        for (const auto& inc : include_paths)
            argv.push_back("-I" + inc);
        for (const auto& def : defines)
            argv.push_back("-D" + def);
        for (const auto& arg : extra_rose_args)
            argv.push_back(arg);
        for (const auto& src : source_files)
            argv.push_back(src);
        return argv;
    }

    // 计算源文件对应的输出路径 (.c -> .cu, 保持目录结构)
    std::string getOutputPath(const std::string& source_file) const {
        fs::path src(source_file);
        fs::path base(base_dir);
        fs::path rel = fs::relative(src, base);
        fs::path out = fs::path(output_dir) / rel;
        out.replace_extension(".cu");
        return out.string();
    }
};

// ============================================================
// TranslateJobBuilder: 解析命令行, 构造 TranslateJob
// ============================================================

class TranslateJobBuilder {
public:
    struct Result {
        TranslateJob job;
        ParsedArgs args;           // 完整的解析结果
        bool should_exit = false;  // true 表示已处理完毕 (如 --help), main 应直接 return
        int exit_code = 0;
    };

    static Result build(int argc, char** argv) {
        Result result;

        CmdLineParser parser = createParser();

        try {
            result.args = parser.parse(argc, argv);
        } catch (const std::runtime_error& e) {
            std::cerr << "Error: " << e.what() << "\n";
            parser.printHelp(argc > 0 ? argv[0] : "translate.out");
            result.should_exit = true;
            result.exit_code = 1;
            return result;
        }

        auto& args = result.args;
        auto& job = result.job;

        // --help
        if (args.hasFlag("help")) {
            parser.printHelp(argc > 0 ? argv[0] : "translate.out");
            result.should_exit = true;
            return result;
        }

        job.verbose = args.hasFlag("verbose");

        bool has_compile_db = args.hasOption("compile-db") &&
                              args.getOption("compile-db") != "";
        bool has_scan_dir = args.hasOption("scan-dir") &&
                            args.getOption("scan-dir") != "";

        // 互斥检查
        if (has_compile_db && has_scan_dir) {
            std::cerr << "Error: --compile-db and --scan-dir are mutually exclusive\n";
            result.should_exit = true;
            result.exit_code = 1;
            return result;
        }

        // 收集额外 include 路径 (从 remaining 中提取 -I 参数)
        std::vector<std::string> extra_includes;
        if (args.hasOption("include")) {
            extra_includes.push_back(args.getOption("include"));
        }

        std::string output_dir = args.getOption("output-dir", "./c2cuda_out");

        if (has_compile_db) {
            job.mode = TranslateMode::CompileDB;
            if (!buildFromCompileDB(job, args.getOption("compile-db"),
                                    output_dir, extra_includes)) {
                result.should_exit = true;
                result.exit_code = 1;
                return result;
            }
            job.extra_rose_args = extractRoseArgs(args.remaining);
        } else if (has_scan_dir) {
            job.mode = TranslateMode::ScanDir;
            std::vector<std::string> excludes;
            if (args.hasOption("exclude")) {
                excludes.push_back(args.getOption("exclude"));
            }
            if (!buildFromScanDir(job, args.getOption("scan-dir"),
                                  output_dir, extra_includes, excludes)) {
                result.should_exit = true;
                result.exit_code = 1;
                return result;
            }
            job.extra_rose_args = extractRoseArgs(args.remaining);
        } else {
            job.mode = TranslateMode::SingleFile;
            // 单文件模式: remaining 原样传给 ROSE, 不需要构造 job 的其他字段
        }

        return result;
    }

private:
    static CmdLineParser createParser() {
        CmdLineParser parser;
        parser.addFlag("help", "h", "Show help message")
              .addFlag("verbose", "v", "Enable verbose logging")
              .addOption("compile-db", "p", "Path to compile_commands.json")
              .addOption("scan-dir", "s", "Recursively scan directory for .c files")
              .addOption("include", "I", "Extra include path")
              // .addOption("output-dir", "O", "Output directory for multi-file mode", "./c2cuda_out")
              .addOption("exclude", "e", "Exclude file glob pattern");
        return parser;
    }

    // 从 compile_commands.json 构建 job
    static bool buildFromCompileDB(TranslateJob& job,
                                   const std::string& db_path,
                                   const std::string& output_dir,
                                   const std::vector<std::string>& extra_includes) {
        CompileCommandsDB db;
        if (!db.load(db_path)) {
            std::cerr << "Error: failed to load " << db_path << "\n";
            return false;
        }

        // 过滤 .c 文件, 合并选项
        std::unordered_set<std::string> seen_includes;
        std::unordered_map<std::string, std::string> seen_defines; // name -> full define

        for (const auto& cmd : db.getCommands()) {
            if (!isCFile(cmd.file)) continue;
            job.source_files.push_back(cmd.getAbsoluteFile());

            for (const auto& inc : cmd.getIncludePaths()) {
                if (seen_includes.insert(inc).second) {
                    job.include_paths.push_back(inc);
                }
            }
            for (const auto& def : cmd.getDefines()) {
                std::string name = def.substr(0, def.find('='));
                auto it = seen_defines.find(name);
                if (it == seen_defines.end()) {
                    seen_defines[name] = def;
                    job.defines.push_back(def);
                } else if (it->second != def) {
                    std::cerr << "Warning: conflicting define -D" << def
                              << " (keeping -D" << it->second << ")\n";
                }
            }
        }

        if (job.source_files.empty()) {
            std::cerr << "Warning: no .c files found in " << db_path << "\n";
            return false;
        }

        // 追加用户额外 include
        for (const auto& inc : extra_includes) {
            if (seen_includes.insert(inc).second) {
                job.include_paths.push_back(inc);
            }
        }

        job.output_dir = output_dir;
        job.base_dir = computeCommonPrefix(job.source_files);
        return true;
    }

    // 从目录扫描构建 job
    static bool buildFromScanDir(TranslateJob& job,
                                 const std::string& scan_dir,
                                 const std::string& output_dir,
                                 const std::vector<std::string>& extra_includes,
                                 const std::vector<std::string>& excludes) {
        if (!fs::is_directory(scan_dir)) {
            std::cerr << "Error: " << scan_dir << " is not a directory\n";
            return false;
        }

        fs::path abs_scan = fs::absolute(scan_dir);

        for (const auto& entry : fs::recursive_directory_iterator(abs_scan)) {
            if (!entry.is_regular_file()) continue;
            std::string path = entry.path().string();
            if (!isCFile(path)) continue;
            if (matchesExclude(path, excludes)) continue;
            job.source_files.push_back(path);
        }

        if (job.source_files.empty()) {
            std::cerr << "Warning: no .c files found in " << scan_dir << "\n";
            return false;
        }

        job.base_dir = abs_scan.string();
        job.output_dir = output_dir;

        // include: 扫描目录本身 + 用户额外指定
        job.include_paths.push_back(abs_scan.string());
        for (const auto& inc : extra_includes) {
            job.include_paths.push_back(inc);
        }

        return true;
    }

    // 判断文件是否为 .c 文件
    static bool isCFile(const std::string& path) {
        if (path.size() < 2) return false;
        return path.compare(path.size() - 2, 2, ".c") == 0;
    }

    // 简单的排除模式匹配 (子串匹配)
    static bool matchesExclude(const std::string& path,
                               const std::vector<std::string>& excludes) {
        for (const auto& pattern : excludes) {
            if (path.find(pattern) != std::string::npos) return true;
        }
        return false;
    }

    // 计算多个路径的最长公共目录前缀
    static std::string computeCommonPrefix(const std::vector<std::string>& paths) {
        if (paths.empty()) return ".";
        if (paths.size() == 1) {
            return fs::path(paths[0]).parent_path().string();
        }

        fs::path common = fs::path(paths[0]).parent_path();
        for (size_t i = 1; i < paths.size(); ++i) {
            fs::path dir = fs::path(paths[i]).parent_path();
            // 逐级缩短 common 直到它是 dir 的前缀
            while (!isPrefix(common, dir)) {
                common = common.parent_path();
                if (common.empty()) return "/";
            }
        }
        return common.string();
    }

    static bool isPrefix(const fs::path& prefix, const fs::path& path) {
        auto it1 = prefix.begin();
        auto it2 = path.begin();
        for (; it1 != prefix.end(); ++it1, ++it2) {
            if (it2 == path.end()) return false;
            if (*it1 != *it2) return false;
        }
        return true;
    }

    // 从 remaining 中提取非源文件的参数作为 ROSE 额外参数
    // (跳过 argv[0] 和 .c 文件)
    static std::vector<std::string> extractRoseArgs(const std::vector<std::string>& remaining) {
        std::vector<std::string> rose_args;
        for (size_t i = 1; i < remaining.size(); ++i) {
            if (!isCFile(remaining[i])) {
                rose_args.push_back(remaining[i]);
            }
        }
        return rose_args;
    }
};

} // namespace c2cuda

#endif // __UTILS_TRANSLATE_JOB_H__
