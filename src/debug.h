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

#ifdef DEBUG
    #include <spdlog/spdlog.h>

    /// Основной логгер - Debug уровень
    #define DEBUG_LOG(fmt, ...) spdlog::debug("[{}] " fmt, __FILE__, ##__VA_ARGS__)

    /// Информационный лог
    #define DEBUG_INFO(fmt, ...) spdlog::info("[{}] " fmt, __FILE__, ##__VA_ARGS__)

    /// Предупреждение
    #define DEBUG_WARN(fmt, ...) spdlog::warn("[{}] " fmt, __FILE__, ##__VA_ARGS__)

    /// Ошибка
    #define DEBUG_ERROR(fmt, ...) spdlog::error("[{}] " fmt, __FILE__, ##__VA_ARGS__)

    /// Assert с сообщением - продолжает выполнение но логирует
    #define DEBUG_ASSERT(cond, msg) \
        do { if (!(cond)) { spdlog::error("[{}] Assert failed: {}", __FILE__, msg); } } while(0)

    /// Assert который крашит программу (как стандартный assert)
    #define DEBUG_ASSERT_FATAL(cond, msg) \
        do { if (!(cond)) { spdlog::critical("[{}] FATAL: {}", __FILE__, msg); std::abort(); } } while(0)

#else // RELEASE / NDEBUG

    // В RELEASE - всё пустое, ничего не компилируется

    #define DEBUG_LOG(fmt, ...) ((void)0)
    #define DEBUG_INFO(fmt, ...) ((void)0)
    #define DEBUG_WARN(fmt, ...) ((void)0)
    #define DEBUG_ERROR(fmt, ...) ((void)0)
    #define DEBUG_ASSERT(cond, msg) ((void)0)
    #define DEBUG_ASSERT_FATAL(cond, msg) ((void)0)

#endif

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
