#include "pll_tree.h"
#include "../pllmod_common.h"

#include "tree_hashtable.h"

typedef struct consensus_data
{
  pll_split_t split;
  unsigned int bit_count;
  double support;
} consensus_data_t;

static int cb_set_indices(pll_unode_t * node, void *data)
{
  string_hashtable_t * string_hashtable = (string_hashtable_t *) data;

  if (!node->next)
  {
    int index = string_hash_lookup(node->label, string_hashtable);
    if (index == -1)
      return 0;
    node->node_index = node->clv_index = (unsigned int) index;
  }

  return 1;
}

static void reverse_split(pll_split_t split, unsigned int tip_count);
static int is_subsplit(pll_split_t child,
                       pll_split_t parent,
                       unsigned int split_len);
static unsigned int setbit_count(pll_split_t split,
                                 unsigned int split_len);
static pll_unode_t * find_splitnode_recurse(pll_split_t split,
                                            pll_unode_t * root,
                                            unsigned int split_len);
static int get_split_id(pll_split_t split,
                        unsigned int split_len);
static pll_unode_t * create_consensus_node(pll_unode_t * parent,
                                           pll_split_t split,
                                           double support,
                                           unsigned int split_len);
static pll_unode_t * find_splitnode_recurse(pll_split_t split,
                                            pll_unode_t * root,
                                            unsigned int split_len);
static pll_split_t clone_split(const pll_split_t from,
                               unsigned int split_len);
static void recursive_assign_indices(pll_unode_t * node,
                                     unsigned int * inner_clv_index,
                                     int * inner_scaler_index,
                                     unsigned int * inner_node_index);
static void reset_template_indices(pll_unode_t * node,
                                   unsigned int tip_count);
static void build_tips_recurse(pll_unode_t * tree,
                               char **tip_labels,
                               unsigned int split_len);


static int cb_destroy_data(pll_unode_t * node, void * d)
{
  UNUSED(d);
  consensus_data_t * data = (consensus_data_t *) node->data;
  free(data->split);
  free(data);
  node->data = NULL;

  return PLL_SUCCESS;
}

PLL_EXPORT int pllmod_utree_compatible_splits(pll_split_t s1,
                                              pll_split_t s2,
                                              unsigned int split_len)
{
  unsigned int i;

  /* check conflicts between s1 and s2 */
  for(i = 0; i < split_len; i++)
    if(s1[i] & s2[i])
      break;

  if(i == split_len)
    return 1;

  /* check conflicts between s1 and ~s2 */
  for(i = 0; i < split_len; i++)
    if(s1[i] & ~s2[i])
      break;

  if(i == split_len)
    return 1;

  /* check conflicts between ~s1 and s2 */
  for(i = 0; i < split_len; i++)
    if(~s1[i] & s2[i])
      break;

  if(i == split_len)
    return 1;
  else
    return 0;
}

PLL_EXPORT pll_unode_t * pllmod_utree_from_splits(const pll_split_system_t * split_system,
                                                  unsigned int tip_count,
                                                  char **tip_labels)
{
  const pll_split_t * splits = split_system->splits;
  unsigned int split_size = sizeof(pll_split_base_t) * 8;
  unsigned int split_offset = tip_count % split_size;
  unsigned int split_len  = tip_count / split_size + (split_offset>0);
  unsigned int i;
  pll_unode_t * tree, * next_parent, * return_tree;
  pll_split_t rootsplit1, rootsplit2, next_split;
  double * support_values;
  pll_split_t * all_splits;

  if (split_system->split_count == 0)
  {
    // build star tree
    rootsplit1 = (pll_split_t) calloc(split_len,
                                      sizeof(pll_split_base_t));
    rootsplit1[0] = 1;
    rootsplit2 = clone_split(rootsplit1, split_len);
    reverse_split(rootsplit1, tip_count);

    tree = create_consensus_node(NULL, rootsplit1, 1.0, split_len);
    tree->back = create_consensus_node(NULL, rootsplit2, 1.0, split_len);
    tree->back->back = tree;

    for (i=1; i<tip_count; ++i)
    {
      unsigned int split_id   = i / split_size;
      next_split = (pll_split_t) calloc(split_len,
                                        sizeof(pll_split_base_t));
      next_split[split_id] = (1 << (i % split_size));
      pll_unode_t * next_n = create_consensus_node(tree,
                                                   next_split,
                                                   1.0,
                                                   split_len);
      assert(next_n);
    }

    return_tree = tree;
  }
  else
  {
    all_splits = (pll_split_t *) malloc((split_system->split_count + tip_count) *
                                                      sizeof(pll_split_t));
    support_values = (double *) malloc((split_system->split_count + tip_count) *
                                                      sizeof(double));
    memcpy(all_splits, splits, split_system->split_count * sizeof(pll_split_t));

    /* fill trivial splits */
    for (i=0; i<tip_count; ++i)
    {
      support_values[split_system->split_count+i] = 1.0;
      all_splits[split_system->split_count+i] = (pll_split_t) calloc(split_len,
                                                 sizeof(pll_split_base_t));
      {
        unsigned int split_id   = i / split_size;
        all_splits[split_system->split_count+i][split_id] = (1 << (i % split_size));
      }
    }

    /* compute support for other splits */
    for (i=0; i<split_system->split_count; ++i)
    {
      support_values[i] = 1.0 * split_system->support[i] / split_system->max_support;
    }

    /* create initial tree with 2 connected nodes of degree 1 */
    rootsplit1 = clone_split(all_splits[0], split_len);
    rootsplit2 = clone_split(all_splits[0], split_len);
    reverse_split(rootsplit2, tip_count);

    /* build tree out of the first split */
    tree = create_consensus_node(NULL, rootsplit1, support_values[0], split_len);
    tree->back = create_consensus_node(NULL, rootsplit2, support_values[0], split_len);
    tree->back->back = tree;

    return_tree = tree;

    /* add splits individually */
    for (i=1; i<(split_system->split_count+tip_count); ++i)
    {
      next_split = clone_split(all_splits[i], split_len);

      /* select branch */
      if (is_subsplit(next_split, rootsplit1, split_len))
        next_parent = tree;
      else if (is_subsplit(next_split, rootsplit2, split_len))
        next_parent = tree->back;
      else
      {
        reverse_split(next_split, tip_count);
        if (is_subsplit(next_split, rootsplit1, split_len))
          next_parent = tree;
        else if (is_subsplit(next_split, rootsplit2, split_len))
          next_parent = tree->back;
        else
        {
          pllmod_set_error(PLLMOD_TREE_ERROR_INVALID_SPLIT,
                           "Splits are incompatible");
          return_tree = NULL;
          break;
        }
      }

      /* select node */
      next_parent = find_splitnode_recurse(next_split, next_parent, split_len);
      assert(next_parent);

      /* create new node for the split*/
      create_consensus_node(next_parent, next_split, support_values[i], split_len);
    }

    /* clean */
    for (i=0; i<tip_count; ++i)
      free(all_splits[split_system->split_count+i]);
    free(all_splits);
  }

  build_tips_recurse(tree, tip_labels, split_len);
  if (tree->back)
    build_tips_recurse(tree->back, tip_labels, split_len);

  reset_template_indices(tree, tip_count);

  // pllmod_utree_traverse_apply(tree,
  //                             cb_destroy_data, /* pre  */
  //                             NULL,            /*  in  */
  //                             NULL,            /* post */
  //                             NULL);

  /* return_tree == tree if sucess, or null if the algorithm failed */
  return return_tree;
}

#define FCHUNK_LEN 2000

static pll_split_t * read_splits(FILE * file,
                                 int * n_chunks,
                                 unsigned int * tip_count,
                                 string_hashtable_t * string_hashtable)
{
  int line_size = *n_chunks * FCHUNK_LEN;
  char * tree_str = (char *) malloc((size_t)line_size);
  char * tree_str_ptr = tree_str;
  char * read;
  pll_split_t * splits;

  while ((read = fgets(tree_str_ptr, line_size, file)) &&
         tree_str_ptr[strlen(tree_str_ptr) - 1] != '\n')
  {
    /* increase line size */
    ++*n_chunks;
    tree_str = (char *) realloc(tree_str, (size_t)*n_chunks * FCHUNK_LEN);
    tree_str_ptr = tree_str + strlen(tree_str);
    line_size = FCHUNK_LEN;
  }

  if (read == NULL)
  {
    splits = NULL;
  }
  else
  {
      splits = pll_utree_split_newick_string(tree_str,
                                             *tip_count,
                                             string_hashtable);
  }
  free(tree_str);

  return splits;
}

static pll_utree_t * read_tree(FILE * file,
                               int * n_chunks,
                               unsigned int * tip_count,
                               string_hashtable_t * string_hashtable)
{
  int line_size = *n_chunks * FCHUNK_LEN;
  char * tree_str = (char *) malloc((size_t)line_size);
  char * tree_str_ptr = tree_str;
  char * read;
  pll_utree_t * utree;
  pll_unode_t * root;

  while ((read = fgets(tree_str_ptr, line_size, file)) &&
         tree_str_ptr[strlen(tree_str_ptr) - 1] != '\n')
  {
    /* increase line size */
    ++*n_chunks;
    tree_str = (char *) realloc(tree_str, (size_t)*n_chunks * FCHUNK_LEN);
    tree_str_ptr = tree_str + strlen(tree_str);
    line_size = FCHUNK_LEN;
  }

  if (read == NULL)
  {
    utree = NULL;
  }
  else
  {
    utree = pll_utree_parse_newick_string(tree_str);
    if (!utree)
    {
      return NULL;
    }
    *tip_count = utree->tip_count;
    root = utree->nodes[utree->tip_count + utree->inner_count - 1];

    if (root && string_hashtable)
    {
      if (!pllmod_utree_traverse_apply(root,
                                       NULL,
                                       NULL,
                                       &cb_set_indices,
                                       (void *)string_hashtable))
      {
        pllmod_set_error(
          PLLMOD_TREE_ERROR_INVALID_TREE,
          "Cannot match labels");
        pll_utree_destroy(utree, NULL);
        utree = NULL;
      }
    }
  }
  free(tree_str);

  return utree;
}

#undef FCHUNK_LEN

static FILE *get_number_of_trees(unsigned int *tree_count,
                                 const char *filename)
{
  FILE
    *f = fopen(filename, "r");

  if (!f)
    return NULL;

  unsigned int trees = 0;
  int ch;

  while((ch = fgetc(f)) != EOF)
    if(ch == ';')
      trees++;

  *tree_count = trees;

  rewind(f);

  return f;
}

/* reverse sort splits by weight */
static int sort_by_weight(const void *a, const void *b)
{
  int ca,
      cb;

  ca = ((*((bitv_hash_entry_t **)a))->support);
  cb = ((*((bitv_hash_entry_t **)b))->support);

  if (ca == cb)
    return 0;

  return ((ca<cb)?1:-1);
}

static void mre(bitv_hashtable_t *h,
                pll_split_system_t *consensus,
                unsigned int split_len,
                unsigned int max_splits)
{
  bitv_hash_entry_t **split_list;

  unsigned int
    i = 0,
    j = 0;

  /* queue all splits */

  split_list = (bitv_hash_entry_t **) malloc(sizeof(bitv_hash_entry_t *) *
                                             h->entry_count);

  j = 0;
  for(i = 0; i < h->table_size; i++) /* copy hashtable h to list sbw */
  {
    bitv_hash_entry_t * e =  h->table[i];
    while (e != NULL)
    {
      split_list[j] = e;
      ++j;
      e = e->next;
    }
  }
  assert(h->entry_count == j);

  /* sort by weight descending */
  qsort(split_list, h->entry_count, sizeof(bitv_hash_entry_t *), sort_by_weight);

  for(i = 0; i < h->entry_count && (consensus->split_count) < max_splits; i++)
  {
    int compatible = 1;
    bitv_hash_entry_t * split_candidate = split_list[i];
    for (j=0; j<consensus->split_count; ++j)
    {
      pll_split_t split_consolidated = consensus->splits[j];
      if (!pllmod_utree_compatible_splits(split_candidate->bit_vector,
                                          split_consolidated,
                                          split_len))
      {
        compatible = 0;
        break;
      }
    }

    if(compatible)
    {
      consensus->splits[consensus->split_count] = clone_split(split_candidate->bit_vector, split_len);
      consensus->support[consensus->split_count] = split_list[i]->support;
      ++(consensus->split_count);
    }
  }

  free(split_list);

  return;
}

/**
 * Build a consensus tree out of a set of trees in a file in NEWICK format
 *
 * @param  trees_filename   trees filename
 * @param  threshold        consensus threshold in [0,1].
 *                          1.0 -> strict
 *                          0.5 -> majority rule
 *                          0.0 -> extended majority rule
 * @param[out] tree_count   number of trees parsed
 * @param[out] min_support  minimum tree support
 * @return                  consensus unrooted tree structure
 */
PLL_EXPORT pll_unode_t * pllmod_utree_consensus(const char * trees_filename,
                                                double threshold,
                                                unsigned int * _tree_count,
                                                unsigned int * _min_support)
{
  FILE * trees_file;
  pll_utree_t * reference_tree = NULL; /* reference tree for consistency */
  pll_unode_t * consensus_tree = NULL; /* final consensus tree */
  pll_unode_t ** tipnodes;             /* tips from reference tree */
  bitv_hashtable_t * splits_hash = NULL;
  string_hashtable_t * string_hashtable = NULL;
  int  n_chunks = 1; /* chunks for reading newick trees */
  pll_split_t * tree_splits;
  unsigned int i,
               tip_count,
               n_splits,
               split_size = sizeof(pll_split_base_t) * 8,
               split_offset,
               split_len,
               tree_count,         /* number of trees */
               min_support,        /* minimum split support */
               thr_support,        /* threshold support */
               current_tree_index; /* for error management */

  /* validate threshold */
  if (threshold > 1)
  {
    pllmod_set_error(
      PLLMOD_TREE_ERROR_INVALID_THRESHOLD,
      "Invalid consensus threshold (%f). Should be in range [0.0,1.0]",
      threshold);
    return NULL;
  }

  /* open file */
  if (!(trees_file = get_number_of_trees(&tree_count, trees_filename)))
  {
    pllmod_set_error(PLL_ERROR_FILE_OPEN, "Cannot open trees file" );
    return NULL;
  }

  min_support = (unsigned int) ceil(threshold * tree_count);
  if (threshold > .5)
    thr_support = min_support;
  else
    thr_support = (unsigned int) floor(tree_count/2) + 1;

  if (_tree_count)
    *_tree_count = tree_count;
  if (_min_support)
    *_min_support = min_support;

  /* read first tree */
  reference_tree = read_tree(trees_file, &n_chunks, &tip_count, NULL);

  if(!reference_tree)
  {
    assert(pll_errno);
    fclose(trees_file);
    return NULL;
  }

  /* store taxa names */
  tipnodes = reference_tree->nodes;

  string_hashtable = string_hash_init(10 * tip_count, tip_count);

  for (i=0; i<tip_count; ++i)
  {
    string_hash_insert(tipnodes[i]->label,
                       string_hashtable,
                       (int) tipnodes[i]->node_index);
  }

  split_offset = tip_count % split_size;
  split_len  = tip_count / split_size + (split_offset>0);

  /* create hashtable */
  splits_hash = hash_init(tip_count * 10, tip_count);

  pll_errno = 0;
  current_tree_index = 1;

  tree_splits = pllmod_utree_split_create(reference_tree->nodes[
                                              reference_tree->tip_count +
                                              reference_tree->inner_count - 1],
                                           tip_count,
                                           &n_splits,
                                           NULL);

  while (tree_splits)
  {
    ++current_tree_index;

    /* insert normalized splits */
    for (i=0; i<n_splits; ++i)
    {
      bitv_normalize(tree_splits[i], tip_count);

      hash_insert(tree_splits[i],
                  splits_hash,
                  i,
                  HASH_KEY_UNDEF,
                  0);
    }
    pllmod_utree_split_destroy(tree_splits);

    /* parse next tree */
    tree_splits = read_splits(trees_file, &n_chunks, &tip_count, string_hashtable);
  }
  fclose(trees_file);

  if (pll_errno)
  {
    /* cleanup and spread error */
    char * aux_errmsg = (char *) malloc(strlen(pll_errmsg) + 1);
    strcpy(aux_errmsg, pll_errmsg);
    snprintf(pll_errmsg, PLLMOD_ERRMSG_LEN, "%s [tree #%d]",
                                            aux_errmsg,
                                            current_tree_index);
    free(aux_errmsg);
    string_hash_destroy(string_hashtable);
    hash_destroy(splits_hash);
    pll_utree_destroy(reference_tree, NULL);
    return NULL;
  }

  /* build final split system */
  unsigned int max_splits = tip_count - 3;
  unsigned int cons_split_count = 0;
  pll_split_system_t * split_system;

  split_system = (pll_split_system_t *) calloc(1, sizeof(pll_split_system_t));
  split_system->splits = (pll_split_t *) calloc(max_splits,
                                               sizeof(pll_split_t));
  split_system->support = (unsigned int *) calloc(max_splits,
                                                      sizeof(unsigned int *));
  split_system->split_count = 0;
  split_system->max_support = tree_count;

  for (i=0; i<splits_hash->table_size; ++i)
  {
    bitv_hash_entry_t * e =  splits_hash->table[i];
    bitv_hash_entry_t ** e_ptr = &(splits_hash->table[i]);
    while (e != NULL)
    {
      int delete_split = e->support < (int) min_support || e->support >= (int) thr_support;

      if (e->support >= (int) thr_support)
      {
        assert (split_system->split_count < max_splits);
        split_system->support[split_system->split_count] = e->support;
        split_system->splits[split_system->split_count++] = clone_split(e->bit_vector, split_len);
      }

      if (delete_split)
      {
        /* remove entry */
        hash_remove(splits_hash, e_ptr, e);

        e = *e_ptr;
      }
      else
      {
        e_ptr = &(e->next);
        e = e->next;
      }
    }
  }

  if (min_support < thr_support)
  {
    mre(splits_hash,
        split_system,
        split_len,
        max_splits);
  }
  hash_destroy(splits_hash);

  /* find set of splits compatible with the tree */

  /* add largest subset of compatible scripts */

  /* buld tree from splits */
  consensus_tree = pllmod_utree_from_splits(split_system,
                                      tip_count,
                                      string_hashtable->labels);

  /* cleanup */
  string_hash_destroy(string_hashtable);
  pll_utree_destroy(reference_tree, NULL);
  for (i=0; i<cons_split_count; ++i)
    free(split_system->splits[i]);
  free(split_system->splits);

  return consensus_tree;
}





/******************************************************************************/
/* static functions */

static unsigned int setbit_count(pll_split_t split,
                                 unsigned int split_len)
{
  unsigned int setb = 0;
  unsigned int i;

  for (i=0; i<split_len; ++i)
  {
    setb += (unsigned int) __builtin_popcount(split[i]);
  }
  return setb;
}

static int get_split_id(pll_split_t split,
                        unsigned int split_len)
{
  unsigned int n_bits = setbit_count(split, split_len);
  int i, base_id, ctz;
  int taxa_per_split = 8 * sizeof(split_len);
  int id = -1;

  if (n_bits != 1)
  {
    pllmod_set_error(PLLMOD_TREE_ERROR_INVALID_SPLIT,
                    "Invalid trivial split");
    return -1;
  }

  for (i=0; i<(int)split_len; ++i)
  {
    if (split[i])
    {
      base_id = i * taxa_per_split;
      ctz = (int)__builtin_ctz(split[i]);
      assert (ctz < taxa_per_split);
      id = base_id + ctz;
      break;
    }
  }

  /* if the assertion below fails, there is an error either in this algorithm,
     or in setbit_count. */
  assert(id != -1);

  return id;
}

static void build_tips_recurse(pll_unode_t * tree,
                               char ** tip_labels,
                               unsigned int split_len)
{
  consensus_data_t * data = (consensus_data_t *) tree->data;
  pll_unode_t * next_root;

  next_root = tree->next;
  if (next_root == tree)
  {
    /* create tips */
    int tip_id = get_split_id(data->split,
                              split_len);

    assert (tip_id != -1);

    if (tip_labels)
    {
      tree->label = (char *) malloc(strlen(tip_labels[tip_id])+1);
      strcpy(tree->label, tip_labels[tip_id]);
    }
    else
    {
      tree->label = NULL;
    }
    tree->node_index = (unsigned int) tip_id;
    tree->next = NULL;
  }
  else
  {
    while (next_root != tree)
    {
      /* recurse next branch */
      build_tips_recurse(next_root->back, tip_labels, split_len);
      next_root = next_root->next;
    }
  }
}

static pll_unode_t * find_splitnode_recurse(pll_split_t split,
                                            pll_unode_t * root,
                                            unsigned int split_len)
{
  pll_unode_t * next_root, * ret_node;
  consensus_data_t * data = (consensus_data_t *) root->data;

  if (is_subsplit(split, data->split, split_len))
  {
    /* check children */
    next_root = root->next;
    while(next_root != root)
    {
      if ((ret_node = find_splitnode_recurse(split,
                                 next_root->back,
                                 split_len)) != NULL)
      {
        return ret_node;
      }
      next_root = next_root->next;
    }
    return root;
  }
  else
  {
    return NULL;
  }
}

static void connect_consensus_node(pll_unode_t * parent,
                                   pll_unode_t * child,
                                   unsigned int split_len,
                                   int auto_rearrange)
{
  consensus_data_t * data_p, * data_c, * data_aux;
  pll_unode_t * new_node, * aux_node, * aux_node2;
  data_p = (consensus_data_t *) parent->data;
  data_c = (consensus_data_t *) child->data;

  assert(is_subsplit(data_c->split, data_p->split, split_len));
  if (child->back)
  {
    new_node = child->back;

    /* disconnect from parent */
    aux_node = new_node;
    assert(new_node->next != new_node);
    while(aux_node->next != child->back) aux_node = aux_node->next;

    aux_node->next = new_node->next;
    new_node->next = 0;
  }
  else
  {
    new_node = (pll_unode_t *) malloc (sizeof(pll_unode_t));
  }

  if (auto_rearrange)
  {
    aux_node = parent->next;
    while (aux_node != parent)
    {
      aux_node2 = aux_node->next;
      data_aux = (consensus_data_t *) aux_node->back->data;
      if (is_subsplit(data_aux->split, data_c->split, split_len))
      {
        connect_consensus_node(child, aux_node->back, split_len, 0);
      }
      aux_node = aux_node2;
    }
  }

  /* connect new node */
  new_node->data = data_p;
  new_node->next = parent->next;
  parent->next = new_node;

  /* connect child */
  new_node->back = child;
  child->back = new_node;

}

static pll_unode_t * create_consensus_node(pll_unode_t * parent,
                                           pll_split_t split,
                                           double support,
                                           unsigned int split_len)
{
  pll_unode_t * new_node = (pll_unode_t *) malloc (sizeof(pll_unode_t));
  consensus_data_t * data = (consensus_data_t *)
                                    malloc (sizeof(consensus_data_t));
  data->split     = split;
  data->support   = support;

  new_node->data  = data;
  new_node->label = NULL;
  new_node->back = NULL;

  /* self link */
  new_node->next = new_node;

  if (parent)
  {
    connect_consensus_node(parent, new_node, split_len, 1);
  }

  return new_node;
}

static int is_subsplit(pll_split_t child,
                       pll_split_t parent,
                       unsigned int split_len)
{
  unsigned int i;
  for (i=0; i<split_len; ++i)
  {
    if ((child[i] & parent[i]) != child[i])
      return 0;
  }
  return 1;
}

static void reverse_split(pll_split_t split, unsigned int tip_count)
{
  unsigned int split_size = sizeof(pll_split_base_t) * 8;
  unsigned int split_offset = tip_count % split_size;
  unsigned int split_len  = tip_count / split_size + (split_offset>0);
  unsigned int i;

  for (i=0; i<split_len; ++i)
    split[i] = ~split[i];

  unsigned int mask = (1<<split_offset) - 1;
  split[split_len - 1] &= mask;
}

static pll_split_t clone_split(const pll_split_t from,
                               unsigned int split_len)
{
  pll_split_t to = (pll_split_t) calloc(split_len, sizeof(pll_split_base_t));
  memcpy(to, from, sizeof(pll_split_base_t) * split_len);

  return to;
}

static void recursive_assign_indices(pll_unode_t * node,
                                    unsigned int * inner_clv_index,
                                    int * inner_scaler_index,
                                    unsigned int * inner_node_index)
{
  if (!node)
    return;

  if (!node->next)
  {
    node->clv_index = node->node_index;
    node->pmatrix_index = node->node_index;
    node->scaler_index = PLL_SCALE_BUFFER_NONE;
    return;
  }

  recursive_assign_indices(node->next->back,
                           inner_clv_index,
                           inner_scaler_index,
                           inner_node_index);

  recursive_assign_indices(node->next->next->back,
                           inner_clv_index,
                           inner_scaler_index,
                           inner_node_index);

  node->node_index = *inner_node_index;
  node->next->node_index = *inner_node_index + 1;
  node->next->next->node_index = *inner_node_index + 2;

  node->clv_index = *inner_clv_index;
  node->next->clv_index = *inner_clv_index;
  node->next->next->clv_index = *inner_clv_index;

  node->pmatrix_index = *inner_clv_index;
  node->next->pmatrix_index = node->next->back->pmatrix_index;
  node->next->next->pmatrix_index = node->next->next->back->pmatrix_index;

  node->scaler_index = *inner_scaler_index;
  node->next->scaler_index = *inner_scaler_index;
  node->next->next->scaler_index = *inner_scaler_index;

  *inner_clv_index = *inner_clv_index + 1;
  *inner_scaler_index = *inner_scaler_index + 1;
  *inner_node_index = *inner_node_index + 3;
}

static void reset_template_indices(pll_unode_t * node,
                                   unsigned int tip_count)
{
  unsigned int inner_clv_index = tip_count;
  unsigned int inner_node_index = tip_count;
  int inner_scaler_index = 0;

  if (pllmod_utree_is_tip(node))
  {
    node = node->back;
    assert(!pllmod_utree_is_tip(node));
  }

  if (node->back)
  recursive_assign_indices(node->back,
                           &inner_clv_index,
                           &inner_scaler_index,
                           &inner_node_index);

  recursive_assign_indices(node->next->back,
                           &inner_clv_index,
                           &inner_scaler_index,
                           &inner_node_index);

  recursive_assign_indices(node->next->next->back,
                           &inner_clv_index,
                           &inner_scaler_index,
                           &inner_node_index);

  node->node_index = inner_node_index;
  node->next->node_index = inner_node_index + 1;
  node->next->next->node_index = inner_node_index + 2;

  node->clv_index = inner_clv_index;
  node->next->clv_index = inner_clv_index;
  node->next->next->clv_index = inner_clv_index;

  node->scaler_index = inner_scaler_index;
  node->next->scaler_index = inner_scaler_index;
  node->next->next->scaler_index = inner_scaler_index;

  if (node->back)
    node->pmatrix_index = node->back->pmatrix_index;
  node->next->pmatrix_index = node->next->back->pmatrix_index;
  node->next->next->pmatrix_index = node->next->next->back->pmatrix_index;
}