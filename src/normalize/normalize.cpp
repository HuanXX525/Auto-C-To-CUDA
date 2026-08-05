/* Normalize loop nest */

#include "normalize/normalize.hpp"
#include "logger.h"

namespace {

// Extract integer value from any ROSE integer constant expression,
// regardless of signedness.  Returns 0 for non-constant expressions.
// Also recurses through SgCastExp wrappers.
long long intValueOf(SgExpression *e)
{
	if (!e) return 0;
	if (SgCastExp* cast = isSgCastExp(e))
		return intValueOf(cast->get_operand());
	if (SgUnsignedIntVal* v = isSgUnsignedIntVal(e)) return (long long)v->get_value();
	if (SgIntVal* v = isSgIntVal(e)) return (long long)v->get_value();
	if (SgUnsignedLongVal* v = isSgUnsignedLongVal(e)) return (long long)v->get_value();
	if (SgLongIntVal* v = isSgLongIntVal(e)) return (long long)v->get_value();
	if (SgUnsignedLongLongIntVal* v = isSgUnsignedLongLongIntVal(e)) return (long long)v->get_value();
	if (SgLongLongIntVal* v = isSgLongLongIntVal(e)) return (long long)v->get_value();
	if (SgUnsignedShortVal* v = isSgUnsignedShortVal(e)) return (long long)v->get_value();
	if (SgShortVal* v = isSgShortVal(e)) return (long long)v->get_value();
	return 0;
}

// True if the expression is a compile-time integer constant (possibly
// wrapped in casts).
bool isIntConst(SgExpression *e)
{
	if (!e) return false;
	if (isSgCastExp(e)) return isIntConst(isSgCastExp(e)->get_operand());
	return isSgValueExp(e);
}

// Build (S - L) with signed arithmetic.  When both operands are
// compile-time constants the difference is folded directly into a
// signed literal, avoiding SgCastExp nodes in the result (ROSE's
// constant folding does not eliminate them, and the affine test
// rejects any expression containing a cast).
SgExpression *buildSignedSub(SgExpression *S, SgExpression *L, SgType *sigTy)
{
	if (isIntConst(S) && isIntConst(L))
		return SageBuilder::buildLongLongIntVal(intValueOf(S) - intValueOf(L));
	return SageBuilder::buildSubtractOp(
		SageBuilder::buildCastExp(SageInterface::copyExpression(S), sigTy),
		SageBuilder::buildCastExp(SageInterface::copyExpression(L), sigTy));
}

} // anonymous namespace

/* Normalize the loop nest (this gets called in main() of translate.cpp */
bool normalizeLoopNest(SgForStatement *loop_nest)
{
	/* Obtain each of the loops in the nest */
	Rose_STL_Container<SgNode*> loops = NodeQuery::querySubTree(loop_nest, V_SgForStatement);

	/* Loop thru the loops in the nest — reverse (innermost-first)
	   so inner loop normalization sees unmodified outer loop variables. */
	Rose_STL_Container<SgNode*>::reverse_iterator iter;
	for(iter = loops.rbegin(); iter != loops.rend(); iter++)
	{
		/* Make proper cast */
		SgForStatement *loop = isSgForStatement(*iter);

		/* Return false if any of the loops in the nest cannot be normalized */
		if(!normalizeLoop(loop))
			return false;
	}
	//// DEBUG__
	// std::cout << loop_nest->unparseToString() << std::endl;
	// std::cout << loop_nest->get_parent()->unparseToString() << std::endl;
	//
	// std::cout<< loop_nest->unparseToString() << std::endl;
	// if (loop_nest && loop_nest->get_parent())
	// {
	// 	std::cout<< loop_nest->get_parent()->unparseToString() << std::endl;
	// }
	// else
	// {
	// 	std::cout << "ERROR!!!!!!!!!!!!!!!!!!!!!!!!!!!!" << std::endl;
	// }
	/* Perform constant folding on the normalized nest (need to supply the parent node) */
	SageInterface::fixVariableReferences(loop_nest);
	SageInterface::constantFolding(loop_nest);

	/* AstPostProcessing fixes parent pointers.  (Previously this block also
	   saved-and-restored variable declarations claimed to be dropped by
	   ROSE's built-in DCE; experiments in .vscode/test/rose_dce_repro showed
	   AstPostProcessing does not run DCE, and the restore logic duplicated
	   function-scope variables into loop bodies causing shadowing bugs.
	   DCE false removal itself was fixed in b8b822a / deadCodeElim.cpp.) */
	AstPostProcessing(loop_nest);

	/* If we get here, the loop nest should be normalized */
	return true;
}


/* Normalize individual loops (this gets called by normalizeLoopNest() */
bool normalizeLoop(SgForStatement *loop)
{
	/* Skip for(;;) and loops whose test is missing/corrupted */
	if (!isSgExprStatement(loop->get_test()))
		return false;

	/* Creates loop in form of: int i; for(i = L; i <= U; i += S) */
	if(SageInterface::forLoopNormalization(loop) == false)
		return false;

	/* Index variable, lower bound, upper bound, and step */
	SgExpression *index = NULL, *L = NULL, *U = NULL, *S = NULL;

	/*
	*********************************
	       Normalize the init           
	*********************************
	*/

	SgStatementPtrList &init_list = loop->get_init_stmt();
	SgStatement *init = init_list.front();
	
	/* Check to see if init statement is an assignment (as is usually the case) */
	if(SageInterface::isAssignmentStatement(init, &index, &L))
	{
		/* Check to see if a variable is being referenced (should be the index variable) */
		SgVarRefExp *index_var = isSgVarRefExp(index);
		if(index_var)
		{
			/* Make a new expression that sets the init_var to 1 */
			SgExprStatement *new_init = SageBuilder::buildAssignStatement(index_var, SageBuilder::buildIntVal(1));

			/* Set the init expression to the new assign statment */
			SageInterface::removeStatement(init);
			SageInterface::appendStatement(new_init, loop->get_for_init_stmt());
		}
		else
			return false;
	}
	else
		/* Skip loops that do not have an assignment statement as an init */
		return false;
	

	

	/*
	********************************
       	       Normalize the test 
	********************************
	*/
	
	/* Verify test is a proper expression (not SgNullStatement) after forLoopNormalization */
	if (!isSgExprStatement(loop->get_test())) {
		log_debug("normalizeLoop: test expression is not a valid SgExprStatement, skipping");
		return false;
	}

	SgExpression *test_expr = loop->get_test_expr();
	SgBinaryOp *test = isSgBinaryOp(test_expr);

	/* Just to make sure it is a binary op (which should be the case after SageInterface::forLoopNormalization()) */
	if(test)
	{
		/* If test is a >=, replace it with a <= */
		if(isSgGreaterOrEqualOp(test))
		{
			test = SageBuilder::buildLessOrEqualOp(test->get_lhs_operand(), test->get_rhs_operand());
			loop->set_test_expr(test);
		}
		
		/* The LHS should be the variable reference */
		SgVarRefExp *test_var = isSgVarRefExp(test->get_lhs_operand());
		SgVarRefExp *index_ref = isSgVarRefExp(index);
		if(!test_var || !index_ref)
			return false;

		/* The RHS should be the upper bound */
		U = test->get_rhs_operand();

		/* If the test_var matches the index_var, then we perform the transformation */
		SgInitializedName *test_decl = test_var->get_symbol()->get_declaration();
		SgInitializedName *index_decl = index_ref->get_symbol()->get_declaration();
		
		if(test_decl == index_decl)
		{
			/* Obtain the step */
			SgBinaryOp *step = isSgBinaryOp(loop->get_increment());
			S = step->get_rhs_operand();

			/* Skip loop if step is not an INT, or if step expression has form: index = index + S */
			if(!isSgIntVal(S))
				return false;

			/* Replace U with (U - L + S)/S */
			//SgExpression *num = SageBuilder::buildAddOp( SageBuilder::buildSubtractOp(U, L) , S);
			//SgExpression *new_upper_bound = SageBuilder::buildIntegerDivideOp(num, S);
			
			/* Replace U with (U + (S - L))/S.  The (S-L) part uses
			   signed arithmetic to avoid unsigned wrapping (e.g.
			   S-(i+1u) when i>0), and is constant-folded when S and L
			   are both constants so no SgCastExp nodes survive. */
			SgType *sigTy = SageBuilder::buildLongType();

			SgExpression *num;
			if(isSgBinaryOp(U))
			{
				SgExpression *lhs = isSgBinaryOp(U)->get_lhs_operand();
				SgExpression *rhs = isSgBinaryOp(U)->get_rhs_operand();
				
				/* (x+1)+(S-L) --> x+(1+(S-L)) */
				if(isSgAddOp(U))
				{
					SgExpression *intermed = SageBuilder::buildAddOp(rhs,
						buildSignedSub(S, L, sigTy));
					num = SageBuilder::buildAddOp(lhs, intermed);
				}

				/* (x-1)+(S-L) --> x+(S-(1+L)) */
				else if(isSgSubtractOp(U))
				{
					SgExpression *rhsL;
					if (isIntConst(rhs) && isIntConst(L))
						rhsL = SageBuilder::buildLongLongIntVal(intValueOf(rhs) + intValueOf(L));
					else
						rhsL = SageBuilder::buildAddOp(rhs, SageInterface::copyExpression(L));
					SgExpression *intermed = buildSignedSub(S, rhsL, sigTy);
					num = SageBuilder::buildAddOp(lhs, intermed);
				}
				/* Just leave U as is */
				else
					num = SageBuilder::buildAddOp(U, buildSignedSub(S, L, sigTy));
			}
			else
				num = SageBuilder::buildAddOp(U, buildSignedSub(S, L, sigTy));

			
			SgExpression *new_upper_bound = SageBuilder::buildIntegerDivideOp(num, S);

			/* Replace test expression */
			SageInterface::setLoopUpperBound(loop, new_upper_bound);

		}
	}
	else
		return false;
	
	


	/* 
	*************************************
	       Normalize the increment 
	************************************
	*/

	SageInterface::setLoopStride(loop, SageBuilder::buildIntVal(1));




	/*
	*************************************************************** 
	       Normalize all references to index in body of loop 
	***************************************************************       
	*/
	
	/* Obtain references to variables in loop body */
	Rose_STL_Container<SgNode*> var_refs = NodeQuery::querySubTree(loop->get_loop_body(), V_SgVarRefExp);

	/* Loop thru the references in the loop body */
	for(Rose_STL_Container<SgNode*>::iterator it = var_refs.begin(); it != var_refs.end(); it++)
	{
		/* Check for any reference to the index variable */
		SgVarRefExp *curr_ref = isSgVarRefExp(*it);
		SgVarRefExp *index_ref = isSgVarRefExp(index);
		if(!curr_ref || !index_ref)
			continue;

		SgInitializedName *curr_decl = curr_ref->get_symbol()->get_declaration();
		SgInitializedName *index_decl = index_ref->get_symbol()->get_declaration();

		/* Make the change from index to (S*index)-S+L */
		if(curr_decl == index_decl)
		{
			/* Skip lvalue uses: ++i / --i and i on lhs of assignment would
			   produce invalid code after replacement (e.g. ++(1*i+0)).
			   Only skip for assignment operators, NOT for arithmetic binary
			   ops (+, -, etc.), otherwise sub-expressions like i-W, i+1
			   inside array indices would keep i un-substituted, leading to
			   out-of-bounds access after loop normalization shifts the
			   iteration variable start from L to 1. */
			SgNode *parent = curr_ref->get_parent();
			if (isSgPlusPlusOp(parent) || isSgMinusMinusOp(parent))
				continue;
			if (isSgAssignOp(parent)) {
				if (isSgAssignOp(parent)->get_lhs_operand() == curr_ref)
					continue;
			}
						
			/* Make it (S*index) + (L-S) using signed arithmetic to
			   avoid unsigned wrapping (e.g. 0u-1 ≠ -1, it wraps to
			   UINT_MAX, corrupting array indices). */
			SgExpression *mul = SageBuilder::buildMultiplyOp(
				SageInterface::copyExpression(S),
				SageInterface::copyExpression(index));
			SgExpression *new_var;
			if (isIntConst(L) && isIntConst(S)) {
				long long delta = intValueOf(L) - intValueOf(S);
				new_var = SageBuilder::buildAddOp(
					mul, SageBuilder::buildLongLongIntVal(delta));
			} else {
				new_var = SageBuilder::buildAddOp(mul,
					SageBuilder::buildSubtractOp(
						SageInterface::copyExpression(L),
						SageInterface::copyExpression(S)));
			}
			SageInterface::replaceExpression(curr_ref, new_var); 
		}
	
	}

	/* Fix parent pointers and variable references after all modifications */
	SageInterface::fixVariableReferences(loop);

	/* If we get here, all steps were successful and loop is normalized */
	return true;
}

