#pragma once
#include <functional>
#include <unordered_map>
#include <vector>
#include <typeindex>
#include <memory>
#include <forge/Logger.h>

namespace forge {

// Base class so we can store handlers polymorphically
struct HandlerListBase {
  virtual ~HandlerListBase() = default;
};

template<typename TEvent>
struct HandlerList : HandlerListBase {
  std::vector<std::function<void(const TEvent&)>> handlers;
};

// EventBus - static pub/sub system
class EventBus {
public:
  // Subscribe to event type
  // Usage: EventBus::subscribe<EntityDiedEvent>([](const EntityDiedEvent& e) {...});
  template<typename TEvent>
  static void subscribe(std::function<void(const TEvent&)> handler) {
    auto& list = getList<TEvent>();
    list.handlers.push_back(std::move(handler));
  }

  template<typename TEvent>
  static void publish(const TEvent& event) {
    auto it = s_handlers.find(std::type_index(typeid(TEvent)));
    if (it == s_handlers.end()) return;

    auto* list = static_cast<HandlerList<TEvent>*>(it->second.get());
    for (auto& handler : list->handlers)
      handler(event);
  }

  // Clear all subscriptions - call inbetween scenes or on reset
  static void clear() { s_handlers.clear(); }

private:
  template<typename TEvent>
  static HandlerList<TEvent>& getList() {
    auto key = std::type_index(typeid(TEvent));
    auto it = s_handlers.find(key);
    if (it == s_handlers.end()) {
      s_handlers[key] = std::make_unique<HandlerList<TEvent>>();
    }
    return *static_cast<HandlerList<TEvent>*>(s_handlers[key].get());
  }

  static std::unordered_map<std::type_index, std::unique_ptr<HandlerListBase>> s_handlers;
};

}
