/* Header file for preprocessing the input code */

#ifndef INPUT_PREPROC
#define INTUT_PREPROC
#include "rose.h"
#include <CallGraph.h>
#include "../parallel/parallel.hpp"

/* Function to convert while loops into for loops 
 
   Input: While loop nest
   Output: While loop if conversion is successful, NULL otherwise

   This handles cases of the following form:
       i = a;
       while(i<n)
       {
           <statements not involving updates to i or n>
	   i = i+1;
       }

   So, the index variable must be set RIGHT BEFORE the loop, the inner-statements must NOT INVOLVE UPDATES to iter/bound, and the INCREMENT IS THE LAST STATEMENT. 
*/
SgStatement * convertWhileToFor(SgWhileStmt *loop_nest);


/* Function to convert an imperfectly nested loop into a series of perfectly nested loops

   Input: Imperfectly nested loop nest
   Output: Vector of perfectly nested loops.  If an error occurs, return an empty vector.

   This function uses the graph definition in ../parallel/parallel.hpp to construct graphs of FLOW and ANTI dependencies.  
*/
std::vector<SgStatement*> convertImperfToPerf(SgForStatement *imperf_loop_nest);


/* Function to determine whether a loop nest is perfectly nested
 
   Input: Loop nest
   Output: true if perfectly nested, false otherwise
*/
bool isPerfectlyNested(SgForStatement *loop_nest);
bool isRecursive(SgFunctionDeclaration *func);
bool haveDefination(SgFunctionDeclaration *func);

/* 函数调用图生成过滤器 */
struct StrictUserOnlyPredicate
{
   bool operator()(SgFunctionDeclaration *decl) const
   {
      if (!decl)
         return false;
      Sg_File_Info *info = decl->get_file_info();
      if (info->isCompilerGenerated())
         return false; // [cite: 63]

      std::string filename = info->get_filename();
      // 核心逻辑：剔除标准库路径 [cite: 103, 105]
      if (filename.find("/usr/") != std::string::npos ||
          filename.find("include") != std::string::npos)
      {
         return false;
      }
      return true;
   }
};
std::map<std::string, int> performTopologicalSort(CallGraphBuilder &CGBuilder); // 获取函数调用的拓扑排序
#endif
