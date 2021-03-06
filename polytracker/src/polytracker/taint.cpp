
#include "polytracker/taint.h"
#include "polytracker/dfsan_types.h"
#include "polytracker/logging.h"
#include "polytracker/output.h"
#include <iostream>
#include <map>
#include <mutex>
#include <sanitizer/dfsan_interface.h>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

extern decay_val taint_node_ttl;
#define TAINT_GRANULARITY 1

std::unordered_map<dfsan_label, std::unordered_map<dfsan_label, dfsan_label>>
    union_table;
std::mutex union_table_lock;

std::unordered_map<uint64_t, atomic_dfsan_label> new_table;
std::mutex new_table_lock;

std::unordered_map<int, std::string> fd_name_map;
std::unordered_map<std::string, std::pair<int, int>> track_target_name_map;
std::unordered_map<int, std::pair<int, int>> track_target_fd_map;
std::mutex track_target_map_lock;

extern sqlite3 *output_db;
extern input_id_t input_id;
extern thread_local int thread_id;
extern thread_local block_id_t curr_block_index;
extern thread_local function_id_t curr_func_index;
extern std::atomic<event_id_t> event_id;
extern thread_local event_id_t thread_event_id;
extern thread_local FunctionStack function_stack;
extern char *forest_mem;
extern std::unordered_set<std::string> target_sources;
extern uint64_t byte_start;
extern uint64_t byte_end;
extern bool polytracker_trace;

void checkMaxLabel(dfsan_label label) {
  if (label == MAX_LABELS) {
    std::cout << "ERROR: MAX LABEL REACHED, ABORTING!" << std::endl;
    // Cant exit due to our exit handlers
    abort();
  }
}

[[nodiscard]] bool isTrackingSource(const std::string &fd) {
  if (target_sources.empty()) {
    return true;
  }
  const std::lock_guard<std::mutex> guard(track_target_map_lock);
  if (track_target_name_map.find(fd) != track_target_name_map.end()) {
    return true;
  }
  return false;
}

[[nodiscard]] bool isTrackingSource(const int &fd) {
  if (target_sources.empty()) {
    return true;
  }
  const std::lock_guard<std::mutex> guard(track_target_map_lock);
  if (track_target_fd_map.find(fd) != track_target_fd_map.end()) {
    return true;
  }
  return false;
}

void closeSource(const std::string &fd) {
  if (!target_sources.empty()) {
    const std::lock_guard<std::mutex> guard(track_target_map_lock);
    if (track_target_name_map.find(fd) != track_target_name_map.end()) {
      track_target_name_map.erase(fd);
    }
  }
}

void closeSource(const int &fd) {
  if (!target_sources.empty()) {
    const std::lock_guard<std::mutex> guard(track_target_map_lock);
    if (track_target_fd_map.find(fd) != track_target_fd_map.end()) {
      track_target_fd_map.erase(fd);
    }
  }
}

// This will be called by polytracker to add new taint source info.
void addInitialTaintSource(std::string &fd, const int start, const int end,
                           std::string &name) {
  const std::lock_guard<std::mutex> guard(track_target_map_lock);
  track_target_name_map[fd] = std::make_pair(start, end);
}

void addInitialTaintSource(int fd, const int start, const int end,
                           std::string &name) {
  const std::lock_guard<std::mutex> guard(track_target_map_lock);
  track_target_fd_map[fd] = std::make_pair(start, end);
  track_target_name_map[name] = std::make_pair(start, end);
}

void addDerivedSource(std::string &track_path, const int &new_fd) {
  const std::lock_guard<std::mutex> guard(track_target_map_lock);
  track_target_fd_map[new_fd] = track_target_name_map[track_path];
  fd_name_map[new_fd] = track_path;
  // Store input if no taints have been specified/no input id has been created.
  if (target_sources.empty()) {
    input_id = storeNewInput(output_db, track_path, byte_start, byte_end,
                             polytracker_trace);
  }
}

auto getSourceName(const int &fd) -> std::string & {
  const std::lock_guard<std::mutex> guard(track_target_map_lock);
  if (fd_name_map.find(fd) == fd_name_map.end()) {
    std::cerr << "Error: source name for fd " << fd << "not found" << std::endl;
    // Kill the run, somethings gone pretty wrong
    abort();
  }
  return fd_name_map[fd];
}

[[nodiscard]] inline auto getTargetTaintRange(const std::string &fd)
    -> std::pair<int, int> & {
  const std::lock_guard<std::mutex> guard(track_target_map_lock);
  return track_target_name_map[fd];
}
[[nodiscard]] inline auto getTargetTaintRange(const int &fd)
    -> std::pair<int, int> & {
  const std::lock_guard<std::mutex> guard(track_target_map_lock);
  return track_target_fd_map[fd];
}

[[nodiscard]] static inline dfsan_label
createCanonicalLabel(const int file_byte_offset, std::string &name) {
  dfsan_label new_label = dfsan_create_label(nullptr, nullptr);
  checkMaxLabel(new_label);
  taint_node_t *new_node = getTaintNode(new_label);
  new_node->p1 = 0;
  new_node->p2 = 0;
  new_node->decay = taint_node_ttl;
  storeCanonicalMap(output_db, input_id, new_label, file_byte_offset);
  storeTaintForestNode(output_db, input_id, new_label, 0, 0);
  return new_label;
}

[[nodiscard]] dfsan_label createReturnLabel(const int file_byte_offset,
                                            std::string &name) {
  dfsan_label ret_label = createCanonicalLabel(file_byte_offset, name);
  storeTaintedChunk(output_db, input_id, file_byte_offset, file_byte_offset);
  return ret_label;
}

/*
 * This function is responsible for marking memory locations as tainted, and is
 * called when taint is processed by functions like read, pread, mmap, recv,
 * etc.
 *
 * Mem is a pointer to the data we want to taint
 * Offset tells us at what point in the stream/file we are in (before we read)
 * Len tells us how much we just read in
 * byte_start and byte_end are target specific options that allow us to only
 * taint specific regions like (0-100) etc etc
 *
 * If a byte is supposed to be tainted we make a new taint label for it, these
 * labels are assigned sequentially.
 *
 * Then, we keep track of what canonical labels map to what original file
 * offsets.
 *
 * Then we update the shadow memory region with the new label
 */
void taintTargetRange(const char *mem, int offset, int len, int byte_start,
                      int byte_end, std::string &name) {
  int curr_byte_num = offset;
  int taint_offset_start = -1, taint_offset_end = -1;
  bool processed_bytes = false;
  // Iterate through the memory region marked as tatinted by [base + start, base
  // + end]
  for (char *curr_byte = (char *)mem; curr_byte_num < offset + len;
       curr_byte_num++, curr_byte++) {
    // If byte end is < 0, then we don't care about ranges.
    if (byte_end < 0 ||
        (curr_byte_num >= byte_start && curr_byte_num <= byte_end)) {
      dfsan_label new_label = createCanonicalLabel(curr_byte_num, name);
      dfsan_set_label(new_label, curr_byte, sizeof(char));

      // Log that we tainted data within this function from a taint source etc.
      // logOperation(new_label);
      storeTaintAccess(output_db, new_label, input_id,
                       ByteAccessType::READ_ACCESS);
      if (taint_offset_start == -1) {
        taint_offset_start = curr_byte_num;
        taint_offset_end = curr_byte_num;
      } else if (curr_byte_num > taint_offset_end) {
        taint_offset_end = curr_byte_num;
      }
      processed_bytes = true;
    }
  }
  if (processed_bytes) {
    storeTaintedChunk(output_db, input_id, taint_offset_start,
                      taint_offset_end);
  }
}

void logUnion(const dfsan_label &l1, const dfsan_label &l2,
              const dfsan_label &union_label, const decay_val &init_decay) {
  taint_node_t *new_node = getTaintNode(union_label);
  new_node->p1 = l1;
  new_node->p2 = l2;
  new_node->decay = init_decay;
  storeTaintForestNode(output_db, input_id, union_label, l1, l2);
}

// TODO (Carson) this seems slow and inefficent
// Can we do this without locking?
atomic_dfsan_label *getUnionEntry(const dfsan_label &l1,
                                  const dfsan_label &l2) {
  std::lock_guard<std::mutex> guard(new_table_lock);
  uint64_t key = (static_cast<uint64_t>(l1) << 32) | l2;
  if (new_table.find(key) == new_table.end()) {
    new_table[key] = {0};
    return &new_table[key];
  }
  return &new_table[key];
}

[[nodiscard]] bool taintData(const int &fd, const char *mem, int offset,
                             int len) {
  if (!isTrackingSource(fd)) {
    return false;
  }
  std::pair<int, int> &targ_info = getTargetTaintRange(fd);
  std::string &name = getSourceName(fd);
  taintTargetRange(mem, offset, len, targ_info.first, targ_info.second, name);
  return true;
}