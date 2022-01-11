#pragma once

#include <alloca.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <math.h>
#include <float.h>

// conjugate gradient solve:
static inline double
dt_conj_grad(const double *A, const double *b, double *x, const int m)
{
  for(int j=0;j<m;j++) x[j] = 0.0;
  double *r = alloca(sizeof(double)*m);
  for(int j=0;j<m;j++) r[j] = b[j];

  double *p = alloca(sizeof(double)*m);
  for(int j=0;j<m;j++) p[j] = r[j];

  double rsold = 0.0;
  for(int j=0;j<m;j++) rsold += r[j]*r[j];

  double *Ap = alloca(sizeof(double)*m);
  for(int i=0;i<m;i++)
  {
    memset(Ap, 0, sizeof(double)*m);
    for(int j=0;j<m;j++)
      for(int k=0;k<m;k++)
        Ap[j] += A[m*j+k] * p[k];
    
    double pAp = 0.0;
    for(int j=0;j<m;j++)
      pAp += p[j] * Ap[j];
    double alpha = rsold / pAp;
    // assert(alpha == alpha);
    if(!(alpha == alpha)) return 0.1;//0.0;
    for(int j=0;j<m;j++)
      x[j] += alpha * p[j];
    for(int j=0;j<m;j++)
      r[j] -= alpha * Ap[j];
    double rsnew = 0.0;
    for(int j=0;j<m;j++) rsnew += r[j]*r[j];
    if(sqrt(rsnew) < 2e-6) return sqrt(rsnew);
    if(rsnew > rsold) return sqrt(rsnew);
    for(int j=0;j<m;j++) p[j] = r[j] + rsnew / rsold * p[j];
    rsold = rsnew;
  }
  return sqrt(rsold);
}

static inline double
dt_gauss_newton_cg_step(
    const int     m,      // number of parameters m < n
    double       *p,      // n parameters, will be updated
    const int     n,      // number of function and target values
    const double *f,      // n function values m < n
    const double *J,      // jacobian m x n (df0/dp0, df0/dp1, .. df1/dp0, ...dfn-1/dpm-1)
    const double *t)      // n target values
{
  // compute b vector Jt r in R[m], residual r in R[n]
  double *b = alloca(sizeof(double)*m);
  memset(b, 0, sizeof(double)*m);
  for(int i=0;i<m;i++)
    for(int j=0;j<n;j++)
      b[i] += J[m*j + i] * (t[j] - f[j]);

  // compute dense square matrix Jt J
  double *A = alloca(sizeof(double)*m*m);
  memset(A, 0, sizeof(double)*m*m);
  // TODO: exploit symmetry
  // TODO: don't compute but pass to CG
  for(int j=0;j<m;j++)
    for(int i=0;i<m;i++)
      for(int k=0;k<n;k++)
        A[j*m+i] += J[k*m+i] * J[k*m+j];

  // now solve
  //        Delta = (Jt J)^-1 Jt r
  // (Jt J) Delta = Jt r
  //   A    x     = b

  double *delta = alloca(sizeof(double)*m);
  double resid = dt_conj_grad(A, b, delta, m);
  assert(resid == resid);
  for(int i=0;i<m;i++) p[i] += delta[i];
  return resid;
}

// TODO: could use only one callback for f and J, would be more efficient
static inline double
dt_gauss_newton_cg(
    void (*f_callback)(double *p, double *f, int m, int n, void *data),
    void (*J_callback)(double *p, double *J, int m, int n, void *data),
    double       *p,      // initial paramters, will be overwritten
    const double *t,      // target values
    const int     m,      // number of parameters
    const int     n,      // number of data points
    const double *lb,     // m lower bound constraints
    const double *ub,     // m upper bound constraints
    const int     num_it, // number of iterations
    void         *data)
{
  double *f = alloca(sizeof(double)*n);
  double *J = alloca(sizeof(double)*n*m);
  double resid = 0.0;
  for(int it=0;it<num_it;it++)
  {
    f_callback(p, f, m, n, data);
    J_callback(p, J, m, n, data);
    resid = dt_gauss_newton_cg_step(m, p, n, f, J, t);
    // if(resid <= 0.0) return resid;
    for(int i=0;i<m;i++)
      p[i] = fminf(fmaxf(p[i], lb[i]), ub[i]);
    // fprintf(stderr, "[solve] residual %g\n", resid);
    if(resid < 1e-30) return resid;
  }
  return resid;
}

static inline double
dt_adam(
    void (*f_callback)(double *p, double *f, int m, int n, void *data),
    void (*J_callback)(double *p, double *J, int m, int n, void *data),
    double       *p,      // initial paramters, will be overwritten
    const double *t,      // target values
    const int     m,      // number of parameters
    const int     n,      // number of data points
    const double *lb,     // m lower bound constraints
    const double *ub,     // m upper bound constraints
    const int     num_it, // number of iterations
    void         *data,
    double        eps,    // 1e-8 to avoid squared gradient estimation to drop to zero
    double        beta1,  // 0.9 decay rate of gradient
    double        beta2,  // 0.99 decay rate of squared gradient
    double        alpha,  // 0.001 learning rate
    int          *abort)  // external user flag to abort asynchronously. can be 0.
{
  double *f  = alloca(sizeof(double)*n);
  double *bp = alloca(sizeof(double)*m);   // best p seen so far
  double *J  = alloca(sizeof(double)*n*m);
  double *mt = alloca(sizeof(double)*n*m); // averaged gradient
  double vt   = 0.0;     // sum of squared past gradients
  double best = DBL_MAX; // best loss seen so far
  memset(mt, 0, sizeof(double)*m*n);
  assert(n == 1); // this only works for a single loss value

  for(int it=1;it<num_it+1;it++)
  {
    f_callback(p, f, m, n, data);
    J_callback(p, J, m, n, data);
    if(f[0] < best)
    {
      best = f[0];
      memcpy(bp, p, sizeof(double)*m);
    }
    double sumJ2 = 0.0;
    for(int k=0;k<n*m;k++) sumJ2 += J[k]*J[k];
    vt = beta2 * vt + (1.0-beta2) * sumJ2;
    for(int k=0;k<n*m;k++)
      mt[k] = beta1 * mt[k] + (1.0-beta1) * J[k];

    double corr_m = 1.0/(1.0-pow(beta1, it));
    double corr_v = sqrt(vt / (1.0-pow(beta2, it)));
    if(!(corr_v > 0.0)) corr_v = 1.0;
    if(!(corr_m > 0.0)) corr_m = 1.0;

    for(int k=0;k<m;k++)
      p[k] -= mt[k]*corr_m * alpha / (corr_v + eps);
    for(int i=0;i<m;i++)
      p[i] = fminf(fmaxf(p[i], lb[i]), ub[i]);
    // for(int i=0;i<m;i++) p[i] = fminf(fmaxf(p[i], lb[i]), ub[i]);
    // fprintf(stderr, "[solve] adam: p: ");
    // for(int i=0;i<m;i++) fprintf(stderr, "%g\t", p[i]);
    // fprintf(stderr, "\n");
    // fprintf(stderr, "[solve] adam: m: ");
    // for(int i=0;i<m;i++) fprintf(stderr, "%g ", mt[i]);
    // fprintf(stderr, "\n");
    fprintf(stderr, "[solve %d/%d] adam: loss %g %a best %g\n", it, num_it, f[0], f[0], best);
    // if(f[0] <= 0.0) return f[0];
    if(abort && *abort) break;
  }
#if 1 // for stochastic gradient descent this is questionable. may only return whatever happened to be good for the subset seen at the time:
  if(best < f[0])
  {
    memcpy(p, bp, sizeof(double)*m);
    return best;
  }
#endif
  return f[0];
}

/*! A basic implementation of the method of Nelder and Mead for minimization of
  an objective function. The implementation follows the description in their
  paper exactly and uses the parameter values described there:
  John A. Nelder and Roger Mead 1965, A Simplex Method for Function
  Minimization, The Computer Journal 7:4, Oxford Academic, p. 308-313,
https://doi.org/10.1093/comjnl/7.4.308
returns the minimum objective function.
implemented by christoph peters. */
#define Float double
static inline Float
dt_nelder_mead(
    Float         *param,        // initial parameter vector, will be overwritten with result
    int            dim,          // dimensionality of the problem, i.e. number of parameters
    const int      num_it,       // number of iterations
    Float        (*objective)(Float *param, void *data),
    void          *data,         // data pointer passed to objective function
    int           *user_abort)   // check within iteration whether we should abort, if not null
{
  const Float reflection_coeff  = (Float)1.0; // The reflection coefficient alpha
  const Float contraction_coeff = (Float)0.5; // The contraction coefficient beta
  const Float expansion_coeff   = (Float)2.0; // The expansion coefficient gamma
  const Float shrink_coeff      = (Float)0.5; // The shrinking coefficient (implicitly 0.5 in the paper)
  Float *simplex_buf = malloc(sizeof(Float)*dim*(dim+1));
  Float (*simplex)[dim] = (Float (*)[dim])simplex_buf; // Construct the initial simplex using fixed axis-aligned offsets
  memcpy(simplex[0], param, sizeof(Float)*dim);
  for(uint32_t j = 0; j != dim; j++)
  {
    memcpy(simplex[j + 1], param, sizeof(Float)*dim);
    simplex[j + 1][j] += 0.05;
  }
  // Evaluate the objective at all simplex vertices:
  Float *simplex_value = malloc(sizeof(Float)*(dim+1));
  for(uint32_t j = 0; j < dim + 1; j++) simplex_value[j] = objective(simplex[j], data);

  Float *centroid   = malloc(sizeof(Float)*dim);
  Float *reflected  = malloc(sizeof(Float)*dim);
  Float *expanded   = malloc(sizeof(Float)*dim);
  Float *contracted = malloc(sizeof(Float)*dim);
  // We also keep track of the best value ever seen in case it is not part of
  // the simplex (which may be the case in presence of failed expansions)
  Float *best = malloc(sizeof(Float)*dim);
  memcpy(best, simplex[0], sizeof(Float)*dim);
  Float best_value = simplex_value[0];
  for(uint32_t i = 0; i < num_it; i++)
  { // Find indices of minimal and maximal value
    fprintf(stderr, "\r[nelder mead] %d/%d best value %g", i, num_it, best_value);
    if(user_abort && *user_abort) break;
    uint32_t min_index = 0, max_index = 0;
    Float min = simplex_value[0], max = simplex_value[0];
    for(uint32_t j = 1; j < dim + 1; j++)
    {
      min_index = (min > simplex_value[j]) ? j : min_index;
      min = (min > simplex_value[j]) ? simplex_value[j] : min;
      max_index = (max < simplex_value[j]) ? j : max_index;
      max = (max < simplex_value[j]) ? simplex_value[j] : max;
    }

    memset(centroid, 0, sizeof(Float)*dim); // compute the centroid of the facet opposite to the worst vertex
    for(uint32_t j = 0; j < dim+1; j++)
      if (j != max_index)
        for(uint32_t k = 0; k < dim; k++)
          centroid[k] += simplex[j][k];
    for(uint32_t k=0;k<dim;k++) centroid[k] *= 1.0 / dim;
    // compute the reflected point
    for(uint32_t k = 0; k < dim; k++) reflected[k] = (1.0 + reflection_coeff) * centroid[k] - reflection_coeff * simplex[max_index][k];
    Float reflected_value = objective(reflected, data);

    if (reflected_value < simplex_value[min_index])
    { // If the reflected point is better than the best vertex, we expand further in that direction
      for(uint32_t k=0;k<dim;k++) expanded[k] = expansion_coeff * reflected[k] + (1.0 - expansion_coeff) * centroid[k];
      Float expanded_value = objective(expanded, data);
      if(expanded_value < simplex_value[min_index])
      { // if the expanded point is also better than the best vertex, we replace the worst vertex by it
        memcpy(simplex[max_index], expanded, sizeof(Float)*dim);
        simplex_value[max_index] = expanded_value;
        if (reflected_value < best_value)
        { // If the reflected value is the best yet, we should not lose it
          memcpy(best, reflected, sizeof(Float)*dim);
          best_value = reflected_value;
        }
      }
      else
      { // Otherwise, we have a failed expansion and use the reflected value instead
        memcpy(simplex[max_index], reflected, sizeof(Float)*dim);
        simplex_value[max_index] = reflected_value;
      }
    }
    else
    { // Check whether replacing the worst vertex by the reflected vertex makes some other vertex the worst
      int reflected_still_worst = 1;
      for(uint32_t j = 0; j < dim + 1; j++)
        reflected_still_worst &= (j == max_index || simplex_value[j] < reflected_value);
      if (reflected_still_worst)
      { // If the reflected point is better than the worst vertex, use it anyway
        if (reflected_value < simplex_value[max_index])
        {
          memcpy(simplex[max_index], reflected, sizeof(Float)*dim);
          simplex_value[max_index] = reflected_value;
        }
        // Form a contracted point between worst vertex and centroid
        for(int k=0;k<dim;k++) contracted[k] = contraction_coeff * simplex[max_index][k] + (1.0 - contraction_coeff) * centroid[k];
        Float contracted_value = objective(contracted, data);
        if(contracted_value < simplex_value[max_index])
        { // ceplace the worst vertex by the contracted point if it makes an improvement
          memcpy(simplex[max_index], contracted, sizeof(Float)*dim);
          simplex_value[max_index] = contracted_value;
        }
        else
        { // otherwise, we shrink all vertices towards the best vertex
          for (uint32_t j = 0; j < dim + 1; ++j)
          {
            if (j != min_index)
            {
              for(int k=0;k<dim;k++) simplex[j][k] = shrink_coeff * simplex[j][k] + (1.0f - shrink_coeff) * simplex[min_index][k];
              simplex_value[j] = objective(simplex[j], data);
            }
          }
        }
      }
      else
      { // At least two vertices are worse than the reflected point, so we replace the worst vertex by it
        memcpy(simplex[max_index], reflected, sizeof(Float)*dim);
        simplex_value[max_index] = reflected_value;
      }
    }
  }
  for(uint32_t j = 0; j < dim + 1; j++)
  { // the best value ever observed is either a vertex of the simplex, or it has been stored in best_value
    if (simplex_value[j] < best_value)
    {
      memcpy(best, simplex[j], sizeof(Float)*dim);
      best_value = simplex_value[j];
    }
  }
  memcpy(param, best, sizeof(Float)*dim);
  free(simplex_buf);
  free(simplex_value);
  free(centroid);
  free(reflected);
  free(expanded);
  free(contracted);
  free(best);
  return best_value;
}
#undef Float
