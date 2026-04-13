#include <string>

enum class DeclKind
{
    Unknown,
    Function, // 比较签名
    Variable, // 比较作用域、名字
    Typedef,  // 比较作用域、名字
    Record,   // struct / union / class 比较作用域、名字
    Enum      // 比较作用域、名字
};

class DeclarationInfo
{
public:
    DeclKind kind = DeclKind::Unknown;
    std::string name;
    std::string signature; // For function
    std::string scope;
    bool needDefinition = false; // For Record
    std::string hFile;           // 声明所在文件（不一定真是 .h，也可能是源文件）
    SgDeclarationStatement *declNode = nullptr; // ⭐关键
};

std::vector<DeclarationInfo> collectDeclarationsForFunction(SgFunctionDefinition *def);
void addDeclaration(const DeclarationInfo &decl, SgStatement *callSite);