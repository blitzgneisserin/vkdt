#pragma once
// https://stackoverflow.com/questions/16022445/access-symbols-inside-application-from-within-shared-object-windows-mingw
#ifdef VKDT_DSO_BUILD
#ifdef _WIN64
#include <windows.h>
#include <stdint.h>
#include <stdio.h>

#define DECLARE_FUNC(type,name,par) typedef type (WINAPI *name ## _t)par; static name ## _t name;
#define DECLARE_VAR(type,name) static type * p ## name;
#define DECLARE_PTR(type,name) static type *name;

#define LOAD_FUNCC(name) if((name=(name ## _t)GetProcAddress(GetModuleHandle(NULL),#name))==NULL) return 1
#define LOAD_VARC(type,name) if((p ## name=(type *)GetProcAddress(GetModuleHandle(NULL),#name))==NULL) return 1
#define LOAD_PTRC(type,name) if((name=*(type **)GetProcAddress(GetModuleHandle(NULL),#name))==NULL) return 1

// forward declare stuff
typedef uint64_t dt_token_t;
typedef struct dt_graph_t dt_graph_t;
typedef struct dt_pipe_global_t dt_pipe_global_t;
typedef struct qvk_t qvk_t;
typedef struct dt_log_t dt_log_t;
// now load the symbols for the public api:
DECLARE_FUNC(int,   dt_node_connect,    (dt_graph_t *graph, int n0, int c0, int n1, int c1));
DECLARE_FUNC(int,   dt_node_feedback,   (dt_graph_t *graph, int n0, int c0, int n1, int c1));
DECLARE_FUNC(int,   dt_module_connect,  (dt_graph_t *graph, int n0, int c0, int n1, int c1));
DECLARE_FUNC(int,   dt_module_feedback, (dt_graph_t *graph, int n0, int c0, int n1, int c1));
DECLARE_FUNC(int,   dt_module_remove,   (dt_graph_t *graph, const int mid));
DECLARE_FUNC(int,   dt_module_add,      (dt_graph_t *graph, dt_token_t name, dt_token_t inst));
DECLARE_FUNC(char*, dt_graph_write_connection_ascii, (dt_graph_t *graph, const int m, const int i, char *line, size_t size));
DECLARE_FUNC(char*, dt_graph_write_param_ascii,  (const dt_graph_t *graph, const int m, const int p, char *line, size_t size, char **eop));
DECLARE_FUNC(char*, dt_graph_write_module_ascii, (const dt_graph_t *graph, const int m, char *line, size_t size));
DECLARE_VAR(dt_pipe_global_t, dt_pipe);
DECLARE_VAR(dt_log_t,         dt_log_global);
DECLARE_VAR(qvk_t,            qvk);
#define dt_pipe       (*pdt_pipe)
#define dt_log_global (*pdt_log_global)
#define qvk           (*pqvk)

// will be executed dso-side as bs_init() if they include api.h
static inline int
dt_module_bs_init()
{
  LOAD_VARC(dt_pipe_global_t, dt_pipe);
  LOAD_VARC(qvk_t, qvk);
  LOAD_VARC(dt_log_t, dt_log_global);
  LOAD_FUNCC(dt_node_connect);
  LOAD_FUNCC(dt_node_feedback);
  LOAD_FUNCC(dt_module_connect);
  LOAD_FUNCC(dt_module_feedback);
  LOAD_FUNCC(dt_module_remove);
  LOAD_FUNCC(dt_module_add);
  LOAD_FUNCC(dt_graph_write_connection_ascii);
  LOAD_FUNCC(dt_graph_write_param_ascii);
  LOAD_FUNCC(dt_graph_write_module_ascii);
  return 0;
}
#endif
#endif
