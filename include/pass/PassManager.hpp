#pragma once

#include "pass/Pass.hpp"
#include <chrono>
#include <memory>

namespace c2cuda {

class PassManager {
public:

    PassManager& add(std::unique_ptr<Pass> p) {
        this->passes_.push_back(std::move(p));
        return *this;
    }

    template<typename T, typename... Args> 
    PassManager& add(Args&&... args) {
        this->passes_.push_back(std::make_unique<T>(std::forward<Args>(args)...));
        return *this;
    }

    // run all passes
    bool run(SgProject* project) {
        bool astModified = false;

        for (auto& p : passes_) {
            if ( verbose_ ) { 
                std::cerr <<  "=================== Running pass: " << p->getName() << "====================\n";
            }

            p->setVerbose(verbose_);
            p->initialize(project, ctx_);

            auto start = std::chrono::high_resolution_clock::now();
            bool changed = p->run(project, ctx_);
            auto end = std::chrono::high_resolution_clock::now();

            p->finalize(project, ctx_);

            if(changed) astModified = true;

            if ( verbose_ ) {
                auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
                std::cout << "  -> " << (changed ? "AST modified" : "no change")
                                          << " (" << ms << " ms)" << std::endl;
            }
        }
        return astModified;
    }

    void setVerbose(bool v) { this->verbose_ = v; }

    PassContext& getContext() { return ctx_; }
    const PassContext& getContext() const { return ctx_; }

    size_t size() const { return passes_.size(); }

private:
    std::vector<std::unique_ptr<Pass>> passes_;
    PassContext ctx_;
    bool verbose_ = false;
};

} // namespace Pass
