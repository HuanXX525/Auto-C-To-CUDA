#include "DEBUG/debugTool.h"
static bool isRealProjectNode(SgNode *n);

/**
 * 检查所有节点的父节点，如果父节点为空则打印相关信息。
 */
void checkParents(SgNode *root)
{
    Rose_STL_Container<SgNode *> nodes =
        NodeQuery::querySubTree(root, V_SgNode);

    for (SgNode *n : nodes)
    {
        if (!n || !isRealProjectNode(n))
            continue;

        SgNode *parent = n->get_parent();

        if (!parent)
        {
            std::cout
                << "[NULL PARENT] "
                << n->class_name()
                << " @ " << n
                << std::endl;
        }
    }
}

void checkParentConsistency(SgNode *root)
{
    Rose_STL_Container<SgNode *> nodes =
        NodeQuery::querySubTree(root, V_SgNode);

    for (SgNode *node : nodes)
    {
        if (!node)
            continue;

        SgNode *parent = node->get_parent();

        if (!parent)
            continue;

        bool found = false;

        const SgNodePtrList &children = parent->get_traversalSuccessorContainer();

        for (SgNode *child : children)
        {
            if (child == node)
            {
                found = true;
                break;
            }
        }

        if (!found)
        {
            std::cout
                << "\n[BROKEN PARENT-CHILD]\n"
                << "NODE   : " << node->class_name()
                << " @ " << node << "\n"
                << "CODE   : ";

            try
            {
                std::cout << node->unparseToString();
            }
            catch (...)
            {
                std::cout << "<unparse failed>";
            }

            std::cout
                << "\nPARENT : " << parent->class_name()
                << " @ " << parent
                << "\n";

            std::cout
                << "Parent does NOT contain this node "
                << "in traversalSuccessorContainer\n";
        }
    }
}

static bool isRealProjectNode(SgNode* n)
{
    if (!n)
        return false;

    Sg_File_Info* fi = n->get_file_info();

    if (!fi)
        return false;

    // 编译器生成节点
    if (fi->isCompilerGenerated())
        return false;

    // transformation 生成节点
    if (fi->isTransformation())
        return false;

    // 无文件信息
    std::string file = fi->get_filenameString();

    if (file.empty())
        return false;

    // builtin/type system
    if (file == "NULL_FILE")
        return false;

    return true;
}


void checkVarRefs(SgNode *root)
{
    Rose_STL_Container<SgNode*> vars =
        NodeQuery::querySubTree(root, V_SgVarRefExp);

    for (auto n : vars)
    {
        SgVarRefExp *v = isSgVarRefExp(n);
        if (!v) continue;

        auto sym = v->get_symbol();

        if (!sym)
        {
            std::cout << "[NULL SYMBOL] "
                      << v->unparseToString()
                      << " @ " << v
                      << std::endl;
            continue;
        }

        auto decl = sym->get_declaration();

        if (!decl)
        {
            std::cout << "[NULL DECL] "
                      << v->unparseToString()
                      << std::endl;
            continue;
        }

        auto scope = decl->get_scope();

        if (!scope)
        {
            std::cout << "[NULL SCOPE] "
                      << decl->get_name()
                      << std::endl;
            continue;
        }

        if (!isSgGlobal(scope) &&
            !isSgBasicBlock(scope) &&
            !isSgFunctionDefinition(scope) &&
            !isSgForStatement(scope))
        {
            std::cout << "[WEIRD SCOPE] "
                      << decl->get_name()
                      << " : "
                      << scope->class_name()
                      << std::endl;
        }
    }
}