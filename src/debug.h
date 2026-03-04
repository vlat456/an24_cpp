#pragma once

/**
 * DEBUG макросы для логирования.
 *
 * Использование:
 *   DEBUG_LOG("Value: {}", value);
 *   DEBUG_INFO("Processing step {}", step);
 *   DEBUG_WARN("Deprecated function called");
 *   DEBUG_ASSERT(ptr != nullptr, "Pointer must not be null");
 *
 * В RELEASE (NDEBUG):
 *   - Весь код удаляется на этапе компиляции
 *   - Нет зависимости от spdlog
 *   - Нет проверок
 */

// Всегда отключено - для отладки использовать printf

#define DEBUG_LOG(fmt, ...) ((void)0)
#define DEBUG_INFO(fmt, ...) ((void)0)
#define DEBUG_WARN(fmt, ...) ((void)0)
#define DEBUG_ERROR(fmt, ...) ((void)0)
#define DEBUG_ASSERT(cond, msg) ((void)0)
#define DEBUG_ASSERT_FATAL(cond, msg) ((void)0)

// =============================================================================
// Примеры использования
// =============================================================================

/*
 *
 * В .cpp файле:
 *
 * #include "debug.h"
 *
 * void process_node(Node* node) {
 *     DEBUG_LOG("Processing node: {}", node->name);
 *
 *     DEBUG_ASSERT(node != nullptr, "Node is null");
 *     DEBUG_ASSERT(node->id >= 0, "Invalid node id");
 *
 *     if (node->state == NodeState::Error) {
 *         DEBUG_WARN("Node {} in error state", node->name);
 *     }
 * }
 *
 * void critical_function() {
 *     auto* ptr = allocate();
 *     DEBUG_ASSERT_FATAL(ptr != nullptr, "Failed to allocate memory");
 * }
 *
 *
 * В Debug билде:
 *   [src/jit_solver/systems.cpp] Processing node: battery_1
 *   [src/jit_solver/systems.cpp] Node battery_1 in error state
 *
 * В Release билде:
 *   - Все вызовы DEBUG_* полностью удалены
 *   - Никаких проверок, никакого оверхеда
 *
 */
