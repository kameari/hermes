#define HERMES_REPORT_WARN
#define HERMES_REPORT_INFO
#define HERMES_REPORT_VERBOSE
#define HERMES_REPORT_FILE "application.log"
#include "hermes2d.h"

using namespace RefinementSelectors;

// This example uses adaptive multimesh hp-FEM to solve a simple problem
// of linear elasticity. Note that since both displacement components
// have similar qualitative behavior, the advantage of the multimesh 
// discretization is less striking than for example in the tutorial 
// example 11-adapt-system.
//
// PDE: Lame equations of linear elasticity, treated as a coupled system
//      of two PDEs.
//
// BC: u_1 = u_2 = 0 on Gamma_1
//     du_2/dn = f on Gamma_2
//     du_1/dn = du_2/dn = 0 elsewhere
//
// The following parameters can be changed: 

const int P_INIT = 2;                             // Initial polynomial degree of all mesh elements.
const bool MULTI = true;                          // MULTI = true  ... use multi-mesh,
                                                  // MULTI = false ... use single-mesh.
                                                  // Note: In the single mesh option, the meshes are
                                                  // forced to be geometrically the same but the
                                                  // polynomial degrees can still vary.
const double THRESHOLD = 0.3;                     // This is a quantitative parameter of the adapt(...) function and
                                                  // it has different meanings for various adaptive strategies (see below).
const int STRATEGY = 1;                           // Adaptive strategy:
                                                  // STRATEGY = 0 ... refine elements until sqrt(THRESHOLD) times total
                                                  //   error is processed. If more elements have similar errors, refine
                                                  //   all to keep the mesh symmetric.
                                                  // STRATEGY = 1 ... refine all elements whose error is larger
                                                  //   than THRESHOLD times maximum element error.
                                                  // STRATEGY = 2 ... refine all elements whose error is larger
                                                  //   than THRESHOLD.
                                                  // More adaptive strategies can be created in adapt_ortho_h1.cpp.
const CandList CAND_LIST = H2D_HP_ANISO;          // Predefined list of element refinement candidates. Possible values are
                                                  // H2D_P_ISO, H2D_P_ANISO, H2D_H_ISO, H2D_H_ANISO, H2D_HP_ISO,
                                                  // H2D_HP_ANISO_H, H2D_HP_ANISO_P, H2D_HP_ANISO.
                                                  // See User Documentation for details.
const int MESH_REGULARITY = -1;                   // Maximum allowed level of hanging nodes:
                                                  // MESH_REGULARITY = -1 ... arbitrary level hangning nodes (default),
                                                  // MESH_REGULARITY = 1 ... at most one-level hanging nodes,
                                                  // MESH_REGULARITY = 2 ... at most two-level hanging nodes, etc.
                                                  // Note that regular meshes are not supported, this is due to
                                                  // their notoriously bad performance.
const double CONV_EXP = 1.0;                      // Default value is 1.0. This parameter influences the selection of
                                                  // cancidates in hp-adaptivity. See get_optimal_refinement() for details.
const double ERR_STOP = 0.5;                      // Stopping criterion for adaptivity (rel. error tolerance between the
                                                  // reference mesh and coarse mesh solution in percent).
const int NDOF_STOP = 60000;                      // Adaptivity process stops when the number of degrees of freedom grows over
                                                  // this limit. This is mainly to prevent h-adaptivity to go on forever.
MatrixSolverType matrix_solver = SOLVER_UMFPACK;  // Possibilities: SOLVER_UMFPACK, SOLVER_PETSC,
                                                  // SOLVER_MUMPS, and more are coming.

// Problem parameters.
const double E  = 200e9;                          // Young modulus for steel: 200 GPa.
const double nu = 0.3;                            // Poisson ratio.
const double f  = 1e3;                            // Load force: 10^3 N.
const double lambda = (E * nu) / ((1 + nu) * (1 - 2*nu));
const double mu = E / (2*(1 + nu));

// Boundary markers.
const int BDY_LEFT = 1;
const int BDY_TOP = 2;

// Boundary condition types.
BCType bc_types(int marker)
  { return (marker == BDY_LEFT) ? BC_ESSENTIAL : BC_NATURAL; }

// Essential (Dirichlet) boundary condition values.
scalar essential_bc_values(int ess_bdy_marker, double x, double y)
{
  return 0;
}

// Weak forms.
#include "forms.cpp"

int main(int argc, char* argv[])
{
  // Time measurement.
  TimePeriod cpu_time;
  cpu_time.tick();

  // Load the mesh.
  Mesh u_mesh, v_mesh;
  H2DReader mloader;
  mloader.load("bracket.mesh", &u_mesh);

  // Initial mesh refinements.
  u_mesh.refine_element(1);
  u_mesh.refine_element(4);

  // Create initial mesh for the vertical displacement component.
  // This also initializes the multimesh hp-FEM.
  v_mesh.copy(&u_mesh);

  // Create H1 spaces with default shapesets.
  H1Space u_space(&u_mesh, bc_types, essential_bc_values, P_INIT);
  H1Space v_space(MULTI ? &v_mesh : &u_mesh, bc_types, essential_bc_values, P_INIT);

  // Initialize the weak formulation.
  WeakForm wf(2);
  wf.add_matrix_form(0, 0, callback(bilinear_form_0_0), HERMES_SYM);  // note that only one symmetric part is
  wf.add_matrix_form(0, 1, callback(bilinear_form_0_1), HERMES_SYM);  // added in the case of symmetric bilinear
  wf.add_matrix_form(1, 1, callback(bilinear_form_1_1), HERMES_SYM);  // forms
  wf.add_vector_form_surf(1, linear_form_surf_1, linear_form_surf_1_ord, BDY_TOP);

  // Initialize coarse and reference mesh solutions.
  Solution u_sln, v_sln, u_ref_sln, v_ref_sln;

  // Initialize refinement selector.
  H1ProjBasedSelector selector(CAND_LIST, CONV_EXP, H2DRS_DEFAULT_ORDER);

  // Initialize views.
  ScalarView s_view_0("Solution[0]", new WinGeom(0, 0, 400, 300));
  s_view_0.show_mesh(false);
  ScalarView s_view_1("Solution[1]", new WinGeom(780, 0, 400, 300));
  s_view_1.show_mesh(false);
  OrderView  o_view_0("Mesh[0]", new WinGeom(410, 0, 360, 300));
  OrderView  o_view_1("Mesh[1]", new WinGeom(1190, 0, 400, 300));

  // DOF and CPU convergence graphs.
  SimpleGraph graph_dof_est, graph_cpu_est;

  // Adaptivity loop:
  int as = 1; 
  bool done = false;
  do
  {
    info("---- Adaptivity step %d:", as);

    // Construct globally refined reference mesh and setup reference space.
    Tuple<Space *>* ref_spaces = construct_refined_spaces(Tuple<Space *>(&u_space, &v_space));

    // Assemble the reference problem.
    info("Solving on reference mesh.");
    bool is_linear = true;
    DiscreteProblem* dp = new DiscreteProblem(&wf, *ref_spaces, is_linear);
    SparseMatrix* matrix = create_matrix(matrix_solver);
    Vector* rhs = create_vector(matrix_solver);
    Solver* solver = create_linear_solver(matrix_solver, matrix, rhs);
    dp->assemble(matrix, rhs);

    // Time measurement.
    cpu_time.tick();
    
    // Solve the linear system of the reference problem. If successful, obtain the solutions.
    if(solver->solve()) Solution::vector_to_solutions(solver->get_solution(), *ref_spaces, 
                                            Tuple<Solution *>(&u_ref_sln, &v_ref_sln));
    else error ("Matrix solver failed.\n");
  
    // Time measurement.
    cpu_time.tick();

    // Project the fine mesh solution onto the coarse mesh.
    info("Projecting reference solution on coarse mesh.");
    OGProjection::project_global(Tuple<Space *>(&u_space, &v_space), Tuple<Solution *>(&u_ref_sln, &v_ref_sln), 
                   Tuple<Solution *>(&u_sln, &v_sln), matrix_solver); 
   
    // View the coarse mesh solution and polynomial orders.
    s_view_0.show(&u_sln); 
    o_view_0.show(&u_space);
    s_view_1.show(&v_sln); 
    o_view_1.show(&v_space);

    // Skip visualization time.
    cpu_time.tick(HERMES_SKIP);

    // Calculate element errors.
    info("Calculating error estimate and exact error."); 
    Adapt* adaptivity = new Adapt(Tuple<Space *>(&u_space, &v_space), Tuple<ProjNormType>(HERMES_H1_NORM, HERMES_H1_NORM));
    adaptivity->set_error_form(0, 0, bilinear_form_0_0<scalar, scalar>, bilinear_form_0_0<Ord, Ord>);
    adaptivity->set_error_form(0, 1, bilinear_form_0_1<scalar, scalar>, bilinear_form_0_1<Ord, Ord>);
    adaptivity->set_error_form(1, 0, bilinear_form_1_0<scalar, scalar>, bilinear_form_1_0<Ord, Ord>);
    adaptivity->set_error_form(1, 1, bilinear_form_1_1<scalar, scalar>, bilinear_form_1_1<Ord, Ord>);

    // Calculate error estimate for each solution component and the total error estimate.
    Tuple<double> err_est_rel;
    bool solutions_for_adapt = true;
    double err_est_rel_total = adaptivity->calc_err_est(Tuple<Solution *>(&u_sln, &v_sln), Tuple<Solution *>(&u_ref_sln, &v_ref_sln), solutions_for_adapt, 
                               HERMES_TOTAL_ERROR_REL | HERMES_ELEMENT_ERROR_ABS, &err_est_rel) * 100;

    // Time measurement.
    cpu_time.tick();

    // Report results.
    info("ndof_coarse[0]: %d, ndof_fine[0]: %d, err_est_rel[0]: %g%%", 
         u_space.Space::get_num_dofs(), Space::get_num_dofs((*ref_spaces)[0]), err_est_rel[0]*100);
    info("ndof_coarse[1]: %d, ndof_fine[1]: %d, err_est_rel[1]: %g%%",
         v_space.Space::get_num_dofs(), Space::get_num_dofs((*ref_spaces)[1]), err_est_rel[1]*100);
    info("ndof_coarse_total: %d, ndof_fine_total: %d, err_est_rel_total: %g%%",
         Space::get_num_dofs(Tuple<Space *>(&u_space, &v_space)), Space::get_num_dofs(*ref_spaces), err_est_rel_total);

    // Add entry to DOF and CPU convergence graphs.
    graph_dof_est.add_values(Space::get_num_dofs(Tuple<Space *>(&u_space, &v_space)), err_est_rel_total);
    graph_dof_est.save("conv_dof_est.dat");
    graph_cpu_est.add_values(cpu_time.accumulated(), err_est_rel_total);
    graph_cpu_est.save("conv_cpu_est.dat");

    // If err_est too large, adapt the mesh.
    if (err_est_rel_total < ERR_STOP) 
      done = true;
    else 
    {
      info("Adapting coarse mesh.");
      done = adaptivity->adapt(Tuple<RefinementSelectors::Selector *>(&selector, &selector), 
                               THRESHOLD, STRATEGY, MESH_REGULARITY);
    }
    if (Space::get_num_dofs(Tuple<Space *>(&u_space, &v_space)) >= NDOF_STOP) done = true;

    // Clean up.
    delete solver;
    delete matrix;
    delete rhs;
    delete adaptivity;
    if(done == false)
      for(int i = 0; i < ref_spaces->size(); i++)
        delete (*ref_spaces)[i]->get_mesh();
    delete ref_spaces;
    delete dp;
    
    // Increase counter.
    as++;
  }
  while (done == false);

  verbose("Total running time: %g s", cpu_time.accumulated());

  // Show the reference solution - the final result.
  s_view_0.set_title("Fine mesh Solution[0]");
  s_view_0.show(&u_ref_sln);
  s_view_1.set_title("Fine mesh Solution[1]");
  s_view_1.show(&v_ref_sln);

  // Wait for all views to be closed.
  View::wait();
  return 0;
}

