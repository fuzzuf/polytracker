#include "polytracker/polytracker.h"
#include "polytracker/logging.h"
#include "polytracker/taint.h"
#include <atomic>
#include <inttypes.h>
#include <iostream>
#include <sanitizer/dfsan_interface.h>

extern bool polytracker_trace_func;
extern bool polytracker_trace;
extern thread_local FunctionStack function_stack;

// extern std::atomic_bool done;

extern "C" void __polytracker_log_taint_op(dfsan_label arg1, dfsan_label arg2,
                                           uint64_t arg1_value, uint64_t arg2_value,
                                           uint32_t findex, uint32_t bindex) {
  //  if (LIKELY(!done)) {
  if (LIKELY(polytracker_trace_func || polytracker_trace)) {
    if (arg1 != 0) {
      logOperation(arg1, findex, bindex);
    }
    if (arg2 != 0) {
      logOperation(arg2, findex, bindex);
    }
  }
  // }
}

// TODO: なんで argn が uint32_t だったの？
// その命令の種類くらいは教えてくれもいいじゃん…
extern "C" void __dfsw___polytracker_log_taint_op(
    uint64_t arg1, uint64_t arg2, uint32_t findex, uint32_t bindex,
    dfsan_label arg1_label, dfsan_label arg2_label, dfsan_label ignore_label1,
    dfsan_label ignore_label2) {
  __polytracker_log_taint_op(arg1_label, arg2_label, arg1, arg2, findex, bindex);
}

extern "C" void __polytracker_log_taint_cmp(dfsan_label arg1, dfsan_label arg2, 
                                            uint64_t arg1_value, uint64_t arg2_value,
                                            uint32_t findex, uint32_t bindex) {

  // if (LIKELY(!done)) {
  if (LIKELY(polytracker_trace_func || polytracker_trace)) {
    if (arg1 != 0) { // もしテイントしていたら
      logCompare(arg1, findex, bindex);
    } else {
      logComparedValue(arg1_value, findex, bindex);
    }
    if (arg2 != 0) { // もしテイントしていたら
      logCompare(arg2, findex, bindex);
    } else {
      logComparedValue(arg2_value, findex, bindex);
    }
  }
  // }
}

extern "C" void __dfsw___polytracker_log_taint_cmp(
    uint64_t arg1, uint64_t arg2, uint32_t findex, uint32_t bindex,
    dfsan_label arg1_label, dfsan_label arg2_label, dfsan_label ignore_label1,
    dfsan_label ignore_label2) {
  __polytracker_log_taint_cmp(arg1_label, arg2_label, arg1, arg2, findex, bindex);
}

extern "C" void __polytracker_log_taint_memory_access_operands(dfsan_label ptrval, dfsan_label idx1, 
                                            uint64_t ptrval_value, uint64_t idx1_value,
                                            uint32_t findex, uint32_t bindex) {

  if (LIKELY(polytracker_trace_func || polytracker_trace)) {
    if (idx1 != 0) { // もしテイントしていたら
      std::cout << "[*] __polytracker_log_taint_memory_access_operands: Tainted!" << std::endl;
      logMemoryAccessOperands(idx1, findex, bindex);
    }
  }
}

extern "C" void __dfsw___polytracker_log_taint_memory_access_operands(
    uint64_t ptrval, uint64_t idx1, uint32_t findex, uint32_t bindex,
    dfsan_label ptrval_label, dfsan_label idx1_label, dfsan_label ignore_label1,
    dfsan_label ignore_label2) {
  __polytracker_log_taint_memory_access_operands(ptrval_label, idx1_label, ptrval, idx1, findex, bindex);
}

extern "C" int __polytracker_log_func_entry(char *fname, uint32_t index,
                                            uint32_t block_index) {
  // if (LIKELY(!done)) {
  return logFunctionEntry(fname, index);
  // }
}

// TODO (Carson) we can use this block index if we need to.
extern "C" void __polytracker_log_func_exit(uint32_t func_index,
                                            uint32_t block_index,
                                            const int stack_loc) {
  // if (LIKELY(!done)) {
  logFunctionExit(func_index, stack_loc);
  // }
}

extern "C" void __polytracker_log_call_exit(uint32_t func_index,
                                            uint32_t block_index,
                                            const int stack_loc) {
  logCallExit(func_index, stack_loc);
}

extern "C" void __polytracker_log_call_uninst(uint32_t func_index,
                                              uint32_t block_index,
                                              char *fname) {
  logCallUninst(func_index, block_index, fname);
}

extern "C" void __polytracker_log_call_indirect(uint32_t func_index,
                                                uint32_t block_index) {
  logCallIndirect(func_index, block_index);
}

extern "C" void __polytracker_log_bb_entry(char *name, uint32_t findex,
                                           uint32_t bindex, uint8_t btype) {
  // if (polytracker_trace && LIKELY(!done)) {
  if (polytracker_trace) {
    logBBEntry(name, findex, bindex, btype);
  }
}

extern "C" atomic_dfsan_label *
__polytracker_union_table(const dfsan_label &l1, const dfsan_label &l2) {
  // if (LIKELY(!done)) {
  try {
    return getUnionEntry(l1, l2);
  } catch (std::exception &e) {
    return nullptr;
  }
  //}
  // return nullptr;
}

extern "C" dfsan_label_info
__polytracker_get_label_info(const dfsan_label &l1) {
  taint_node_t *node = getTaintNode(l1);
  return {node->p1, node->p2, nullptr, nullptr};
}

extern "C" void __polytracker_log_union(const dfsan_label &l1,
                                        const dfsan_label &l2,
                                        const dfsan_label &union_label) {
  // Note (Carson), we don't really have control over decay anymore.
  // if (LIKELY(!done)) {
  logUnion(l1, l2, union_label, 100);
  // }
}

extern "C" int __polytracker_size() { return function_stack.size(); }

extern "C" void __polytracker_start() { polytracker_start(); }

extern "C" void dfs$__polytracker_log_call_exit(uint32_t func_index,
                                                uint32_t block_index,
                                                const int stack_loc) {
  fprintf(stdout, "WARNING Using instrumented log call exit func\n");
  __polytracker_log_call_exit(func_index, block_index, stack_loc);
}

// These two dfs$ functions exist for testing
// If polytracker-llvm needs an update but it's too time consuming to
// rebuild/wait
extern "C" void dfs$__polytracker_start() {
  fprintf(stderr, "WARNING Using instrumented internal start func\n");
  __polytracker_start();
}
extern "C" int dfs$__polytracker_size() {
  fprintf(stderr, "WARNING Using instrumented internal size func\n");
  return __polytracker_size();
}

extern "C" void __polytracker_print_label(dfsan_label l1) {
  printf("label from inst is: %" PRIu32 "\n", l1);
}

extern "C" void __polytracker_dump(const dfsan_label last_label) {}

extern "C" int __polytracker_has_label(dfsan_label label, dfsan_label elem) {
  return false;
}