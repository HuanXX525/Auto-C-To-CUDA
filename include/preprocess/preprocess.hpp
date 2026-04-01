/* Header file for preprocessing the input code */

#ifndef INPUT_PREPROC
#define INTUT_PREPROC
#include "rose.h"
#include <CallGraph.h>
#include "parallel/parallel.hpp"

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

class FuncAttribute : public AstAttribute {
	public:
		FuncAttribute(bool safe) { this->is_safe = safe;}
		virtual FuncAttribute * copy() const override {return new FuncAttribute(*this);}
		virtual std::string attribute_class_name() const override {return "FuncAttribute";}
      static void getAttributes(SgFunctionCallExp *call, std::map<std::string, int>& funcOrder);
      /* Getters */
      bool isSafe(){return is_safe;}
      bool isRecursive() { return is_recursive; }
		bool haveDefination() { return have_defination; }
      bool haveStaticVar() { return have_static_var; }
      bool haveStaticFuncCall() { return have_static_func_call; }
      bool isPure() { return is_pure; }
      void setPure(bool p) { is_pure = p; }
      void setDefination(bool de) { have_defination = de; }
		void setRecursive(bool re) { is_recursive = re; }
      void setStaticVar(bool sv) {have_static_var=sv;}
      void setStaticFuncCall(bool sf){have_static_func_call=sf;}

	private:
		bool is_safe = false;
		bool is_recursive = true;
		bool have_defination = false;
      bool have_static_var = true;
      bool have_static_func_call = true;
      bool is_pure = false;
      static bool _static_var(SgFunctionDefinition *funDef);
      static bool _internalStaticFunctionCall(SgFunctionDefinition *funDef);
      static bool _pure_function(SgFunctionDefinition *funDef);
};

#endif
