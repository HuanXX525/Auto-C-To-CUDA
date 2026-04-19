/* Header file for induction variable analysis info */

#ifndef INDUCTION_VAR_INFO
#define INDUCTION_VAR_INFO

#include "rose.h"
#include <map>
#include <set>
#include <vector>

struct DerivedAffineForm {
	SgInitializedName *base_iter;
	int scale;
	int bias;

	DerivedAffineForm() : base_iter(NULL), scale(0), bias(0) {}
	DerivedAffineForm(SgInitializedName *base, int s, int b) : base_iter(base), scale(s), bias(b) {}
};

struct InductionVarInfo {
	std::set<SgInitializedName*> bivs;
	std::set<SgInitializedName*> exposable_divs;
	std::map<SgInitializedName*, SgStatement*> definition_stmt;
	std::map<SgInitializedName*, SgBasicBlock*> definition_block;
	std::map<SgInitializedName*, DerivedAffineForm> normalized_closed_form;
	std::map<SgInitializedName*, std::set<SgInitializedName*>> derived_dependencies;
	std::vector<SgInitializedName*> derived_order;
};

#endif
