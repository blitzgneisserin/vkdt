#include "modules/api.h"

void modify_roi_in(
    dt_graph_t  *graph,
    dt_module_t *module)
{
  module->connector[0].roi.wd = module->connector[0].roi.full_wd;
  module->connector[0].roi.ht = module->connector[0].roi.full_ht;
  module->connector[0].roi.scale = 1.0f;
  module->connector[1].roi.wd = module->connector[0].roi.full_wd;
  module->connector[1].roi.ht = module->connector[0].roi.full_ht;
  module->connector[1].roi.scale = 1.0f;
}

void modify_roi_out(
    dt_graph_t  *graph,
    dt_module_t *module)
{
  const int wd = 64;//256;
  module->connector[2].roi.full_wd = wd;
  module->connector[2].roi.full_ht = wd;
  module->connector[3].roi.full_wd = wd;
  module->connector[3].roi.full_ht = wd;
}

void create_nodes(
    dt_graph_t  *graph,
    dt_module_t *module)
{
  const int id = dt_node_add(
      graph, module, "cnngenin", "main", 
      module->connector[2].roi.wd, module->connector[2].roi.ht, 1,
      0, 0, 4,
      "imgi", "read",  "*",    "*",   &module->connector[0].roi,
      "refi", "read",  "*",    "*",   &module->connector[1].roi,
      "imgo", "write", "rgba", "f16", &module->connector[2].roi,
      "refo", "write", "rgba", "f16", &module->connector[3].roi);
  for(int i=0;i<4;i++) dt_connector_copy(graph, module, i, id, i);
}
