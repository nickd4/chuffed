#include <cstdio>
#include <cassert>
#include "core/engine.h"
#include "core/propagator.h"
#include "branching/branching.h"
#include "vars/modelling.h"
#include "ldsb/ldsb.h"

class GraphColouringSym2 : public Problem {
public:
	// Constants
	int v;                                          // Number of vertices
	int e;                                          // Number of edges

	// Core variables

	vec<IntVar*> x;                                 // Vectex labels
	IntVar* colours;                                // Number of colours

	GraphColouringSym2(char* filename) {

		FILE *fp = fopen(filename, "r");
		assert(fp);

		// ignore comments

		char temp[1000];
		while (fgets(temp, 1000, fp) && temp[0] == 'c');

		// get instance size

		assert(temp[0] == 'p');
		rassert(sscanf(temp, "p edge %d %d\n", &v, &e) == 2);

		// create variables

		createVars(x, v, 1, v, true);
		createVar(colours, 0, v);

		// Post some constraints

		// add edges

		for (int i = 0; i < e; i++) {
			rassert(fgets(temp, 1000, fp));
			int v1, v2;
			rassert(sscanf(temp, "e %d %d\n", &v1, &v2) == 2);
			int_rel(x[v1-1], IRT_NE, x[v2-1]);
		}

		fclose(fp);

		// add constraint on objective

		for (int i = 0; i < v; i++) {
			int_rel(x[i], IRT_LE, colours);
		}

		// Post some branchings

		branch(x, VAR_INORDER, VAL_MIN);
		optimize(colours, OPT_MIN);

		// Declare output variables (optional)

		output_vars(x);

		// Declare symmetries (optional)

		if (so.ldsb) {
			val_sym_ldsb(x, 1, v);
		} else if (so.sym_static) {
			val_sym_break(x, 1, v);
		}

	}

	void restrict_learnable() {
		printf("Setting learnable white list\n");
		for (int i = 0; i < sat.nVars(); i++) sat.flags[i] = 0;
		for (int i = 0; i < x.size(); i++) {
			assert(x[i]->getType() == INT_VAR_EL);
			((IntVarEL*) x[i])->setVLearnable();
			((IntVarEL*) x[i])->setVDecidable(true);
		}
	}

	// Function to print out solution

	void print() {
		for (int i = 0; i < v; i++) {
			printf("%d, ", x[i]->getVal());
		}
		printf("\n");
		fprintf(stderr, "Objective = %d\n", colours->getVal());
		// hack for this problem
		if (so.ldsb) {
			int *a = (int*) ldsb.symmetries[0];
//			for (int i = 0; i < 6; i++) printf("%d ", a[i]); printf("\n");
			a[5] = colours->getVal()-1;
		}
	}

};

int main(int argc, char** argv) {
	parseOptions(argc, argv);

	assert(argc == 2);

	engine.solve(new GraphColouringSym2(argv[1]));

	return 0;
}



