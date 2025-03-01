#include "core/gaussian_elimination.h"
#include "modules/api.h"
#include <math.h>
#include <stdlib.h>

void ui_callback(
    dt_module_t *mod,
    dt_token_t   param)
{ // callback for import from cc24/colour picker button:
  // retrieve instance token from our params and corresponding colour picker by that instance name:
  const int   parid   = dt_module_get_param(mod->so, param);
  const char *inst    = dt_module_param_string(mod, parid); if(!inst) return;
  const int   pick_id = dt_module_get(mod->graph, dt_token("pick"), dt_token(inst));
  if(pick_id < 0 || pick_id > mod->graph->num_modules) return;
  const dt_module_t *mp = mod->graph->module+pick_id;
  // retrieve rbmap data from colour picker
  const int   pck_pid = dt_module_get_param(mp->so, dt_token("picked"));
  const int   ref_pid = dt_module_get_param(mp->so, dt_token("ref"));
  const int   cnt_pid = dt_module_get_param(mp->so, dt_token("nspots"));
  const int   cnt = CLAMP(dt_module_param_int(mp, cnt_pid)[0], 0, 24);
  const float *ref = dt_module_param_float(mp, ref_pid);
  const float *pck = dt_module_param_float(mp, pck_pid);
  const int   rbm_pid = dt_module_get_param(mod->so, dt_token("rbmap"));
  const int   rbc_pid = dt_module_get_param(mod->so, dt_token("cnt"));
  float *rbmap = (float *)dt_module_param_float(mod, rbm_pid);
  int   *rbcnt = (int   *)dt_module_param_int  (mod, rbc_pid);
  if(!rbmap || !rbcnt)     return;
  if(!pck || !ref || !cnt) return;
  for(int i=0;i<cnt;i++)
  { // init source -> destination
    rbmap[6*i+0] = pck[3*i+0];
    rbmap[6*i+1] = pck[3*i+1];
    rbmap[6*i+2] = pck[3*i+2];
    rbmap[6*i+3] = ref[3*i+0] <= 0.0f ? pck[3*i+0] : ref[3*i+0];
    rbmap[6*i+4] = ref[3*i+1] <= 0.0f ? pck[3*i+1] : ref[3*i+1];
    rbmap[6*i+5] = ref[3*i+2] <= 0.0f ? pck[3*i+2] : ref[3*i+2];
  }
  rbcnt[0] = cnt;
}

// thinplate spline kernel phi(r).
// note that this one is different to the one used in darktable:
// the threshold added to r2 is crucial,
// or else distance 0 and distance 1 will both evaluate to 0, resulting in
// interesting curves that do not pass through the control points. this is true
// at least in my tests with 5 control points, maybe the effect evens out for more
// points which do not happen to reach exactly 0 and 1 as distance.
// (some sources use r^2 * log(r/r0) where r0 takes care of the scale)
static inline double
kernel(const float *x, const float *y)
{
  const double r2 =
      (x[0] - y[0]) * (x[0] - y[0]) +
      (x[1] - y[1]) * (x[1] - y[1]) +
      (x[2] - y[2]) * (x[2] - y[2]);
  return sqrt(r2); // linear, cannot overshoot
  // if(r2 < 1e-8) return 0.0;
  // return r2 * sqrt(r2);
  // return r2 * logf(r2);
}

static inline void
compute_coefficients(
  const int    N,        // number of patches
  const float *source,   // N 3d source coordinates
  const float *target,   // N 3d target coordinates
  float       *coef)     // will write the 3*(N+3) coefficient vector here, assumed 0 inited
{
  /*
      K. Anjyo, J. P. Lewis, and F. Pighin, "Scattered data
      interpolation for computer graphics," ACM SIGGRAPH 2014 Courses
      on - SIGGRAPH ’14, 2014.
      http://dx.doi.org/10.1145/2614028.2615425
      http://scribblethink.org/Courses/ScatteredInterpolation/scatteredinterpcoursenotes.pdf

      construct the system matrix and the vector of function values and
      solve the set of linear equations Ax = b:

      / R   P \  / c \   / f \
      |       |  |   | = |   |
      \ P^t 0 /  \ d /   \ 0 /

      for the coefficient vector x = (c d)^t (for every output dimension separately)
      we work in 2D xy chromaticity space.
      our P is a 3x3 matrix (no constant part, we want to keep black black)
      (split into one 1x3 row per solve, as we solve for every output dimension subsequently)
  */
  const int N2 = N+3;
  switch(N) // N is the number of patches, i.e. we have N+2 constraints in the matrix
  {
  case 0: // identity matrix for all three output channels
    for(int co=0;co<3;co++) coef[co*4+co] = 1.0;
    break;
  case 1: // one patch: white balance (we could move, but i don't think we want that)
    for(int co=0;co<3;co++) coef[co*4+co] = target[co] / source[co];
    break;
  default: // fully generic case, N patches
  { // setup linear system of equations
    double *A = malloc(sizeof(*A) * N2 * N2);
    double *b = malloc(sizeof(*b) * N2);
    // coefficients from nonlinear radial kernel functions
    for(int j=0;j<N;j++)
      for(int i=j;i<N;i++)
        A[j*N2+i] = A[i*N2+j] = kernel(source + 3*i, source + 3*j);
    // coefficients from (constant and) linear functions
    for(int i=0;i<N;i++) A[i*N2+N+0] = A[(N+0)*N2+i] = source[3*i+0];
    for(int i=0;i<N;i++) A[i*N2+N+1] = A[(N+1)*N2+i] = source[3*i+1];
    for(int i=0;i<N;i++) A[i*N2+N+2] = A[(N+2)*N2+i] = source[3*i+2];
    // lower-right zero block
    for(int j=N;j<N2;j++)
      for(int i=N;i<N2;i++)
        A[j*N2+i] = 0;
    // make coefficient matrix triangular
    int *pivot = malloc(sizeof(*pivot)*N2);
    if (gauss_make_triangular(A, pivot, N2))
    { // calculate coefficients for r channel
      for(int i=0;i<N; i++) b[i] = target[3*i];
      for(int i=N;i<N2;i++) b[i] = 0;
      gauss_solve_triangular(A, pivot, b, N2);
      for(int i=0;i<N;i++) coef[12 + 4*i + 0] = b[i];
      for(int i=0;i<3;i++) coef[4*i + 0] = b[N+i];
      // calculate coefficients for g channel
      for(int i=0;i<N; i++) b[i] = target[3*i+1];
      for(int i=N;i<N2;i++) b[i] = 0;
      gauss_solve_triangular(A, pivot, b, N2);
      for(int i=0;i<N;i++) coef[12 + 4*i + 1] = b[i];
      for(int i=0;i<3;i++) coef[4*i + 1] = b[N+i];
      // calculate coefficients for b channel
      for(int i=0;i<N; i++) b[i] = target[3*i+2];
      for(int i=N;i<N2;i++) b[i] = 0;
      gauss_solve_triangular(A, pivot, b, N2);
      for(int i=0;i<N;i++) coef[12 + 4*i + 2] = b[i];
      for(int i=0;i<3;i++) coef[4*i + 2] = b[N+i];
    }
    else
    { // yes, really, we should have continued to use the svd/pseudoinverse for exactly such cases.
      // i might bring it back at some point.
      // or precondition like https://www.sciencedirect.com/science/article/pii/S0377042711003669 ?
#if 0
      fprintf(stderr, "[colour] fuck, matrix was singular or something!\n");
      fprintf(stderr, "[colour] N=%d\n", N);
      for(int k=0;k<N;k++)
        fprintf(stderr, "[colour] src = %g %g %g --> %g %g %g\n",
            source[3*k+0], source[3*k+1], source[3*k+2],
            target[3*k+0], target[3*k+1], target[3*k+2]);
      fprintf(stderr, "[colour] A = \n");
      for(int j=0;j<N2;j++)
      {
        for(int i=0;i<N2;i++)
          fprintf(stderr, "%.2f\t", A[j*N2+i]);
        fprintf(stderr, "\n");
      }
#endif
    }
    free(pivot);
    free(b);
    free(A);
  }
  }
}

void commit_params(dt_graph_t *graph, dt_module_t *module)
{
  float *f = (float *)module->committed_param;
  uint32_t *i = (uint32_t *)module->committed_param;

  // grab params by name:
  const float *p_wb  = dt_module_param_float(module, dt_module_get_param(module->so, dt_token("white")));
  const float  p_tmp = dt_module_param_float(module, dt_module_get_param(module->so, dt_token("temp")))[0];
  const int    p_cnt = dt_module_param_int  (module, dt_module_get_param(module->so, dt_token("cnt")))[0];
  const float *p_map = dt_module_param_float(module, dt_module_get_param(module->so, dt_token("rbmap")));
        int    p_mat = dt_module_param_int  (module, dt_module_get_param(module->so, dt_token("matrix")))[0];
  const float *p_mtx = dt_module_param_float(module, dt_module_get_param(module->so, dt_token("mat")));
  const int    p_gam = dt_module_param_int  (module, dt_module_get_param(module->so, dt_token("gamut")))[0];
  const int    p_mod = dt_module_param_int  (module, dt_module_get_param(module->so, dt_token("mode")))[0];
  const float  p_sat = dt_module_param_float(module, dt_module_get_param(module->so, dt_token("sat")))[0];
  const int    p_pck = dt_module_param_int  (module, dt_module_get_param(module->so, dt_token("picked")))[0];

  // wb and exposure mul:
  f[0] = p_wb[0] / p_wb[1];
  f[1] = 1.0f;
  f[2] = p_wb[2] / p_wb[1];
  f[3] = powf(2.0f, ((float*)module->param)[0]);
  int off = 4+12+4+12+4*24+4*24;
  // linear, as the dng spec says:
  // f[off+0] = 1.0f - CLAMP((p_tmp - 2856.f)/(6504.f-2856.f), 0.0f, 1.0f);
  // turingbot reverses this function to more accurately blend along CCT:
  f[off+0] = 1.0f - CLAMP(tanf(asinhf(46.3407f+p_tmp))+(-0.0287128f*cosf(0.000798585f*(714.855f-p_tmp)))+0.942275f, 0.0f, 1.0f);
  i[off+1] = p_mat == 4 ? 1 : 0; // colour mode matrix or clut
  f[off+2] = p_sat;
  i[off+3] = p_pck;
  i[off+4] = p_gam;

  if(p_mat == 1 && !(module->img_param.cam_to_rec2020[0] > 0)) p_mat = 0; // no matrix? default to identity
  if(p_mat == 1)
  { // the one that comes with the image from the source node:
    for(int j=0;j<3;j++) for(int i=0;i<3;i++)
      f[4+4*i+j] = module->img_param.cam_to_rec2020[3*j+i];
  }
  else if(p_mat == 2)
  { // CIE XYZ
    const float xyz_to_rec2020[] = {
      1.7166511880, -0.3556707838, -0.2533662814,
     -0.6666843518,  1.6164812366,  0.0157685458,
      0.0176398574, -0.0427706133,  0.9421031212};
    for(int j=0;j<3;j++) for(int i=0;i<3;i++)
      f[4+4*i+j] = xyz_to_rec2020[3*j+i];
  }
  else if(p_mat == 3)
  { // rec709/linear srgb
    const float rec709_to_rec2020[] = {
      0.62750375, 0.32927542, 0.04330267,
      0.06910828, 0.91951917, 0.0113596,
      0.01639406, 0.08801128, 0.89538036};
    for(int j=0;j<3;j++) for(int i=0;i<3;i++)
      f[4+4*i+j] = rec709_to_rec2020[3*j+i];
  }
  else if(p_mat == 5)
  { // read what we have stored in params
    for(int j=0;j<3;j++) for(int i=0;i<3;i++)
      f[4+4*i+j] = p_mtx[3*j+i];
  }
  else
  { // p_mat == 0 (or default) rec2020, identity matrix
    for(int j=0;j<3;j++) for(int i=0;i<3;i++)
      f[4+4*j+i] = i==j ? 1.0f : 0.0f;
  }
  
  if(p_mod == 1)
  { // mode is set to "data driven", means we have explicit src/tgt points
    const int N = CLAMP(p_cnt, 0, 24);
    i[16] = N;
    i[17] = i[18] = i[19] = 0;

    float src[72], tgt[72];
    for(int i=0;i<N;i++)
    { // plain rgb
      src[3*i+0] = p_map[6*i+0];
      src[3*i+1] = p_map[6*i+1];
      src[3*i+2] = p_map[6*i+2];
      tgt[3*i+0] = p_map[6*i+3];
      tgt[3*i+1] = p_map[6*i+4];
      tgt[3*i+2] = p_map[6*i+5];
    }

    int off = 128; // 4+12+4 +12+4*24 = 128
    memset(f + 20, 0, sizeof(float)*(12+24*4+24*4)); // zero out everything
    for(int k=0;k<N;k++)
    { // source points
      f[off + 4*k + 0] = src[3*k+0];
      f[off + 4*k + 1] = src[3*k+1];
      f[off + 4*k + 2] = src[3*k+2];
      f[off + 4*k + 3] = 0.0f;
    }
    compute_coefficients(N, src, tgt, f + 20);
  }
  else
  { // mode == 0 (or default): "parametric" mode
    i[16] = i[17] = i[18] = i[19] = 0; // disable rbf
  }
}

int init(dt_module_t *mod)
{
  mod->committed_param_size = sizeof(float)*(4+12+4+12+4*24+4*24+5);
  return 0;
}

void create_nodes(
    dt_graph_t  *graph,
    dt_module_t *module)
{
  int have_clut = dt_connected(module->connector+2);
  int have_pick = dt_connected(module->connector+3);
  int have_abney = dt_connected(module->connector+4) && dt_connected(module->connector+5);
  const int pc[] = { have_clut, have_pick, have_abney };
  const int nodeid = dt_node_add(graph, module, "colour", qvk.float_atomics_supported ? "main" : "main-",
      module->connector[0].roi.wd, module->connector[0].roi.ht, 1, sizeof(pc), pc, 6,
      "input",   "read",  "rgba", "f16",  dt_no_roi,
      "output",  "write", "rgba", "f16",  &module->connector[0].roi,
      "clut",    "read",  "rgba", "f16",  dt_no_roi,
      "picked",  "read",  "r",    "atom", dt_no_roi,
      "abney",   "read",  "rg",   "f16",  dt_no_roi,
      "spectra", "read",  "rgba", "f16",  dt_no_roi);
  dt_connector_copy(graph, module, 0, nodeid, 0);
  dt_connector_copy(graph, module, 1, nodeid, 1);
  if(have_clut)  dt_connector_copy(graph, module, 2, nodeid, 2);
  else           dt_connector_copy(graph, module, 0, nodeid, 2); // dummy
  if(have_pick)  dt_connector_copy(graph, module, 3, nodeid, 3);
  else           dt_connector_copy(graph, module, 0, nodeid, 3); // dummy
  if(have_abney) dt_connector_copy(graph, module, 4, nodeid, 4);
  else           dt_connector_copy(graph, module, 0, nodeid, 4); // dummy
  if(have_abney) dt_connector_copy(graph, module, 5, nodeid, 5);
  else           dt_connector_copy(graph, module, 0, nodeid, 5); // dummy
}
