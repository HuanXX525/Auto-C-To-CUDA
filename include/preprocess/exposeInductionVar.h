#include <rose.h>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include "preprocess/deadCodeElim.h"

int exposeInductionVariablesInFor(SgForStatement *forStmt);
class ForIVExposureTraversal : public AstSimpleProcessing
{
public:
    void visit(SgNode* node) override
    {
        if (auto forStmt = isSgForStatement(node))
        {
            int n = exposeInductionVariablesInFor(forStmt);
            // 死代码消除
            int removed = DeadCodeEliminator::eliminate(forStmt);
            std::cout << "[DCE] removed "
                      << removed
                      << " dead statements"
                      << std::endl;
            // 死代码消除
            if (n > 0)
            {
                std::cout << "[IVExposure] replaced "
                          << n
                          << " var refs in loop: "
                          << forStmt->unparseToString()
                          << std::endl;
            }
        }
    }
};
