#pragma once

#include <rose.h>
#include <string>
#include <unordered_map>
#include <any>
#include <iostream>


namespace c2cuda {

enum class PassResult {  };

// ============================================================
// PassContext: pass 之间传递数据的共享上下文
// ============================================================
class PassContext {
public:
    // 存入数据
    template <typename T>
    void set(const std::string& key, T&& value) {
        data_[key] = std::forward<T>(value);
    }
 
    // 取出数据
    template <typename T>
    T get(const std::string& key) const {
        auto it = data_.find(key);
        if (it == data_.end()) {
            throw std::runtime_error("PassContext: key not found: " + key);
        }
        return std::any_cast<T>(it->second);
    }

    // 取出数据 ref
    template <typename T>
    const T& getRef(const std::string& key) const {
        auto it = data_.find(key);
        if (it == data_.end()) {
            throw std::runtime_error("PassContext: key not found: " + key);
        }
        return std::any_cast<const T&>(it->second);
    }
 
    // 安全取出（带默认值）
    template <typename T>
    T getOr(const std::string& key, const T& defaultVal) const {
        auto it = data_.find(key);
        if (it == data_.end()) return defaultVal;
        try {
            return std::any_cast<T>(it->second);
        } catch (...) {
            return defaultVal;
        }
    }
 
    bool has(const std::string& key) const {
        return data_.find(key) != data_.end();
    }
 
    void remove(const std::string& key) {
        data_.erase(key);
    }
 
    void clear() {
        data_.clear();
    }
 
private:
    std::unordered_map<std::string, std::any> data_;
};
//
// ============================================================
// Pass: 所有 pass 的抽象基类
// ============================================================
class Pass {
public:
    explicit Pass(const std::string& name) : name_(name) {}
    virtual ~Pass() = default;
 
    // --- 核心接口 ---
 
    // 运行 pass，返回 true 表示修改了 AST
    virtual bool run(SgProject* project, PassContext& ctx) = 0;
 
    // --- 可选重写 ---
 
    // pass 运行前的初始化
    virtual void initialize(SgProject* /*project*/, PassContext& /*ctx*/) {}
 
    // pass 运行后的清理
    virtual void finalize(SgProject* /*project*/, PassContext& /*ctx*/) {}
 
    // --- 属性 ---
 
    const std::string& getName() const { return name_; }
 
    void setVerbose(bool v) { verbose_ = v; }
    bool isVerbose() const { return verbose_; }
 
protected:
    void log(const std::string& msg) const {
        if (verbose_) {
            std::cout << "[" << name_ << "] " << msg << std::endl;
        }
    }
 
private:
    std::string name_;
    bool verbose_ = false;
};
 
// The Pass runner
struct TranslateJob;
PassContext plan1( SgProject* project, const TranslateJob& job, bool isVerbose=false );
PassContext plan2(SgProject *project, const TranslateJob &job, bool isVerbose);

} // namespace Pass
