#pragma once

#include "../src/IBookObserver.hpp"
#include <string>

/**
 * @brief Base interface for all analytics plugins.
 * 
 * Extends IBookObserver to provide plugin lifecycle management.
 * All analytics modules should inherit from this class to enable
 * uniform plugin management.
 */
class IAnalyticsPlugin : public IBookObserver {
public:
    virtual ~IAnalyticsPlugin() = default;
    
    /**
     * @brief Get the unique name of this plugin.
     * @return Human-readable plugin name.
     */
    virtual std::string getName() const = 0;
    
    /**
     * @brief Initialize the plugin.
     * Called when plugin is registered with PluginManager.
     */
    virtual void initialize() {}
    
    /**
     * @brief Cleanup plugin resources.
     * Called when plugin is unregistered or during shutdown.
     */
    virtual void cleanup() {}
    
    /**
     * @brief Check if plugin is enabled.
     * @return true if plugin should process updates.
     */
    virtual bool isEnabled() const { return true; }
    
    /**
     * @brief Enable or disable the plugin at runtime.
     * @param enabled true to enable, false to disable.
     */
    virtual void setEnabled(bool enabled) { enabled_ = enabled; }
    
protected:
    bool enabled_ = true;
};
