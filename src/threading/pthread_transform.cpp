#include "threading/pthread_transform.hpp"
#include "logger.h"
#include <fstream>
#include <sstream>
#include <vector>

namespace c2cuda {

/* ==================================================================
 *  Embedded C preamble: JSON config loader + task config struct
 *  This text is inserted verbatim at the top of the output file.
 * ================================================================== */
static const char *kEmbeddedPreamble = R"(
/*** c2cuda: pthread task config & JSON loader ***/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifndef C2CUDA_BATCH_SIZE
#define C2CUDA_BATCH_SIZE 64
#endif

#ifndef C2CUDA_CONFIG_FILE
#define C2CUDA_CONFIG_FILE "c2cuda_config.json"
#endif

typedef struct {
    int argc;
    char **argv;
} _c2cuda_task_config_t;

static _c2cuda_task_config_t _c2cuda_configs[1024];
static int _c2cuda_num_tasks = 0;

/* Minimal JSON array-of-arrays-of-strings parser */
static int _c2cuda_load_config(const char *path,
                                _c2cuda_task_config_t *configs,
                                int *num_tasks) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "[c2cuda] ERROR: cannot open config '%s'\n", path);
        return 0;
    }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len <= 0) { fclose(f); return 0; }

    char *buf = (char*)malloc((size_t)len + 1);
    if (!buf) { fclose(f); return 0; }
    size_t r = fread(buf, 1, (size_t)len, f);
    fclose(f);
    buf[r] = '\0';

    char *p = buf;
    while (*p && (unsigned char)*p <= ' ') p++;
    if (*p++ != '[') { free(buf); return 0; }

    int tidx = 0;
    while (*p && *p != ']' && tidx < 1024) {
        while (*p && (unsigned char)*p <= ' ') p++;
        if (*p++ != '[') break;

        int argc = 0;
        {
            char *q = p;
            while (*q && *q != ']') {
                while (*q && (unsigned char)*q <= ' ') q++;
                if (*q == '"') { argc++; q++; while (*q && *q != '"') { if (*q == '\\') q++; q++; } if (*q) q++; }
                else if (*q == ',' || *q == ']') { if (*q == ',') q++; }
                else break;
            }
        }
        if (argc == 0) {
            while (*p && *p != ']') p++;
            if (*p) p++;
            continue;
        }

        configs[tidx].argc = argc;
        configs[tidx].argv = (char**)malloc(sizeof(char*) * (size_t)argc);
        int ai = 0;
        while (*p && *p != ']' && ai < argc) {
            while (*p && (unsigned char)*p <= ' ') p++;
            if (*p == '"') {
                p++;
                const char *start = p;
                while (*p && *p != '"') { if (*p == '\\') p++; p++; }
                int slen = (int)(p - start);
                configs[tidx].argv[ai] = (char*)malloc((size_t)slen + 1);
                if (slen > 0) memcpy(configs[tidx].argv[ai], start, (size_t)slen);
                configs[tidx].argv[ai][slen] = '\0';
                ai++;
                if (*p) p++;
            } else if (*p == ',') { p++; }
            else break;
        }
        while (*p && *p != ']') p++;
        if (*p) p++;
        tidx++;
        while (*p && (unsigned char)*p <= ' ') p++;
        if (*p == ',') p++;
    }
    *num_tasks = tidx;
    free(buf);
    return 1;
}
)";

/* ==================================================================
 *  Helper: read file content into string
 * ================================================================== */
static std::string readFile(const std::string &path) {
    std::ifstream ifs(path);
    if (!ifs) {
        log_info("pthread_transform: cannot open %s", path.c_str());
        return "";
    }
    std::stringstream ss;
    ss << ifs.rdbuf();
    return ss.str();
}

/* ==================================================================
 *  Helper: find the 'main' function definition in a source file
 * ================================================================== */
static SgFunctionDefinition *findMain(SgSourceFile *file) {
    Rose_STL_Container<SgNode *> funcs =
        NodeQuery::querySubTree(file, V_SgFunctionDefinition);
    for (auto node : funcs) {
        SgFunctionDefinition *def = isSgFunctionDefinition(node);
        if (!def) continue;
        std::string name =
            def->get_declaration()->get_name().getString();
        if (name == "main")
            return def;
    }
    return nullptr;
}

/* ==================================================================
 *  Helper: create the task function in global scope
 *    void* _c2cuda_pthread_task(void* arg);
 * ================================================================== */
static SgFunctionDeclaration *createTaskFunction(SgGlobal *globalScope,
                                                  SgFunctionDeclaration *mainDecl) {
    SgType *voidPtr = SageBuilder::buildPointerType(SageBuilder::buildVoidType());

    SgInitializedName *argParam =
        SageBuilder::buildInitializedName("arg", voidPtr);
    SgFunctionParameterList *paramList =
        SageBuilder::buildFunctionParameterList();
    SageInterface::appendArg(paramList, argParam);

    SgFunctionDeclaration *taskFn =
        SageBuilder::buildDefiningFunctionDeclaration(
            "_c2cuda_pthread_task", voidPtr, paramList, globalScope);
    taskFn->get_functionModifier().setDefault();

    SageInterface::insertStatementBefore(mainDecl, taskFn);

    return taskFn;
}

/* ==================================================================
 *  Move all statements from mainBody to taskBody,
 *  then insert preamble text (argc/argv restore) in taskBody
 * ================================================================== */
static void moveBodyAndInjectPreamble(SgFunctionDefinition *mainDef,
                                       SgFunctionDefinition *taskDef) {
    SgBasicBlock *mainBody = mainDef->get_body();
    SgBasicBlock *taskBody = taskDef->get_body();

    /* Move every statement from mainBody to taskBody */
    while (!mainBody->get_statements().empty()) {
        SgStatement *stmt = mainBody->get_statements()[0];
        SageInterface::removeStatement(stmt);
        SageInterface::appendStatement(stmt, taskBody);
    }

    /* Inject preamble before the first moved statement */
    SgStatement *firstInTask =
        SageInterface::getFirstStatement(taskBody);
    if (firstInTask) {
        std::string preamble =
            "    int _c2cuda_tid = (int)(long)arg;\n"
            "    int argc = _c2cuda_configs[_c2cuda_tid].argc;\n"
            "    char **argv = _c2cuda_configs[_c2cuda_tid].argv;\n";
        SageInterface::addTextForUnparser(firstInTask, preamble,
            AstUnparseAttribute::e_before);
    }
}

/* ==================================================================
 *  Transform return statements in the task function:
 *    return expr;   →   return 0;   (NULL for void*)
 *    return;        →   return 0;
 * ================================================================== */
static void transformReturns(SgFunctionDefinition *taskDef) {
    Rose_STL_Container<SgNode *> returns =
        NodeQuery::querySubTree(taskDef, V_SgReturnStmt);
    for (auto node : returns) {
        SgReturnStmt *ret = isSgReturnStmt(node);
        if (!ret) continue;

        SgExpression *expr = ret->get_expression();
        if (expr) {
            /* Replace the existing expression with 0 (NULL) */
            SageInterface::replaceExpression(
                expr, SageBuilder::buildIntVal(0), true);
        } else {
            /* bare "return;" → "return 0;" */
            ret->set_expression(SageBuilder::buildIntVal(0));
        }
    }

    /* Ensure the last statement is "return 0;" */
    SgBasicBlock *body = taskDef->get_body();
    SgStatementPtrList &stmts = body->get_statements();
    if (!stmts.empty()) {
        SgReturnStmt *lastRet = isSgReturnStmt(stmts.back());
        if (!lastRet) {
            SageInterface::appendStatement(
                SageBuilder::buildReturnStmt(SageBuilder::buildIntVal(0)),
                body);
        }
    }
}

/* ==================================================================
 *  Rewrite main's body with pthreadManager batch-submit logic
 * ================================================================== */
static void rewriteMain(SgFunctionDefinition *mainDef) {
    SgBasicBlock *mainBody = mainDef->get_body();

    std::string newBody =
        "{\n"
        "    if (!_c2cuda_load_config(C2CUDA_CONFIG_FILE,\n"
        "                              _c2cuda_configs,\n"
        "                              &_c2cuda_num_tasks))\n"
        "        return 1;\n"
        "\n"
        "    int _c2cuda_n = (_c2cuda_num_tasks < C2CUDA_BATCH_SIZE)\n"
        "                        ? _c2cuda_num_tasks\n"
        "                        : C2CUDA_BATCH_SIZE;\n"
        "    if (_c2cuda_n < 1) return 1;\n"
        "\n"
        "    void *_c2cuda_args[1024];\n"
        "    for (int _c2cuda_i = 0; _c2cuda_i < _c2cuda_n; _c2cuda_i++)\n"
        "        _c2cuda_args[_c2cuda_i] = (void*)(long)_c2cuda_i;\n"
        "\n"
        "    thread_pool_execute(_c2cuda_pthread_task,\n"
        "                        _c2cuda_args, _c2cuda_n, 0);\n"
        "    return 0;\n"
        "}\n";

    SageInterface::addTextForUnparser(mainBody, newBody,
        AstUnparseAttribute::e_replace);
}

/* ==================================================================
 *  Embed thread_manager.h/.c and the preamble into the output file
 * ================================================================== */
static void embedPthreadSources(SgSourceFile *file,
                                 SgGlobal *globalScope) {
    SgStatement *anchor =
        SageInterface::getFirstStatement(globalScope);

    /* 1) Read and embed thread_manager.h */
    std::string tmH = readFile("pthreadManager/thread_manager.h");
    if (!tmH.empty()) {
        if (anchor)
            SageInterface::addTextForUnparser(anchor, tmH + "\n",
                AstUnparseAttribute::e_before);
        else
            SageInterface::addTextForUnparser(globalScope, tmH + "\n",
                AstUnparseAttribute::e_before);
    }

    /* 3) Read and embed thread_manager.c (strip its #include since the header is already inlined above) */
    std::string tmC = readFile("pthreadManager/thread_manager.c");
    if (!tmC.empty()) {
        /* Remove the #include "thread_manager.h" line — header is already inlined */
        size_t pos = tmC.find("#include \"thread_manager.h\"");
        if (pos != std::string::npos)
            tmC.erase(pos, strlen("#include \"thread_manager.h\""));
        if (anchor)
            SageInterface::addTextForUnparser(anchor, tmC + "\n",
                AstUnparseAttribute::e_before);
        else
            SageInterface::addTextForUnparser(globalScope, tmC + "\n",
                AstUnparseAttribute::e_before);
    }

    /* 4) Embed the task-config preamble (struct + JSON parser) */
    if (anchor)
        SageInterface::addTextForUnparser(anchor,
            kEmbeddedPreamble,
            AstUnparseAttribute::e_before);
    else
        SageInterface::addTextForUnparser(globalScope,
            kEmbeddedPreamble,
            AstUnparseAttribute::e_before);
}

/* ==================================================================
 *  Main entry point
 * ================================================================== */
void applyPthreadTransform(SgProject *project) {
    log_info("pthread_transform: applying pthread task extraction...");

    SgFilePtrList &files = project->get_fileList();
    for (size_t fi = 0; fi < files.size(); ++fi) {
        SgSourceFile *file = isSgSourceFile(files[fi]);
        if (!file) continue;

        SgGlobal *globalScope = file->get_globalScope();
        if (!globalScope) continue;

        /* 1) Find main */
        SgFunctionDefinition *mainDef = findMain(file);
        if (!mainDef) {
            log_info("pthread_transform: no main() in file %zu, skip", fi);
            continue;
        }
        log_info("pthread_transform: found main() in file %zu", fi);

        /* 2) Create the task function in global scope, before main() */
        SgFunctionDeclaration *mainDecl = mainDef->get_declaration();
        SgFunctionDeclaration *taskFn =
            createTaskFunction(globalScope, mainDecl);
        SgFunctionDefinition *taskDef = taskFn->get_definition();

        /* 3) Move main body → task function + inject argc/argv preamble */
        moveBodyAndInjectPreamble(mainDef, taskDef);

        /* 4) Fix variable references after moving */
        SageInterface::fixVariableReferences(taskFn);

        /* 5) Transform return statements */
        transformReturns(taskDef);

        /* 6) Rewrite main body with batch-execute code */
        rewriteMain(mainDef);
        SageInterface::fixVariableReferences(mainDef);

        /* 7) Embed pthreadManager sources + JSON loader */
        embedPthreadSources(file, globalScope);

        log_info("pthread_transform: done for file %zu", fi);
    }

    log_info("pthread_transform: all files processed");
}

} // namespace c2cuda
