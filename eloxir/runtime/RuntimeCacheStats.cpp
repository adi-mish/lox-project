#include "RuntimeAPI.h"

#include <iostream>

using namespace eloxir;

namespace eloxir {

#if defined(ELOXIR_ENABLE_CACHE_STATS)
void CacheStatsCollector::reset() {
  property_get_hits.store(0, std::memory_order_relaxed);
  property_get_misses.store(0, std::memory_order_relaxed);
  property_get_shape_transitions.store(0, std::memory_order_relaxed);
  property_set_hits.store(0, std::memory_order_relaxed);
  property_set_misses.store(0, std::memory_order_relaxed);
  property_set_shape_transitions.store(0, std::memory_order_relaxed);
  call_hits.store(0, std::memory_order_relaxed);
  call_misses.store(0, std::memory_order_relaxed);
  call_shape_transitions.store(0, std::memory_order_relaxed);
}

CacheStats CacheStatsCollector::snapshot() const {
  CacheStats stats;
  stats.property_get_hits = property_get_hits.load(std::memory_order_relaxed);
  stats.property_get_misses =
      property_get_misses.load(std::memory_order_relaxed);
  stats.property_get_shape_transitions =
      property_get_shape_transitions.load(std::memory_order_relaxed);
  stats.property_set_hits = property_set_hits.load(std::memory_order_relaxed);
  stats.property_set_misses =
      property_set_misses.load(std::memory_order_relaxed);
  stats.property_set_shape_transitions =
      property_set_shape_transitions.load(std::memory_order_relaxed);
  stats.call_hits = call_hits.load(std::memory_order_relaxed);
  stats.call_misses = call_misses.load(std::memory_order_relaxed);
  stats.call_shape_transitions =
      call_shape_transitions.load(std::memory_order_relaxed);
  return stats;
}
#endif

namespace {

#if defined(ELOXIR_ENABLE_CACHE_STATS)
CacheStatsCollector g_cache_stats;
#endif
const CacheStats kEmptyCacheStats{};

} // namespace

} // namespace eloxir

int elx_cache_stats_enabled() {
#if defined(ELOXIR_ENABLE_CACHE_STATS)
  return 1;
#else
  return 0;
#endif
}

void elx_cache_stats_reset() {
#if defined(ELOXIR_ENABLE_CACHE_STATS)
  g_cache_stats.reset();
#endif
}

CacheStats elx_cache_stats_snapshot() {
#if defined(ELOXIR_ENABLE_CACHE_STATS)
  return g_cache_stats.snapshot();
#else
  return kEmptyCacheStats;
#endif
}

void elx_cache_stats_dump() {
#if defined(ELOXIR_ENABLE_CACHE_STATS)
  CacheStats stats = elx_cache_stats_snapshot();
  std::cout << "CACHE_STATS {\"enabled\": true";
  std::cout << ", \"property_get_hits\": " << stats.property_get_hits;
  std::cout << ", \"property_get_misses\": " << stats.property_get_misses;
  std::cout << ", \"property_get_shape_transitions\": "
            << stats.property_get_shape_transitions;
  std::cout << ", \"property_set_hits\": " << stats.property_set_hits;
  std::cout << ", \"property_set_misses\": " << stats.property_set_misses;
  std::cout << ", \"property_set_shape_transitions\": "
            << stats.property_set_shape_transitions;
  std::cout << ", \"call_hits\": " << stats.call_hits;
  std::cout << ", \"call_misses\": " << stats.call_misses;
  std::cout << ", \"call_shape_transitions\": " << stats.call_shape_transitions;
  std::cout << "}" << std::endl;
#else
  std::cout << "CACHE_STATS {\"enabled\": false}" << std::endl;
#endif
}

#if defined(ELOXIR_ENABLE_CACHE_STATS)
void elx_cache_stats_record_property_hit(int is_set) {
  if (is_set) {
    g_cache_stats.property_set_hits.fetch_add(1, std::memory_order_relaxed);
  } else {
    g_cache_stats.property_get_hits.fetch_add(1, std::memory_order_relaxed);
  }
}

void elx_cache_stats_record_property_miss(int is_set) {
  if (is_set) {
    g_cache_stats.property_set_misses.fetch_add(1, std::memory_order_relaxed);
  } else {
    g_cache_stats.property_get_misses.fetch_add(1, std::memory_order_relaxed);
  }
}

void elx_cache_stats_record_property_shape_transition(int is_set) {
  if (is_set) {
    g_cache_stats.property_set_shape_transitions.fetch_add(
        1, std::memory_order_relaxed);
  } else {
    g_cache_stats.property_get_shape_transitions.fetch_add(
        1, std::memory_order_relaxed);
  }
}

void elx_cache_stats_record_call_hit(int /*kind*/) {
  g_cache_stats.call_hits.fetch_add(1, std::memory_order_relaxed);
}

void elx_cache_stats_record_call_miss() {
  g_cache_stats.call_misses.fetch_add(1, std::memory_order_relaxed);
}

void elx_cache_stats_record_call_transition(int /*previous_kind*/,
                                            int /*new_kind*/) {
  g_cache_stats.call_shape_transitions.fetch_add(1, std::memory_order_relaxed);
}
#endif
