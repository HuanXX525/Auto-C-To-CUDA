#include <rose.h>
#include <CallGraph.h>
#include <iostream>
#include <vector>
#include <map>
#include <queue>
#include <set>

using namespace std;

// 官方推荐的严格过滤器：排除系统库和编译器生成代码 [cite: 55, 63]
struct StrictUserOnlyPredicate
{
    bool operator()(SgFunctionDeclaration *decl) const
    {
        if (!decl)
            return false;
        Sg_File_Info *info = decl->get_file_info();
        if (info->isCompilerGenerated())
            return false; // [cite: 63]

        string filename = info->get_filename();
        // 核心逻辑：剔除标准库路径 [cite: 103, 105]
        if (filename.find("/usr/") != string::npos ||
            filename.find("include") != string::npos)
        {
            return false;
        }
        return true;
    }
};

map<std::string, int> performTopologicalSort(CallGraphBuilder &CGBuilder)
{
    SgIncidenceDirectedGraph *graph = CGBuilder.getGraph(); // [cite: 145]
    // 获取声明到图节点的映射
    boost::unordered_map<SgFunctionDeclaration *, SgGraphNode *> &nodeMap = CGBuilder.getGraphNodesMapping();

    map<SgGraphNode *, int> inDegree;
    queue<SgGraphNode *> zeroInDegreeQueue;
    vector<SgFunctionDeclaration *> sortedFunctions;
    map<std::string, int> sortedFunc;
    set<SgGraphNode *> userNodes;

    // 1. 初始化入度：只统计用户节点
    for (auto const &[decl, node] : nodeMap)
    {
        userNodes.insert(node);
    }

    for (SgGraphNode *node : userNodes)
    {
        set<SgDirectedGraphEdge *> inEdges = graph->computeEdgeSetIn(node);
        int degree = 0;
        for (auto *edge : inEdges)
        {
            // 只有当调用者也是用户函数，且不是自环时计入入度 [cite: 7]
            if (edge->get_from() != node && userNodes.count(edge->get_from()))
            {
                degree++;
            }
        }
        inDegree[node] = degree;
        if (degree == 0)
            zeroInDegreeQueue.push(node);
    }

    // 2. Kahn 算法逻辑 [cite: 5, 6]
    set<SgGraphNode *> processed;
    while (!zeroInDegreeQueue.empty())
    {
        SgGraphNode *curr = zeroInDegreeQueue.front();
        zeroInDegreeQueue.pop();
        processed.insert(curr);

        SgFunctionDeclaration *decl = isSgFunctionDeclaration(curr->get_SgNode());
        if (decl)
            sortedFunctions.push_back(decl);

        vector<SgGraphNode *> successors;
        graph->getSuccessors(curr, successors);
        for (SgGraphNode *next : successors)
        {
            if (userNodes.count(next))
            {
                inDegree[next]--;
                if (inDegree[next] == 0)
                    zeroInDegreeQueue.push(next);
            }
        }
    }

    // 3. 输出排序结果
    // cout << ">>> Optimized Function Processing Order (Top-Down):" << endl;
    int order = 0;
    for (auto *decl : sortedFunctions)
    {
        // cout << "  [Order] " << decl->get_name().getString() << endl;
        sortedFunc.insert(std::pair<std::string, int>(decl->get_name().getString(), order++));
    }

    // 检查递归（环路）
    for (SgGraphNode *node : userNodes)
    {
        if (processed.find(node) == processed.end())
        {
            SgFunctionDeclaration *d = isSgFunctionDeclaration(node->get_SgNode());
            cerr << "  [Warning] Cycle detected involving: " << d->get_name().getString() << endl;
        }
    }

    return sortedFunc;
}

int main(int argc, char **argv)
{
    SgProject *project = frontend(argc, argv); // [cite: 119]

    // 构建调用图 [cite: 128, 130]
    CallGraphBuilder CGBuilder(project);
    CGBuilder.buildCallGraph(StrictUserOnlyPredicate());

    // 执行拓扑排序
    performTopologicalSort(CGBuilder);

    // 可视化输出 [cite: 145]
    AstDOTGeneration dotgen;
    dotgen.writeIncidenceGraphToDOTFile(CGBuilder.getGraph(), "callGraph.dot");

    return 0;
}