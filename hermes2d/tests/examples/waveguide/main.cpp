#define HERMES_REPORT_WARN
#define HERMES_REPORT_INFO
#define HERMES_REPORT_VERBOSE
#define HERMES_REPORT_FILE "application.log"
#include "hermes2d.h"

using namespace RefinementSelectors;

/** \addtogroup t_example_waveguide Examples/Waveguide
 *  \{
 *  \brief This test makes sure that the example "waveguide" works correctly.
 *
 *  \section s_params Parameters
 *   - P_INIT=1
 *   - THERSHOLD=0.5
 *   - STRATEGY=1
 *   - CAND_LIST=HP_ANISO
 *   - MESH_REGULARITY=-1
 *   - ERR_STOP=0.1
 *   - CONV_EXP=1.0
 *   - NDOF_STOP=40000
 *   - ERROR_WEIGHTS=(H: 1; P: 1; ANISO: 1)
 *
 *  \section s_res Results
 *   - DOFs: 3994
 *   - Error estimate: 9.22E-2%
 *   - Iterations: 36 (the last iteration at which ERR_STOP is fulfilled)
 */

const int INIT_REF_NUM = 0;              // Number of initial uniform mesh refinements.
const int P_INIT = 2;                    // Initial polynomial degree. NOTE: The meaning is different from
                                         // standard continuous elements in the space H1. Here, P_INIT refers
                                         // to the maximum poly order of the tangential component, and polynomials
                                         // of degree P_INIT + 1 are present in element interiors. P_INIT = 0
                                         // is for Whitney elements.
const bool ALIGN_MESH = true;           // if ALIGN_MESH == true, curvilinear elements aligned with the
                                         // circular load are used, otherwise one uses a non-aligned mesh.
const double THRESHOLD = 0.3;            // This is a quantitative parameter of the adapt(...) function and
                                         // it has different meanings for various adaptive strategies (see below).
const int STRATEGY = 0;                  // Adaptive strategy:
                                         // STRATEGY = 0 ... refine elements until sqrt(THRESHOLD) times total
                                         //   error is processed. If more elements have similar errors, refine
                                         //   all to keep the mesh symmetric.
                                         // STRATEGY = 1 ... refine all elements whose error is larger
                                         //   than THRESHOLD times maximum element error.
                                         // STRATEGY = 2 ... refine all elements whose error is larger
                                         //   than THRESHOLD.
                                         // More adaptive strategies can be created in adapt_ortho_h1.cpp.
const CandList CAND_LIST = H2D_HP_ANISO; // Predefined list of element refinement candidates. Possible values are
                                         // H2D_P_ISO, H2D_P_ANISO, H2D_H_ISO, H2D_H_ANISO, H2D_HP_ISO,
                                         // H2D_HP_ANISO_H, H2D_HP_ANISO_P, H2D_HP_ANISO.
                                         // See the User Documentation for details.
const int MESH_REGULARITY = -1;          // Maximum allowed level of hanging nodes:
                                         // MESH_REGULARITY = -1 ... arbitrary level hangning nodes (default),
                                         // MESH_REGULARITY = 1 ... at most one-level hanging nodes,
                                         // MESH_REGULARITY = 2 ... at most two-level hanging nodes, etc.
                                         // Note that regular meshes are not supported, this is due to
                                         // their notoriously bad performance.
const double CONV_EXP = 1.0;             // Default value is 1.0. This parameter influences the selection of
                                         // cancidates in hp-adaptivity. See get_optimal_refinement() for details.
const double ERR_STOP = 2.0;             // Stopping criterion for adaptivity (rel. error tolerance between the
                                         // reference mesh and coarse mesh solution in percent).
const int NDOF_STOP = 60000;             // Adaptivity process stops when the number of degrees of freedom grows
                                         // over this limit. This is to prevent h-adaptivity to go on forever.
MatrixSolverType matrix_solver = SOLVER_UMFPACK;  // Possibilities: SOLVER_UMFPACK, SOLVER_PETSC,
                                                  // SOLVER_MUMPS, and more are coming.

// Problem parameters.
const double e_0   = 8.8541878176 * 1e-12;
const double mu_0   = 1.256 * 1e-6;
const double e_r = 1.0;
const double mu_r = 1.0;
const double rho = 3820.0;
const double Cp = 7.531000;
const double freq = 1.0*2450000000.0;
const double omega = 2 * M_PI * freq;
const double c = 1 / sqrt(e_0 * mu_0);
const double kappa  = 2 * M_PI * freq * sqrt(e_0 * mu_0);
const double J = 0.0000033333;

// Boundary condition types.
BCType bc_types(int marker)
{
  if (marker == 2) return BC_ESSENTIAL; // perfect conductor BC
  else return BC_NATURAL;               // impedance BC
}

// Essential (Dirichlet) boundary condition values.
scalar essential_bc_values(int ess_bdy_marker, double x, double y)
{
  return 0;
}

// Geometry of the load.
bool in_load(double x, double y)
{
  double cx = -0.152994121;
  double cy =  0.030598824;
  double r = 0.043273273;
  if (sqr(cx - x) + sqr(cy - y) < sqr(r)) return true;
  else return false;
}

// Gamma as a function of x, y.
double gam(int marker, double x, double y)
{
  if (ALIGN_MESH && marker == 1) return 0.03;
  if (!ALIGN_MESH && in_load(x,y)) {
    double cx = -0.152994121;  double cy =  0.030598824;
    double r = sqrt(sqr(cx - x) + sqr(cy - y));
    return (0.03 + 1)/2.0 - (0.03 - 1) * atan(10.0*(r -  0.043273273)) / M_PI;
  }
  return 0.0;
}
double gam(int marker, Ord x, Ord y)
{  return 0.0; }


// Relative permittivity as a function of x, y.
double er(int marker, double x, double y)
{
  if (ALIGN_MESH && marker == 1) return 7.5;
  if (!ALIGN_MESH && in_load(x,y)) {
    double cx = -0.152994121;  double cy =  0.030598824;
    double r = sqrt(sqr(cx - x) + sqr(cy - y));
    return (7.5 + 1)/2.0 - (7.5 - 1) * atan(10.0*(r -  0.043273273)) / M_PI;
  }
  return 1.0;
}
double er(int marker, Ord x, Ord y)
{  return 1.0; }

// Weak forms.
#include "forms.cpp"

int main(int argc, char* argv[])
{
  // Load the mesh.
  Mesh mesh;
  H2DReader mloader;
  if (ALIGN_MESH) mloader.load("oven_load_circle.mesh", &mesh);
  else mloader.load("oven_load_square.mesh", &mesh);

  // Perform initial mesh refinemets.
  for (int i=0; i < INIT_REF_NUM; i++)  mesh.refine_all_elements();

  // Create an Hcurl space.
  HcurlSpace space(&mesh, bc_types, essential_bc_values, P_INIT);

  // Initialize the weak formulation.
  WeakForm wf;
  wf.add_matrix_form(callback(bilinear_form));
  wf.add_vector_form_surf(callback(linear_form_surf));

  // Initialize coarse and reference mesh solution.
  Solution sln, ref_sln;

  // Initialize refinements selector.
  HcurlProjBasedSelector selector(CAND_LIST, CONV_EXP, H2DRS_DEFAULT_ORDER);
  
  // DOF and CPU convergence graphs initialization.
  SimpleGraph graph_dof, graph_cpu;
  
  // Time measurement.
  TimePeriod cpu_time;
  cpu_time.tick();

  // Adaptivity loop:
  int as = 1; 
  bool done = false;
  do
  {
    info("---- Adaptivity step %d:", as);

    // Construct globally refined reference mesh and setup reference space.
    Space* ref_space = construct_refined_space(&space);

    // Assemble the reference problem.
    info("Solving on reference mesh.");
    bool is_linear = true;
    DiscreteProblem* dp = new DiscreteProblem(&wf, ref_space, is_linear);
    SparseMatrix* matrix = create_matrix(matrix_solver);
    Vector* rhs = create_vector(matrix_solver);
    Solver* solver = create_linear_solver(matrix_solver, matrix, rhs);
    dp->assemble(matrix, rhs);

    // Time measurement.
    cpu_time.tick();
    
    // Solve the linear system of the reference problem. 
    // If successful, obtain the solution.
    if(solver->solve()) Solution::vector_to_solution(solver->get_solution(), ref_space, &ref_sln);
    else error ("Matrix solver failed.\n");

    // Project the fine mesh solution onto the coarse mesh.
    info("Projecting reference solution on coarse mesh.");
    OGProjection::project_global(&space, &ref_sln, &sln, matrix_solver, HERMES_HCURL_NORM); 

    // Time measurement.
    cpu_time.tick();

    // Calculate element errors and total error estimate.
    info("Calculating error estimate."); 
    Adapt* adaptivity = new Adapt(&space, HERMES_HCURL_NORM);
    bool solutions_for_adapt = true;
    double err_est_rel = adaptivity->calc_err_est(&sln, &ref_sln, solutions_for_adapt, HERMES_TOTAL_ERROR_REL | HERMES_ELEMENT_ERROR_REL) * 100;

    // Report results.
    info("ndof_coarse: %d, ndof_fine: %d, err_est_rel: %g%%", 
      Space::get_num_dofs(&space), Space::get_num_dofs(ref_space), err_est_rel);

    // Time measurement.
    cpu_time.tick();

    // Add entry to DOF and CPU convergence graphs.
    graph_dof.add_values(Space::get_num_dofs(&space), err_est_rel);
    graph_dof.save("conv_dof_est.dat");
    graph_cpu.add_values(cpu_time.accumulated(), err_est_rel);
    graph_cpu.save("conv_cpu_est.dat");

    // If err_est too large, adapt the mesh.
    if (err_est_rel < ERR_STOP) done = true;
    else 
    {
      info("Adapting coarse mesh.");
      done = adaptivity->adapt(&selector, THRESHOLD, STRATEGY, MESH_REGULARITY);
      
      // Increase the counter of performed adaptivity steps.
      if (done == false)  as++;
    }
    if (Space::get_num_dofs(&space) >= NDOF_STOP) done = true;

    // Clean up.
    delete solver;
    delete matrix;
    delete rhs;
    delete adaptivity;
    if(done == false) delete ref_space->get_mesh();
    delete ref_space;
    delete dp;
    
  }
  while (done == false);
  
  verbose("Total running time: %g s", cpu_time.accumulated());

  int ndof = Space::get_num_dofs(&space);

#define ERROR_SUCCESS                               0
#define ERROR_FAILURE                               -1
  int n_dof_allowed = 1300;
  printf("n_dof_actual = %d\n", ndof);
  printf("n_dof_allowed = %d\n", n_dof_allowed);
  if (ndof <= n_dof_allowed) {
    printf("Success!\n");
    return ERROR_SUCCESS;
  }
  else {
    printf("Failure!\n");
    return ERROR_FAILURE;
  }
}

