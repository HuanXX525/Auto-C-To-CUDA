/* Loop normalization */

#include "normalize/normalize.hpp"
#include "logger.h"

/* Normalize the loop nest (this gets called in main() of translate.cpp */
bool normalizeLoopNest(SgForStatement *loop_nest)
{
	/* Obtain each of the loops in the nest */
	Rose_STL_Container<SgNode*> loops = NodeQuery::querySubTree(loop_nest, V_SgForStatement);

	/* Loop thru the loops in the nest */
	Rose_STL_Container<SgNode*>::iterator iter;
	for(iter = loops.begin(); iter != loops.end(); iter++)
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
	AstPostProcessing(loop_nest);

	/* If we get here, the loop nest should be normalized */
	return true;
}


/* Normalize individual loops (this gets called by normalizeLoopNest() */
bool normalizeLoop(SgForStatement *loop)
{
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
			
			/* Replace U with (U + (S - L))/S in order to help with constant folding (since U can be a variable) */

			/* Handle the case when U is a binary op (ex: U = x+1)
			   
			   This only helps for the case when U was originally a var, 
			   since SageInterface::forLoopNormalization() may add or sub a 1
			*/
			SgExpression *num;
			if(isSgBinaryOp(U))
			{
				SgExpression *lhs = isSgBinaryOp(U)->get_lhs_operand();
				SgExpression *rhs = isSgBinaryOp(U)->get_rhs_operand();
				
				/* (x+1)+(S-L) --> x+(1+(S-L)) */
				if(isSgAddOp(U))
				{
					SgExpression *intermed = SageBuilder::buildAddOp(rhs, SageBuilder::buildSubtractOp(S, L) );
					num = SageBuilder::buildAddOp(lhs, intermed);
				}

				/* (x-1)+(S-L) --> x+(S-(1+L)) */
				else if(isSgSubtractOp(U))
				{
					SgExpression *intermed = SageBuilder::buildSubtractOp(S, SageBuilder::buildAddOp(rhs, L) );
					num = SageBuilder::buildAddOp(lhs, intermed);
				}
				/* Just leave U as is */
				else
					num = SageBuilder::buildAddOp(U, SageBuilder::buildSubtractOp(S,L) );
			}
			else
				num = SageBuilder::buildAddOp(U, SageBuilder::buildSubtractOp(S,L) );

			
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
			   produce invalid code after replacement (e.g. ++(1*i+0)) */
			SgNode *parent = curr_ref->get_parent();
			if (isSgPlusPlusOp(parent) || isSgMinusMinusOp(parent))
				continue;
			if (SgBinaryOp *bop = isSgBinaryOp(parent)) {
				if (bop->get_lhs_operand() == curr_ref)
					continue;
			}
						
			/* Make it (S*index) + (L-S) to help with constant folding */
			SgExpression *mul = SageBuilder::buildMultiplyOp(SageInterface::copyExpression(S), SageInterface::copyExpression(index));
			//SgExpression *new_var = SageBuilder::buildAddOp( SageBuilder::buildSubtractOp(mul, S) , L);
			SgExpression *new_var = SageBuilder::buildAddOp(mul, SageBuilder::buildSubtractOp(SageInterface::copyExpression(L), SageInterface::copyExpression(S) ) );				
			SageInterface::replaceExpression(curr_ref, new_var); 
		}
	
	}

	/* Fix parent pointers and variable references after all modifications */
	SageInterface::fixVariableReferences(loop);

	/* If we get here, all steps were successful and loop is normalized */
	return true;
}

