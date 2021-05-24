#ifndef POLYTRACKER_LOGGING
#define POLYTRACKER_LOGGING
#include <stack>
#include <unordered_map>

#include "dfsan_types.h"
#include "polytracker/output.h"

[[nodiscard]] taint_node_t *getTaintNode(dfsan_label label);
[[nodiscard]] dfsan_label getTaintLabel(taint_node_t *node);

void logCompare(const dfsan_label label, const function_id_t findex,
                const block_id_t bindex);
void logOperation(const dfsan_label label, const function_id_t findex,
                  const block_id_t bindex);
int logFunctionEntry(const char *fname, const function_id_t func_id);
void logFunctionExit(const function_id_t index, const int stack_loc);
void logBBEntry(const char *fname, const function_id_t findex,
                const block_id_t bindex, const uint8_t btype);

struct FunctionStackFrame {
  event_id_t func_event_id;
  function_id_t func_id;
  std::unordered_map<block_id_t, block_entry_count_t> bb_entry_count;
};

using FunctionStack = std::stack<FunctionStackFrame>;

#define LIKELY(x) __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)
#endif
