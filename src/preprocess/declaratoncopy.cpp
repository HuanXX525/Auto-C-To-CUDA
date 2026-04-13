#include <rose.h>
#include <AstSimpleProcessing.h>

#include <vector>
#include <set>
#include <sstream>
#include <iostream>

#include "preprocess/declarationcopy.h"
#include "logger.h"

static std::string declKindToString(DeclKind k)
{
    switch (k)
    {
    case DeclKind::Function:
        return "Function";
    case DeclKind::Variable:
        return "Variable";
    case DeclKind::Typedef:
        return "Typedef";
    case DeclKind::Record:
        return "Record";
    case DeclKind::Enum:
        return "Enum";
    default:
        return "Unknown";
    }
}

// 获取该节点所在文件名
static std::string getFilenameOfNode(SgNode *n)
{
    if (!n)
        return "";
    Sg_File_Info *fi = n->get_file_info();
    if (!fi)
        return "";
    return fi->get_filenameString();
}

// 获取声明所在作用域名
static std::string getScopeName(SgDeclarationStatement *decl)
{
    if (!decl)
        return "";

    SgScopeStatement *scope = decl->get_scope();
    if (!scope)
        return "";

    // 全局作用域
    if (isSgGlobal(scope))
        return "::";

    // 命名空间
    if (auto ns = isSgNamespaceDefinitionStatement(scope))
        return ns->get_qualified_name().getString();

    // 类作用域
    if (auto cd = isSgClassDefinition(scope))
    {
        SgClassDeclaration *clsDecl = cd->get_declaration();
        if (clsDecl)
            return clsDecl->get_qualified_name().getString();
    }

    // 函数作用域
    if (auto fd = isSgFunctionDefinition(scope))
    {
        SgFunctionDeclaration *fdecl = fd->get_declaration();
        if (fdecl)
            return fdecl->get_qualified_name().getString();
    }

    // 兜底
    return scope->class_name();
}

// 获取函数签名
static std::string getFunctionSignature(SgFunctionDeclaration *fdecl)
{
    if (!fdecl)
        return "";

    std::ostringstream oss;
    oss << fdecl->get_qualified_name().getString() << "(";

    SgFunctionParameterList *params = fdecl->get_parameterList();
    if (params)
    {
        const SgInitializedNamePtrList &args = params->get_args();
        for (size_t i = 0; i < args.size(); ++i)
        {
            SgInitializedName *arg = args[i];
            if (!arg)
                continue;

            SgType *ty = arg->get_type();
            if (ty)
                oss << ty->unparseToString();
            else
                oss << "<null-type>";

            if (i + 1 < args.size())
                oss << ", ";
        }
    }

    oss << ")";

    SgType *retTy = fdecl->get_orig_return_type();
    if (retTy)
        oss << " -> " << retTy->unparseToString();

    return oss.str();
}

// 拼接唯一标识
static std::string makeUniqueKey(const DeclarationInfo &info)
{
    std::ostringstream oss;
    oss << declKindToString(info.kind) << " | "
        << info.scope << " | "
        << info.name << " | "
        << info.signature << " | "
        << info.needDefinition;
    return oss.str();
}

// 是不是头文件
static bool isHeaderLikeFile(const std::string &f)
{
    return f.size() >= 2 &&
           (f.rfind(".h") == f.size() - 2 ||
            f.rfind(".hh") == f.size() - 3 ||
            f.rfind(".hpp") == f.size() - 4 ||
            f.rfind(".hxx") == f.size() - 4);
}

// 往容器中加声明信息 去重
static void addDeclInfo(std::vector<DeclarationInfo> &out,
                        std::set<std::string> &seen,
                        const DeclarationInfo &info)
{
    std::string key = makeUniqueKey(info);
    if (seen.insert(key).second)
        out.push_back(info);
}

static void addFunctionDecl(std::vector<DeclarationInfo> &out,
                            std::set<std::string> &seen,
                            SgFunctionDeclaration *fdecl)
{
    if (!fdecl)
        return;

    DeclarationInfo info;
    info.kind = DeclKind::Function;
    info.name = fdecl->get_name().getString();
    info.signature = getFunctionSignature(fdecl);
    info.scope = getScopeName(fdecl);
    info.hFile = getFilenameOfNode(fdecl);
    info.needDefinition = false; // 这里只记录声明层面；是否要函数定义你后续可单独判

    addDeclInfo(out, seen, info);
}

static void addVariableDecl(std::vector<DeclarationInfo> &out,
                            std::set<std::string> &seen,
                            SgVariableDeclaration *vdecl)
{
    if (!vdecl)
        return;

    const SgInitializedNamePtrList &vars = vdecl->get_variables();
    for (SgInitializedName *var : vars)
    {
        if (!var)
            continue;

        DeclarationInfo info;
        info.kind = DeclKind::Variable;
        info.name = var->get_name().getString();
        info.scope = getScopeName(vdecl);
        info.hFile = getFilenameOfNode(vdecl);
        info.needDefinition = false;

        addDeclInfo(out, seen, info);
    }
}

static void addTypedefDecl(std::vector<DeclarationInfo> &out,
                           std::set<std::string> &seen,
                           SgTypedefDeclaration *tdecl)
{
    if (!tdecl)
        return;

    DeclarationInfo info;
    info.kind = DeclKind::Typedef;
    info.name = tdecl->get_name().getString();
    info.scope = getScopeName(tdecl);
    info.hFile = getFilenameOfNode(tdecl);
    info.needDefinition = false;

    addDeclInfo(out, seen, info);
}

static void addClassDecl(std::vector<DeclarationInfo> &out,
                         std::set<std::string> &seen,
                         SgClassDeclaration *cdecl,
                         bool needDefinition)
{
    if (!cdecl)
        return;

    DeclarationInfo info;
    info.kind = DeclKind::Record;
    info.name = cdecl->get_name().getString();
    info.scope = getScopeName(cdecl);
    info.hFile = getFilenameOfNode(cdecl);
    info.needDefinition = needDefinition;

    addDeclInfo(out, seen, info);
}

static void addEnumDecl(std::vector<DeclarationInfo> &out,
                        std::set<std::string> &seen,
                        SgEnumDeclaration *edecl)
{
    if (!edecl)
        return;

    DeclarationInfo info;
    info.kind = DeclKind::Enum;
    info.name = edecl->get_name().getString();
    info.scope = getScopeName(edecl);
    info.hFile = getFilenameOfNode(edecl);
    info.needDefinition = true; // enum 一般要完整定义才能用枚举值

    addDeclInfo(out, seen, info);
}

static void collectFromType(SgType *ty,
                            std::vector<DeclarationInfo> &out,
                            std::set<std::string> &seen);

static void collectFromInitializedName(SgInitializedName *initName,
                                       std::vector<DeclarationInfo> &out,
                                       std::set<std::string> &seen)
{
    if (!initName)
        return;
    collectFromType(initName->get_type(), out, seen);
}

static void collectFromType(SgType *ty,
                            std::vector<DeclarationInfo> &out,
                            std::set<std::string> &seen)
{
    if (!ty)
        return;

    ty = ty->stripType(SgType::STRIP_MODIFIER_TYPE |
                       SgType::STRIP_REFERENCE_TYPE |
                       SgType::STRIP_TYPEDEF_TYPE);

    if (!ty)
        return;

    if (auto tdef = isSgTypedefType(ty))
    {
        SgTypedefDeclaration *decl = isSgTypedefDeclaration(tdef->get_declaration());
        addTypedefDecl(out, seen, decl);
        return;
    }

    if (auto cty = isSgClassType(ty))
    {
        SgClassDeclaration *decl = isSgClassDeclaration(cty->get_declaration());
        addClassDecl(out, seen, decl, true);
        return;
    }

    if (auto ety = isSgEnumType(ty))
    {
        SgEnumDeclaration *decl = isSgEnumDeclaration(ety->get_declaration());
        addEnumDecl(out, seen, decl);
        return;
    }

    if (auto pty = isSgPointerType(ty))
    {
        collectFromType(pty->get_base_type(), out, seen);
        return;
    }

    if (auto aty = isSgArrayType(ty))
    {
        collectFromType(aty->get_base_type(), out, seen);
        return;
    }

    if (auto mty = isSgModifierType(ty))
    {
        collectFromType(mty->get_base_type(), out, seen);
        return;
    }

    if (auto fty = isSgFunctionType(ty))
    {
        collectFromType(fty->get_return_type(), out, seen);
        SgTypePtrList &args = fty->get_argument_list()->get_arguments();
        for (SgType *argTy : args)
            collectFromType(argTy, out, seen);
        return;
    }
}

class DeclCollector : public AstSimpleProcessing
{
public:
    explicit DeclCollector(SgFunctionDefinition *funcDef)
        : m_funcDef(funcDef)
    {
    }

    std::vector<DeclarationInfo> result;

    void visit(SgNode *node) override
    {
        if (!node)
            return;

        // 1. 变量引用
        if (auto varRef = isSgVarRefExp(node))
        {
            SgVariableSymbol *sym = varRef->get_symbol();
            if (sym)
            {
                SgInitializedName *initName = sym->get_declaration();
                if (initName)
                {
                    SgVariableDeclaration *vdecl =
                        isSgVariableDeclaration(initName->get_declaration());
                    addVariableDecl(result, seen, vdecl);

                    // 变量类型也可能引入 typedef / struct / enum
                    collectFromInitializedName(initName, result, seen);
                }
            }
            return;
        }

        // 2. 函数引用（直接调用）
        if (auto fnRef = isSgFunctionRefExp(node))
        {
            SgFunctionSymbol *sym = fnRef->get_symbol();
            if (sym)
            {
                SgFunctionDeclaration *fdecl = sym->get_declaration();
                addFunctionDecl(result, seen, fdecl);

                if (fdecl)
                {
                    // 参数和返回类型依赖
                    if (auto ftype = fdecl->get_type())
                        collectFromType(ftype, result, seen);
                }
            }
            return;
        }

        // 3. 成员函数引用
        if (auto memFnRef = isSgMemberFunctionRefExp(node))
        {
            SgMemberFunctionSymbol *sym = memFnRef->get_symbol();
            if (sym)
            {
                SgMemberFunctionDeclaration *fdecl = sym->get_declaration();
                addFunctionDecl(result, seen, fdecl);

                if (fdecl)
                {
                    if (auto ftype = fdecl->get_type())
                        collectFromType(ftype, result, seen);

                    // 所属类
                    SgClassDefinition *clsDef = isSgClassDefinition(fdecl->get_class_scope());
                    if (clsDef)
                    {
                        SgClassDeclaration *clsDecl = clsDef->get_declaration();
                        addClassDecl(result, seen, clsDecl, true);
                    }
                }
            }
            return;
        }

        // 4. 显式使用到某种类型
        if (auto varDecl = isSgVariableDeclaration(node))
        {
            const SgInitializedNamePtrList &vars = varDecl->get_variables();
            for (SgInitializedName *v : vars)
                collectFromInitializedName(v, result, seen);
            return;
        }

        if (auto castExp = isSgCastExp(node))
        {
            collectFromType(castExp->get_type(), result, seen);
            return;
        }

        if (auto sizeofOp = isSgSizeOfOp(node))
        {
            collectFromType(sizeofOp->get_operand_type(), result, seen);
            return;
        }

        // 5. 点/箭头访问，补一下所属类型
        if (auto dotExp = isSgDotExp(node))
        {
            collectFromType(dotExp->get_lhs_operand()->get_type(), result, seen);
            return;
        }

        if (auto arrowExp = isSgArrowExp(node))
        {
            collectFromType(arrowExp->get_lhs_operand()->get_type(), result, seen);
            return;
        }
    }

private:
    SgFunctionDefinition *m_funcDef = nullptr;
    std::set<std::string> seen;
};

std::vector<DeclarationInfo> collectDeclarationsForFunction(SgFunctionDefinition *def)
{
    if (!def)
        return {};

    DeclCollector collector(def);
    collector.traverse(def->get_body(), preorder);
    for(auto r : collector.result){
        log_debug("%s", makeUniqueKey(r).c_str());
    }
    return collector.result;
}