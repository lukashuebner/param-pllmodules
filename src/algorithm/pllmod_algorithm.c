#include "pllmod_algorithm.h"
#include "algo_callback.h"

static void fill_rates   (double *rates,
                          double *x,
                          int *bt, double *lb, double *ub,
                          double min_rate, double max_rate,
                          unsigned int n_rates);

static void fill_weights (double *weights,
                          unsigned int * highest_weight_index,
                          double *x, int *bt, double *lb, double *ub,
                          unsigned int n_weights);


PLL_EXPORT double pllmod_algo_opt_frequencies (pll_partition_t * partition,
                                               pll_utree_t * tree,
                                               unsigned int params_index,
                                               unsigned int * params_indices,
                                               double bfgs_factor,
                                               double tolerance)
{
  double cur_logl;
  double *x, *lb, *ub;
  int *bt;
  unsigned int i;
  struct freqs_params opt_params;
  opt_params.partition      = partition;
  opt_params.tree           = tree;
  opt_params.params_indices = params_indices;
  opt_params.params_index   = params_index;

  double * frequencies = partition->frequencies[params_index];
  unsigned int states  = partition->states;
  unsigned int cur_index;

  const double factor = bfgs_factor > 0. ? bfgs_factor : PLLMOD_ALGO_BFGS_FACTR;

  x  = (double *) malloc(sizeof(double) * (states - 1));
  lb = (double *) malloc(sizeof(double) * (states - 1));
  ub = (double *) malloc(sizeof(double) * (states - 1));
  bt = (int *)    malloc(sizeof(int)    * (states - 1));

  /* find highest frequency */
  opt_params.highest_freq_state = 0;
  for (i = 1; i < states; ++i)
    if (frequencies[i] > frequencies[opt_params.highest_freq_state])
      opt_params.highest_freq_state = i;

  cur_index = 0;
  for (i = 0; i < states; ++i)
  {
    if (i != opt_params.highest_freq_state)
    {
      x[cur_index] = frequencies[i]
        / frequencies[opt_params.highest_freq_state];
      lb[cur_index] = PLLMOD_OPT_MIN_FREQ;
      ub[cur_index] = PLLMOD_OPT_MAX_FREQ;
      bt[cur_index] = PLLMOD_OPT_LBFGSB_BOUND_BOTH;
      cur_index++;
    }
  }

  cur_logl = pllmod_opt_minimize_lbfgsb(x, lb, ub, bt,
                                 states-1,
                                 factor, tolerance,
                                 (void *) &opt_params,
                                 &target_freqs_func);

  /* update frequencies */
  target_freqs_func((void *)&opt_params, x);

  free(x);
  free(lb);
  free(ub);
  free(bt);

  return cur_logl;
}

PLL_EXPORT double pllmod_algo_opt_subst_rates (pll_partition_t * partition,
                                               pll_utree_t * tree,
                                               unsigned int params_index,
                                               unsigned int * params_indices,
                                               int * symmetries,
                                               double min_rate,
                                               double max_rate,
                                               double bfgs_factor,
                                               double tolerance)
{
  double cur_logl;
  double *x, *lb, *ub;
  int *bt;
  unsigned int i, j, k;

  double *subst_rates    = partition->subst_params[params_index];
  unsigned int states    = partition->states;
  unsigned int subst_params = (states * (states-1)) / 2;
  unsigned int subst_free_params;

  const double factor = bfgs_factor > 0. ? bfgs_factor : PLLMOD_ALGO_BFGS_FACTR;

  if (!symmetries)
  {
    subst_free_params = subst_params - 1;
  }
  else
  {
    subst_free_params = 0;
    for (i=0; i<subst_params; ++i)
    {
     if ((unsigned int)symmetries[i] > subst_free_params)
     {
      /* check that symmetries vector is correctly formatted */
      assert((unsigned int)symmetries[i] == (subst_free_params+1));
      ++subst_free_params;
     }
    }
  }

  struct algo_subst_params opt_params;
  opt_params.partition         = partition;
  opt_params.tree              = tree;
  opt_params.params_index      = params_index;
  opt_params.params_indices    = params_indices;
  opt_params.symmetries        = symmetries;
  opt_params.subst_free_params = subst_free_params;

  x  = (double *) malloc(sizeof(double) * (subst_free_params));
  lb = (double *) malloc(sizeof(double) * (subst_free_params));
  ub = (double *) malloc(sizeof(double) * (subst_free_params));
  bt = (int *)    malloc(sizeof(int)    * (subst_free_params));

  k = 0;
  for (i = 0; i < subst_free_params; ++i)
  {
    bt[i] = PLLMOD_OPT_LBFGSB_BOUND_BOTH;
    lb[i] = min_rate;
    ub[i] = max_rate;

    if (symmetries)
    {
      if ((unsigned int)symmetries[subst_params-1] == k)
        ++k;

      for (j=0; j<subst_params; ++j)
      {
        if ((unsigned int)symmetries[j] == k)
        {
          x[i] = subst_rates[j];
          break;
        }
      }
      ++k;
    }
    else
    {
      x[i] = subst_rates[i];
    }

    if (x[i] < min_rate || x[i] > max_rate)
      x[i] = (max_rate + min_rate)/2;
  }

  cur_logl = pllmod_opt_minimize_lbfgsb(x, lb, ub, bt,
                                 subst_free_params,
                                 factor, tolerance,
                                 (void *) &opt_params, target_subst_params_func);

  free(x);
  free(lb);
  free(ub);
  free(bt);

  return cur_logl;
}

PLL_EXPORT double pllmod_algo_opt_alpha (pll_partition_t * partition,
                                         pll_utree_t * tree,
                                         unsigned int * params_indices,
                                         double min_alpha,
                                         double max_alpha,
                                         double *alpha,
                                         double tolerance)
{
  double cur_logl;
  double f2x;
  double xres;

  struct default_params opt_params;
  opt_params.partition      = partition;
  opt_params.tree           = tree;
  opt_params.params_indices = params_indices;

  xres = pllmod_opt_minimize_brent(min_alpha, *alpha, max_alpha,
                            tolerance,
                            &cur_logl,
                            &f2x,
                            (void *) &opt_params,
                            &target_alpha_func);

  cur_logl = target_alpha_func(&opt_params, xres);
  *alpha = xres;

  return cur_logl;
}

PLL_EXPORT double pllmod_algo_opt_pinv (pll_partition_t * partition,
                                        pll_utree_t * tree,
                                        unsigned int * params_indices,
                                        double min_pinv,
                                        double max_pinv,
                                        double tolerance)
{
  double cur_logl;
  double f2x;
  double xres;
  double start_pinv;
  struct default_params opt_params;
  opt_params.partition      = partition;
  opt_params.tree           = tree;
  opt_params.params_indices = params_indices;
  start_pinv = partition->prop_invar[params_indices[0]];

  xres = pllmod_opt_minimize_brent(min_pinv,
                            start_pinv,
                            max_pinv,
                            tolerance,
                            &cur_logl,
                            &f2x,
                            (void *) &opt_params,
                            &target_pinv_func);

  cur_logl = target_pinv_func(&opt_params, xres);

  return cur_logl;
}

PLL_EXPORT double pllmod_algo_opt_alpha_pinv (pll_partition_t * partition,
                                              pll_utree_t * tree,
                                              unsigned int * params_indices,
                                              double min_alpha,
                                              double max_alpha,
                                              double *alpha,
                                              double min_pinv,
                                              double max_pinv,
                                              double bfgs_factor,
                                              double tolerance)
{
  double cur_logl;
  double x[2], lb[2], ub[2];
  int bt[2];

  const double factor = bfgs_factor > 0. ? bfgs_factor : PLLMOD_ALGO_BFGS_FACTR;

  struct default_params opt_params;
  opt_params.partition      = partition;
  opt_params.tree           = tree;
  opt_params.params_indices = params_indices;

  /* init alpha */
  x[0] = *alpha;
  lb[0] = min_alpha ? min_alpha : PLLMOD_OPT_MIN_ALPHA;
  ub[0] = max_alpha ? max_alpha : PLLMOD_OPT_MAX_ALPHA;
  bt[0] = PLLMOD_OPT_LBFGSB_BOUND_BOTH;

  /* init p-inv */
  x[1] = partition->prop_invar[params_indices[0]];
  lb[1] = min_pinv > PLLMOD_ALGO_LBFGSB_ERROR ? min_pinv :
      PLLMOD_OPT_MIN_PINV + PLLMOD_ALGO_LBFGSB_ERROR;
  ub[1] = max_pinv ? max_pinv : PLLMOD_OPT_MAX_PINV;
  bt[1] = PLLMOD_OPT_LBFGSB_BOUND_BOTH;

  cur_logl = pllmod_opt_minimize_lbfgsb(x, lb, ub, bt,
                                 2,
                                 factor, tolerance,
                                 (void *) &opt_params,
                                 &target_alpha_pinv_func);

  /* save optimal alpha (p-inv is stored in the partition) */
  *alpha = x[0];

  return cur_logl;
}

PLL_EXPORT double pllmod_algo_opt_brlen_scaler (pll_partition_t * partition,
                                                pll_utree_t * tree,
                                                unsigned int * params_indices,
                                                double * scaler,
                                                double min_scaler,
                                                double max_scaler,
                                                double tolerance)
{
  double cur_logl;
  double f2x;
  double xres;
  struct brlen_scaler_params opt_params;

  /* create a temporary tree with the scaled branches */
  pll_utree_t * scaled_tree = pll_utree_clone(tree);
  pllmod_utree_scale_branches(scaled_tree, *scaler);

  opt_params.partition      = partition;
  opt_params.tree           = scaled_tree;
  opt_params.params_indices = params_indices;
  opt_params.old_scaler     = *scaler;

  xres = pllmod_opt_minimize_brent(min_scaler,
                                   *scaler,
                                   max_scaler,
                                   tolerance,
                                   &cur_logl,
                                   &f2x,
                                   (void *) &opt_params,
                                   &target_brlen_scaler_func);

  cur_logl = target_brlen_scaler_func(&opt_params, xres);

  pll_utree_destroy(scaled_tree);

  *scaler = xres;

  return cur_logl;
}

PLL_EXPORT double pllmod_algo_opt_rates_weights (pll_partition_t * partition,
                                                 pll_utree_t * tree,
                                                 unsigned int * params_indices,
                                                 double min_rate,
                                                 double max_rate,
                                                 double bfgs_factor,
                                                 double tolerance,
                                                 double * brlen_scaler,
                                                 int scale_branches)
{
  double cur_logl, prev_logl;
  double sum_weightrates, rate_scaler;
  double *x, *lb, *ub;
  int *bt;
  unsigned int i;

  double *rates          = partition->rates;
  double *weights        = partition->rate_weights;
  unsigned int rate_cats = partition->rate_cats;

  struct rate_weights_params opt_params;
  opt_params.partition      = partition;
  opt_params.tree           = tree;
  opt_params.params_indices = params_indices;

  const double factor = bfgs_factor > 0. ? bfgs_factor : PLLMOD_ALGO_BFGS_FACTR;

  x  = (double *) malloc(sizeof(double) * (rate_cats));
  lb = (double *) malloc(sizeof(double) * (rate_cats));
  ub = (double *) malloc(sizeof(double) * (rate_cats));
  bt = (int *)    malloc(sizeof(int)    * (rate_cats));

  /* 2 step BFGS */

  cur_logl = 0;
  do
  {
    prev_logl = cur_logl;

    /* optimize mixture weights */

    fill_weights(weights, &(opt_params.highest_weight_state), x,
                      bt, lb, ub, rate_cats);

    cur_logl = 1
        * pllmod_opt_minimize_lbfgsb (x, lb, ub, bt, rate_cats-1,
                                      factor, tolerance,
                                      (void *) &opt_params,
                                      target_weights_func);

    /* optimize mixture rates */

    fill_rates (rates,
                x, bt, lb, ub,
                min_rate, max_rate,
                rate_cats);

    cur_logl = pllmod_opt_minimize_lbfgsb(x, lb, ub, bt,
                                          rate_cats,
                                          factor, tolerance,
                                          (void *) &opt_params,
                                          target_rates_func);

  } while (!prev_logl || prev_logl - cur_logl > tolerance);

  /* force constraint sum(weights x rates) = 1.0 */
  sum_weightrates = 0.0;
  for (i=0; i<rate_cats; ++i)
    sum_weightrates += rates[i] * weights[i];
  rate_scaler = 1.0 / sum_weightrates;

  for (i=0; i<rate_cats; ++i)
    rates[i] *= rate_scaler;

  *brlen_scaler = sum_weightrates;

  if (scale_branches)
  {
    /* scale branch lengths such that likelihood is conserved */
    pllmod_utree_scale_branches(tree, sum_weightrates);

    /* update pmatrices and partials according to the new branches */
    cur_logl = -1 *
               pllmod_utree_compute_lk(partition,
                                       tree,
                                       params_indices,
                                       1,   /* update pmatrices */
                                       1);  /* update partials */
  }

  free(x);
  free(lb);
  free(ub);
  free(bt);

  return cur_logl;
}

/* STATIC FUNCTIONS */

static void fill_rates   (double *rates,
                          double *x,
                          int *bt, double *lb, double *ub,
                          double min_rate,
                          double max_rate,
                          unsigned int n_rates)
{
  unsigned int i;

  assert (min_rate > 1e-4 && max_rate > min_rate);

  for (i = 0; i < n_rates; ++i)
  {
    bt[i] = PLLMOD_OPT_LBFGSB_BOUND_BOTH;
    lb[i] = min_rate;
    ub[i] = max_rate;

    if (rates[i] < min_rate || rates[i] > max_rate)
      x[i] = (max_rate + min_rate)/2;
    else
      x[i] = rates[i];
  }
}

static void fill_weights (double *weights,
                          unsigned int * highest_weight_index,
                          double *x,
                          int *bt,
                          double *lb,
                          double *ub,
                          unsigned int n_weights)
{
  unsigned int i, cur_index = 0;

  *highest_weight_index = 0;
  for (i = 1; i < n_weights; ++i)
    if (weights[i] > weights[*highest_weight_index])
      *highest_weight_index = i;

  for (i = 0; i < n_weights; ++i)
  {
    if (i != *highest_weight_index)
    {
      bt[cur_index] = PLLMOD_OPT_LBFGSB_BOUND_BOTH;

      double r = weights[i] / weights[*highest_weight_index];
      lb[cur_index] = PLLMOD_ALGO_MIN_WEIGHT_RATIO;
      ub[cur_index] = PLLMOD_ALGO_MAX_WEIGHT_RATIO;
      x[cur_index] = (r >= lb[cur_index] && r <= ub[cur_index]) ? r : 1.0;
      cur_index++;
    }
  }
}
