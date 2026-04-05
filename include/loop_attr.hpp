/* Header File for the LoopNestAttribute Class */

#ifndef LOOP_ATTR
#define LOOP_ATTR

#include "rose.h"
#include <algorithm>
#include <vector>

/* Class for setting attributes of loop nest */
class LoopNestAttribute : public AstAttribute {
	public:
		LoopNestAttribute(int s, bool f) {this->size = s; this->flag = f;}
		virtual LoopNestAttribute * copy() const override {return new LoopNestAttribute(*this);}
		virtual std::string attribute_class_name() const override {return "LoopNestAttribute";}
		
		/* Getters */
		int get_nest_size() const {return size;}
		bool get_nest_flag() const {return flag;}
		const std::vector<SgInitializedName*> & get_iter_vec() const {return iter_vec;}
		const std::list<SgExpression*> & get_bound_vec() const {return bound_vec;}
		const std::vector<SgInitializedName*> & get_symb_vec() const {return symb_vec;}
		const std::list<std::list<std::list<std::vector<SgExpression*>>>> & get_arr_dep_info() const {return arr_dep_info;}
		bool contains_iter_var(SgInitializedName *decl) const {
			return std::find(iter_vec.begin(), iter_vec.end(), decl) != iter_vec.end();
		}
		bool contains_symb_var(SgInitializedName *decl) const {
			return std::find(symb_vec.begin(), symb_vec.end(), decl) != symb_vec.end();
		}
		int get_iter_index(SgInitializedName *decl) const {
			auto it = std::find(iter_vec.begin(), iter_vec.end(), decl);
			if (it == iter_vec.end())
				return -1;
			return static_cast<int>(std::distance(iter_vec.begin(), it));
		}
		int get_symb_index(SgInitializedName *decl) const {
			auto it = std::find(symb_vec.begin(), symb_vec.end(), decl);
			if (it == symb_vec.end())
				return -1;
			return static_cast<int>(std::distance(symb_vec.begin(), it));
		}
		int get_coeff_index(SgInitializedName *decl) const {
			int iter_index = get_iter_index(decl);
			if (iter_index >= 0)
				return iter_index;
			int symb_index = get_symb_index(decl);
			if (symb_index >= 0)
				return static_cast<int>(iter_vec.size()) + symb_index;
			return -1;
		}
		
		/* Setters */
		void set_nest_flag(bool new_flag) {flag = new_flag;}
		void set_iter_vec(std::vector<SgInitializedName*> vec) {iter_vec = vec;}
		void set_bound_vec(std::list<SgExpression*> vec) {bound_vec = vec;}
		void set_symb_vec(std::vector<SgInitializedName*> vec) {symb_vec = vec;}
		void set_arr_dep_info(std::list<std::list<std::list<std::vector<SgExpression*>>>> info) {arr_dep_info = info;}

	private:
		int size;
		bool flag;
		std::vector<SgInitializedName*> iter_vec;
		std::list<SgExpression*> bound_vec;
		std::vector<SgInitializedName*> symb_vec;
		std::list<std::list<std::list<std::vector<SgExpression*>>>> arr_dep_info;

};

#endif
