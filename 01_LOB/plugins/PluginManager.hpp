#pragma once

#include "IAnalyticsPlugin.hpp"
#include "../src/OrderBook.hpp"
#include <vector>
#include <memory>
#include <string>
#include <iostream>

/**
 * @brief Manages the lifecycle of analytics plugins.
 * 
 * Provides a centralized way to register, initialize, and manage
 * all analytics plugins that observe the order book.
 * 
 * @tparam OrderBookType The type of OrderBook being observed.
 */
class PluginManager {
    std::vector<std::unique_ptr<IAnalyticsPlugin>> plugins_;
    OrderBook& book_;
    bool verbose_ = false;

public:
    /**
     * @brief Construct a PluginManager for the given OrderBook.
     * @param book The OrderBook instance to observe.
     * @param verbose If true, print plugin registration messages.
     */
    explicit PluginManager(OrderBook& book, bool verbose = false) 
        : book_(book), verbose_(verbose) {}
    
    /**
     * @brief Destructor - cleans up all registered plugins.
     */
    ~PluginManager() {
        cleanup();
    }
    
    /**
     * @brief Add a plugin of type T with constructor arguments.
     * 
     * The plugin will be automatically registered as an observer
     * of the OrderBook and initialized.
     * 
     * @tparam T The plugin type (must inherit from IAnalyticsPlugin).
     * @tparam Args Constructor argument types.
     * @param args Constructor arguments.
     * @return Reference to the created plugin.
     */
    template<typename T, typename... Args>
    T& addPlugin(Args&&... args) {
        static_assert(std::is_base_of_v<IAnalyticsPlugin, T>, 
                     "Plugin must inherit from IAnalyticsPlugin");
        
        auto plugin = std::make_unique<T>(std::forward<Args>(args)...);
        T* ptr = plugin.get();
        
        // Register with OrderBook
        book_.addObserver(ptr);
        
        // Initialize the plugin
        ptr->initialize();
        
        // Store ownership
        plugins_.push_back(std::move(plugin));
        
        if (verbose_) {
            std::cout << "[PluginManager] Registered plugin: " 
                      << ptr->getName() << std::endl;
        }
        
        return *ptr;
    }
    
    /**
     * @brief Get a plugin by name.
     * @param name The plugin name to find.
     * @return Pointer to the plugin, or nullptr if not found.
     */
    IAnalyticsPlugin* getPlugin(const std::string& name) {
        for (auto& plugin : plugins_) {
            if (plugin->getName() == name) {
                return plugin.get();
            }
        }
        return nullptr;
    }
    
    /**
     * @brief Get a plugin by type.
     * @tparam T The plugin type.
     * @return Pointer to the first plugin of type T, or nullptr if not found.
     */
    template<typename T>
    T* getPlugin() {
        for (auto& plugin : plugins_) {
            T* typed = dynamic_cast<T*>(plugin.get());
            if (typed) {
                return typed;
            }
        }
        return nullptr;
    }
    
    /**
     * @brief Enable or disable a plugin by name.
     * @param name The plugin name.
     * @param enabled true to enable, false to disable.
     * @return true if plugin was found and updated.
     */
    bool setPluginEnabled(const std::string& name, bool enabled) {
        for (auto& plugin : plugins_) {
            if (plugin->getName() == name) {
                plugin->setEnabled(enabled);
                if (verbose_) {
                    std::cout << "[PluginManager] " 
                              << (enabled ? "Enabled" : "Disabled") 
                              << " plugin: " << name << std::endl;
                }
                return true;
            }
        }
        return false;
    }
    
    /**
     * @brief Cleanup all plugins.
     * Calls cleanup() on each plugin and removes them from the OrderBook.
     */
    void cleanup() {
        // Note: Need to remove from OrderBook first, but OrderBook
        // doesn't have a removeObserver method. For now, just
        // call cleanup on each plugin.
        for (auto& plugin : plugins_) {
            plugin->cleanup();
        }
    }
    
    /**
     * @brief Get the number of registered plugins.
     * @return Number of plugins.
     */
    size_t getPluginCount() const {
        return plugins_.size();
    }
    
    /**
     * @brief List all registered plugin names.
     * @return Vector of plugin names.
     */
    std::vector<std::string> getPluginNames() const {
        std::vector<std::string> names;
        for (const auto& plugin : plugins_) {
            names.push_back(plugin->getName());
        }
        return names;
    }
    
    // Delete copy constructor and assignment operator
    PluginManager(const PluginManager&) = delete;
    PluginManager& operator=(const PluginManager&) = delete;
};
